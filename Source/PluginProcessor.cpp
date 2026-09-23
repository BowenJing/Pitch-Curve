#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

ContourAudioProcessor::ContourAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ContourAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
            [] (float value, int) { return juce::String(juce::roundToInt(value * 100.0f)); })));
    return { parameters.begin(), parameters.end() };
}

void ContourAudioProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;
    freeRunningSample = 0;
    stretcher.presetDefault(getTotalNumInputChannels(), sampleRate);
    stretcher.reset();
    setLatencySamples(stretcher.inputLatency() + stretcher.outputLatency());
    bypassDelay.setMaximumDelayInSamples(juce::jmax(1, getLatencySamples() + 1));
    bypassDelay.prepare({ sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                          static_cast<juce::uint32>(getTotalNumInputChannels()) });
    bypassDelay.reset();
    bypassDelay.setDelay(static_cast<float>(getLatencySamples()));
    processed.setSize(getTotalNumOutputChannels(), juce::jmax(64, maximumBlockSize),
                      false, false, true);
    pitchSmoother.reset(sampleRate, 0.025);
    pitchSmoother.setCurrentAndTargetValue(0.0f);
    effectMix.reset(sampleRate, 0.010);
    effectMix.setCurrentAndTargetValue(0.0f);
}

void ContourAudioProcessor::releaseResources()
{
    processed.setSize(0, 0);
}

bool ContourAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

float ContourAudioProcessor::curveValueAt(float position,
                                          const CurveData& curve) const
{
    if (curve.pointCount == 0)
        return 0.0f;
    const auto begin = curve.points.begin();
    const auto end = begin + static_cast<std::ptrdiff_t>(curve.pointCount);
    if (position <= begin->position)
        return begin->cents;
    if (position >= (end - 1)->position)
        return (end - 1)->cents;

    const auto upper = std::lower_bound(begin, end, position,
        [] (const PitchPoint& point, float value) { return point.position < value; });
    const auto lower = upper - 1;
    const float span = upper->position - lower->position;
    const float proportion = span > 0.0f ? (position - lower->position) / span : 0.0f;
    return juce::jmap(proportion, lower->cents, upper->cents);
}

void ContourAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockInternal(buffer, false);
}

void ContourAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    processBlockInternal(buffer, true);
}

void ContourAudioProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer,
                                                  bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(buffer.getNumChannels(), getTotalNumOutputChannels());
    const int samples = buffer.getNumSamples();
    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, samples);

    int curveIndex = 0;
    do
    {
        curveIndex = publishedCurveIndex.load(std::memory_order_acquire);
        audioReadingCurveIndex.store(curveIndex, std::memory_order_release);
    }
    while (curveIndex != publishedCurveIndex.load(std::memory_order_acquire));
    const auto& curve = curveBuffers[static_cast<size_t>(curveIndex)];
    const float duration = curve.durationSeconds;
    const float amount = state.getRawParameterValue("amount")->load();
    const bool contourEnabled = ! forceBypass && curve.pointCount > 0 && duration > 0.0f
                             && std::abs(amount) > 1.0e-6f;
    effectMix.setTargetValue(contourEnabled ? 1.0f : 0.0f);

    bool transportStopped = false;
    int64_t timelineSample = freeRunningSample;
    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
        {
            transportStopped = ! position->getIsPlaying();
            if (transportStopped)
                timelineSample = stretcher.inputLatency();
            else if (const auto hostSample = position->getTimeInSamples())
                timelineSample = *hostSample;
        }

    const auto durationSamples = juce::jmax<int64_t>(
        1, static_cast<int64_t>(duration * currentSampleRate));
    constexpr int controlBlockSize = 64;
    for (int offset = 0; offset < samples; offset += controlBlockSize)
    {
        const int blockSamples = juce::jmin(controlBlockSize, samples - offset);
        const auto blockTimeline = timelineSample + offset - stretcher.inputLatency();
        const auto wrappedSample =
            ((blockTimeline % durationSamples) + durationSamples) % durationSamples;
        const float position =
            static_cast<float>(wrappedSample) / static_cast<float>(durationSamples);
        displayPosition.store(transportStopped ? 0.0f : position);

        pitchSmoother.setTargetValue(contourEnabled ? curveValueAt(position, curve) * amount : 0.0f);
        stretcher.setTransposeSemitones(pitchSmoother.skip(blockSamples) / 100.0f);

        std::array<const float*, 2> inputPointers {};
        std::array<float*, 2> outputPointers {};
        for (int channel = 0; channel < channels; ++channel)
        {
            inputPointers[static_cast<size_t>(channel)] = buffer.getReadPointer(channel, offset);
            outputPointers[static_cast<size_t>(channel)] = processed.getWritePointer(channel);
        }
        stretcher.process(inputPointers.data(), blockSamples,
                          outputPointers.data(), blockSamples);

        for (int sample = 0; sample < blockSamples; ++sample)
        {
            const float mix = effectMix.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                const float input = buffer.getSample(channel, offset + sample);
                bypassDelay.pushSample(channel, input);
                const float bypass = bypassDelay.popSample(channel);
                const float wet = processed.getSample(channel, sample);
                buffer.setSample(channel, offset + sample,
                                 bypass + mix * (wet - bypass));
            }
        }
    }
    audioReadingCurveIndex.store(-1, std::memory_order_release);
    freeRunningSample = transportStopped ? 0 : freeRunningSample + samples;
}

