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
    setLatencySamples(stretcher.outputLatency());
    bypassDelay.setMaximumDelayInSamples(juce::jmax(1, getLatencySamples() + 1));
    bypassDelay.prepare({ sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                          static_cast<juce::uint32>(getTotalNumInputChannels()) });
    bypassDelay.reset();
    bypassDelay.setDelay(static_cast<float>(getLatencySamples()));
    processed.setSize(getTotalNumOutputChannels(), juce::jmax(1, maximumBlockSize),
                      false, false, true);
    pitchSmoother.reset(sampleRate, 0.025);
    pitchSmoother.setCurrentAndTargetValue(0.0f);
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
                                          const std::vector<PitchPoint>& points) const
{
    if (points.empty())
        return 0.0f;
    if (position <= points.front().position)
        return points.front().cents;
    if (position >= points.back().position)
        return points.back().cents;

    const auto upper = std::lower_bound(points.begin(), points.end(), position,
        [] (const PitchPoint& point, float value) { return point.position < value; });
    const auto lower = upper - 1;
    const float span = upper->position - lower->position;
    const float proportion = span > 0.0f ? (position - lower->position) / span : 0.0f;
    return juce::jmap(proportion, lower->cents, upper->cents);
}

void ContourAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(buffer.getNumChannels(), getTotalNumOutputChannels());
    const int samples = buffer.getNumSamples();
    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, 0, samples);

    const auto currentCurve = std::atomic_load(&curveData);
    const auto& points = currentCurve->points;
    const float duration = currentCurve->durationSeconds;

    const bool canProcessContour = ! points.empty() && duration > 0.0f
                                && samples <= processed.getNumSamples();
    for (int sample = 0; sample < samples; ++sample)
        for (int channel = 0; channel < channels; ++channel)
        {
            bypassDelay.pushSample(channel, buffer.getSample(channel, sample));
            const float delayed = bypassDelay.popSample(channel);
            if (! canProcessContour)
                buffer.setSample(channel, sample, delayed);
        }

    if (! canProcessContour)
    {
        freeRunningSample += samples;
        return;
    }

    int64_t timelineSample = freeRunningSample;
    if (auto* hostPlayHead = getPlayHead())
        if (const auto position = hostPlayHead->getPosition())
            if (const auto hostSample = position->getTimeInSamples())
                timelineSample = *hostSample;

    const auto durationSamples = juce::jmax<int64_t>(
        1, static_cast<int64_t>(duration * currentSampleRate));
    const float amount = state.getRawParameterValue("amount")->load();
    processed.clear(0, samples);
    constexpr int controlBlockSize = 64;
    for (int offset = 0; offset < samples; offset += controlBlockSize)
    {
        const int blockSamples = juce::jmin(controlBlockSize, samples - offset);
        const auto blockTimeline = timelineSample + offset;
        const auto wrappedSample =
            ((blockTimeline % durationSamples) + durationSamples) % durationSamples;
        const float position =
            static_cast<float>(wrappedSample) / static_cast<float>(durationSamples);
        displayPosition.store(position);

        pitchSmoother.setTargetValue(curveValueAt(position, points) * amount);
        stretcher.setTransposeSemitones(pitchSmoother.skip(blockSamples) / 100.0f);

        std::array<const float*, 2> inputPointers {};
        std::array<float*, 2> outputPointers {};
        for (int channel = 0; channel < channels; ++channel)
        {
            inputPointers[static_cast<size_t>(channel)] = buffer.getReadPointer(channel, offset);
            outputPointers[static_cast<size_t>(channel)] = processed.getWritePointer(channel, offset);
        }
        stretcher.process(inputPointers.data(), blockSamples,
                          outputPointers.data(), blockSamples);
    }
    for (int channel = 0; channel < channels; ++channel)
        buffer.copyFrom(channel, 0, processed, channel, 0, samples);

    freeRunningSample += samples;
}

void ContourAudioProcessor::setContour(std::vector<PitchPoint> points, float durationSeconds)
{
    constexpr size_t maximumPoints = 4096;
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
    if (points.size() > maximumPoints)
    {
        std::vector<PitchPoint> reduced;
        reduced.reserve(maximumPoints);
        for (size_t i = 0; i < maximumPoints; ++i)
            reduced.push_back(points[i * (points.size() - 1) / (maximumPoints - 1)]);
        points = std::move(reduced);
    }

    if (! std::isfinite(durationSeconds))
        durationSeconds = 2.0f;
    auto updated = std::make_shared<const CurveData>(
        CurveData { std::move(points), juce::jlimit(0.1f, 600.0f, durationSeconds) });
    std::atomic_store(&curveData, std::move(updated));
    contourRevision.fetch_add(1);
}

std::vector<PitchPoint> ContourAudioProcessor::getContour() const
{
    return std::atomic_load(&curveData)->points;
}

float ContourAudioProcessor::getContourDuration() const
{
    return std::atomic_load(&curveData)->durationSeconds;
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