void ContourAudioProcessor::setContour(std::vector<PitchPoint> points, float durationSeconds)
{
    points.erase(std::remove_if(points.begin(), points.end(), [] (const PitchPoint& point)
    {
        return ! std::isfinite(point.position) || ! std::isfinite(point.cents)
            || ! std::isfinite(point.confidence);
    }), points.end());

    for (auto& point : points)
    {
        point.position = juce::jlimit(0.0f, 1.0f, point.position);
        point.cents = juce::jlimit(-200.0f, 200.0f, point.cents);
        point.confidence = juce::jlimit(0.0f, 1.0f, point.confidence);
    }

    std::sort(points.begin(), points.end(),
              [] (const PitchPoint& a, const PitchPoint& b) { return a.position < b.position; });
    constexpr size_t maximumInteriorPoints = maximumCurvePoints - 2;
    if (points.size() > maximumInteriorPoints)
    {
        std::vector<PitchPoint> reduced;
        reduced.reserve(maximumInteriorPoints);
        for (size_t i = 0; i < maximumInteriorPoints; ++i)
            reduced.push_back(points[i * (points.size() - 1) / (maximumInteriorPoints - 1)]);
        points = std::move(reduced);
    }

    if (! points.empty())
    {
        const float boundaryCents = 0.5f * (points.front().cents + points.back().cents);
        const float boundaryConfidence = juce::jmin(points.front().confidence,
                                                    points.back().confidence);
        if (points.front().position > 0.0f)
            points.insert(points.begin(), { 0.0f, boundaryCents, boundaryConfidence });
        else
            points.front() = { 0.0f, boundaryCents, boundaryConfidence };

        if (points.back().position < 1.0f)
            points.push_back({ 1.0f, boundaryCents, boundaryConfidence });
        else
            points.back() = { 1.0f, boundaryCents, boundaryConfidence };
    }

    if (! std::isfinite(durationSeconds))
        durationSeconds = 2.0f;

    std::lock_guard<std::mutex> lock(curveWriterMutex);
    const int published = publishedCurveIndex.load(std::memory_order_acquire);
    const int reading = audioReadingCurveIndex.load(std::memory_order_acquire);
    int target = 0;
    while (target == published || target == reading)
        ++target;

    auto& updated = curveBuffers[static_cast<size_t>(target)];
    updated.pointCount = points.size();
    std::copy(points.begin(), points.end(), updated.points.begin());
    updated.durationSeconds = juce::jlimit(1.0f / 30.0f, 600.0f, durationSeconds);
    publishedCurveIndex.store(target, std::memory_order_release);
    contourRevision.fetch_add(1);
}

std::vector<PitchPoint> ContourAudioProcessor::getContour() const
{
    std::lock_guard<std::mutex> lock(curveWriterMutex);
    const auto& curve = curveBuffers[static_cast<size_t>(
        publishedCurveIndex.load(std::memory_order_acquire))];
    return { curve.points.begin(),
             curve.points.begin() + static_cast<std::ptrdiff_t>(curve.pointCount) };
}

float ContourAudioProcessor::getContourDuration() const
{
    std::lock_guard<std::mutex> lock(curveWriterMutex);
    return curveBuffers[static_cast<size_t>(
        publishedCurveIndex.load(std::memory_order_acquire))].durationSeconds;
}

void ContourAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto root = state.copyState();
    juce::ValueTree curve("CONTOUR");
    curve.setProperty("duration", getContourDuration(), nullptr);
    for (const auto& point : getContour())
    {
        juce::ValueTree node("POINT");
        node.setProperty("x", point.position, nullptr);
        node.setProperty("cents", point.cents, nullptr);
        node.setProperty("confidence", point.confidence, nullptr);
        curve.addChild(node, -1, nullptr);
    }
    root.addChild(curve, -1, nullptr);

    if (const auto xml = root.createXml())
        copyXmlToBinary(*xml, destination);
}

void ContourAudioProcessor::setStateInformation(const void* data, int size)
{
    constexpr int maximumStateBytes = 2 * 1024 * 1024;
    constexpr int maximumStatePoints = 4096;
    if (data == nullptr || size <= 0 || size > maximumStateBytes)
        return;

    if (const auto xml = getXmlFromBinary(data, size))
    {
        const auto root = juce::ValueTree::fromXml(*xml);
        if (! root.isValid() || ! root.hasType("PARAMETERS"))
            return;

        if (const auto curve = root.getChildWithName("CONTOUR"); curve.isValid())
        {
            std::vector<PitchPoint> restored;
            restored.reserve(static_cast<size_t>(
                juce::jmin(curve.getNumChildren(), maximumStatePoints)));
            for (int i = 0; i < curve.getNumChildren() && i < maximumStatePoints; ++i)
            {
                const auto node = curve.getChild(i);
                if (node.hasType("POINT"))
                    restored.push_back({ node["x"], node["cents"], node["confidence"] });
            }
            setContour(std::move(restored), curve.getProperty("duration", 2.0f));
        }

        auto parameterState = root.createCopy();
        if (const auto curve = parameterState.getChildWithName("CONTOUR"); curve.isValid())
            parameterState.removeChild(curve, nullptr);
        state.replaceState(parameterState);
    }
}

juce::AudioProcessorEditor* ContourAudioProcessor::createEditor()
{
    return new ContourAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ContourAudioProcessor();
}
