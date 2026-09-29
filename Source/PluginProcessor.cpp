#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "CurveSmoothing.h"

#include <cctype>
#include <cmath>

namespace
{
bool isSafeStatePayload(const void* data, int size)
{
    constexpr uint32_t xmlMagic = 0x21324356;
    constexpr int maximumXmlBytes = 1024 * 1024;
    constexpr int maximumXmlNodes = 8192;
    constexpr int maximumXmlDepth = 8;

    if (data == nullptr || size <= 8
        || juce::ByteOrder::littleEndianInt(data) != xmlMagic)
        return false;

    const int xmlBytes = static_cast<int>(juce::ByteOrder::littleEndianInt(
        juce::addBytesToPointer(data, 4)));
    if (xmlBytes <= 0 || xmlBytes > size - 8 || xmlBytes > maximumXmlBytes)
        return false;

    const auto* xml = static_cast<const char*>(data) + 8;
    int depth = 0;
    int nodeCount = 0;
    for (int i = 0; i < xmlBytes; ++i)
    {
        if (xml[i] == '\0')
            return false;
        if (xml[i] != '<')
            continue;
        if (++i >= xmlBytes)
            return false;

        const bool processingInstruction = xml[i] == '?';
        const bool closingTag = xml[i] == '/';
        if (xml[i] == '!')
            return false; // State never needs comments, CDATA, DTDs, or entities.

        char quote = 0;
        char lastNonSpace = 0;
        bool foundEnd = false;
        for (; i < xmlBytes; ++i)
        {
            const char character = xml[i];
            if (quote != 0)
            {
                if (character == quote)
                    quote = 0;
                continue;
            }
            if (character == '"' || character == '\'')
            {
                quote = character;
                continue;
            }
            if (character == '>')
            {
                foundEnd = true;
                break;
            }
            if (! std::isspace(static_cast<unsigned char>(character)))
                lastNonSpace = character;
        }
        if (! foundEnd || quote != 0)
            return false;
        if (processingInstruction)
            continue;

        if (closingTag)
        {
            if (depth <= 0)
                return false;
            --depth;
            continue;
        }

        if (++nodeCount > maximumXmlNodes || ++depth > maximumXmlDepth)
            return false;
        if (lastNonSpace == '/')
            --depth;
    }
    return nodeCount > 0 && depth == 0;
}
}

ContourAudioProcessor::ContourAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& slot : curveSlotState)
        slot.store(0, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout ContourAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
            [] (float value, int) { return juce::String(juce::roundToInt(value * 100.0f)); })));
    parameters.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "smooth", 1 }, "Smooth", 0, 10, 5));
    return { parameters.begin(), parameters.end() };
}

void ContourAudioProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    PitchCurveSmoothing::prepare();
    currentSampleRate = sampleRate;
    freeRunningSample = 0;
    hostPlaybackSample = 0;
    hostWasPlaying = false;
    displayPosition.store(0.0f);
    displayPlaying.store(false);
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

float ContourAudioProcessor::smoothedCurveValueAt(float position,
                                                   const CurveData& curve,
                                                   int smooth) const
{
    return PitchCurveSmoothing::valueAt(
        position, smooth,
        [this, &curve] (float samplePosition)
        {
            return curveValueAt(samplePosition, curve);
        });
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
    for (;;)
    {
        curveIndex = publishedCurveIndex.load(std::memory_order_acquire);
        int expected = 0;
        if (curveSlotState[static_cast<size_t>(curveIndex)].compare_exchange_weak(
                expected, 1, std::memory_order_acquire, std::memory_order_relaxed))
            break;
    }
    const auto& curve = curveBuffers[static_cast<size_t>(curveIndex)];
    const float duration = curve.durationSeconds;
    const float amount = state.getRawParameterValue("amount")->load();
    const int smooth = juce::roundToInt(state.getRawParameterValue("smooth")->load());
    const bool contourEnabled = ! forceBypass && curve.pointCount > 0 && duration > 0.0f
                             && std::abs(amount) > 1.0e-6f;
    effectMix.setTargetValue(contourEnabled ? 1.0f : 0.0f);

    bool hasHostTransport = false;
    bool transportStopped = false;
    int64_t timelineSample = freeRunningSample + stretcher.inputLatency();
    if (auto* hostPlayHead = getPlayHead();
        hostPlayHead != nullptr && wrapperType != wrapperType_Standalone)
    {
        hasHostTransport = true;
        transportStopped = true;
        if (const auto position = hostPlayHead->getPosition())
        {
            transportStopped = ! position->getIsPlaying();
            if (transportStopped)
            {
                hostPlaybackSample = 0;
                hostWasPlaying = false;
                timelineSample = stretcher.inputLatency();
            }
            else
            {
                if (! hostWasPlaying)
                    hostPlaybackSample = 0;
                hostWasPlaying = true;
                timelineSample = hostPlaybackSample + stretcher.inputLatency();
            }
        }
        else
        {
            hostPlaybackSample = 0;
            hostWasPlaying = false;
            timelineSample = stretcher.inputLatency();
        }
    }
    displayPlaying.store(! hasHostTransport || ! transportStopped,
                         std::memory_order_relaxed);

    const auto durationSamples = juce::jmax<int64_t>(
        1, static_cast<int64_t>(duration * currentSampleRate));
    constexpr int controlBlockSize = 64;
    for (int offset = 0; offset < samples; offset += controlBlockSize)
    {
        const int blockSamples = juce::jmin(controlBlockSize, samples - offset);
        const auto blockTimeline = timelineSample + offset - stretcher.inputLatency()
                                 + blockSamples / 2;
        const auto wrappedSample =
            ((blockTimeline % durationSamples) + durationSamples) % durationSamples;
        const float position =
            static_cast<float>(wrappedSample) / static_cast<float>(durationSamples);
        displayPosition.store(transportStopped ? 0.0f : position);

        const float targetCents = contourEnabled
            ? juce::jlimit(-1200.0f, 1200.0f,
                           smoothedCurveValueAt(position, curve, smooth) * amount)
            : 0.0f;
        stretcher.setTransposeSemitones(targetCents / 100.0f);

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
    curveSlotState[static_cast<size_t>(curveIndex)].store(0, std::memory_order_release);
    if (hasHostTransport)
    {
        if (! transportStopped)
            hostPlaybackSample += samples;
    }
    else
    {
        hostWasPlaying = false;
        hostPlaybackSample = 0;
        freeRunningSample += samples;
    }
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
        point.cents = juce::jlimit(-600.0f, 600.0f, point.cents);
        point.confidence = juce::jlimit(0.0f, 1.0f, point.confidence);
    }

    std::stable_sort(points.begin(), points.end(),
                     [] (const PitchPoint& a, const PitchPoint& b)
                     {
                         return a.position < b.position;
                     });
    points.erase(std::unique(points.begin(), points.end(),
        [] (const PitchPoint& a, const PitchPoint& b)
        {
            return a.position == b.position;
        }), points.end());
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
    int target = -1;
    while (target < 0)
    {
        for (int candidate = 0; candidate < static_cast<int>(curveBuffers.size()); ++candidate)
        {
            if (candidate == published)
                continue;
            int expected = 0;
            if (curveSlotState[static_cast<size_t>(candidate)].compare_exchange_strong(
                    expected, -1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                target = candidate;
                break;
            }
        }
        if (target < 0)
            juce::Thread::yield();
    }

    auto& updated = curveBuffers[static_cast<size_t>(target)];
    updated.pointCount = points.size();
    std::copy(points.begin(), points.end(), updated.points.begin());
    updated.durationSeconds = juce::jlimit(1.0f / 30.0f, 60.0f, durationSeconds);
    curveSlotState[static_cast<size_t>(target)].store(0, std::memory_order_release);
    publishedCurveIndex.store(target, std::memory_order_release);
    contourRevision.fetch_add(1);
}

float ContourAudioProcessor::setLearnedContour(std::vector<PitchPoint> points,
                                               float detectedDurationSeconds)
{
    const float appliedDuration = isDurationLocked()
        ? getContourDuration()
        : detectedDurationSeconds;
    setContour(std::move(points), appliedDuration);
    return getContourDuration();
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
    CurveData snapshot;
    {
        std::lock_guard<std::mutex> lock(curveWriterMutex);
        snapshot = curveBuffers[static_cast<size_t>(
            publishedCurveIndex.load(std::memory_order_acquire))];
    }

    juce::ValueTree curve("CONTOUR");
    curve.setProperty("schema", 2, nullptr);
    curve.setProperty("duration", snapshot.durationSeconds, nullptr);
    curve.setProperty("durationLocked", isDurationLocked(), nullptr);
    for (size_t i = 0; i < snapshot.pointCount; ++i)
    {
        const auto& point = snapshot.points[i];
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
    if (data == nullptr || size <= 0 || size > maximumStateBytes
        || ! isSafeStatePayload(data, size))
        return;

    if (const auto xml = getXmlFromBinary(data, size))
    {
        const auto root = juce::ValueTree::fromXml(*xml);
        if (! root.isValid() || ! root.hasType("PARAMETERS"))
            return;

        if (const auto curve = root.getChildWithName("CONTOUR"); curve.isValid())
        {
            setDurationLocked(static_cast<bool>(
                curve.getProperty("durationLocked", false)));
            std::vector<PitchPoint> restored;
            restored.reserve(static_cast<size_t>(
                juce::jmin(curve.getNumChildren(), maximumStatePoints)));
            for (int i = 0; i < curve.getNumChildren() && i < maximumStatePoints; ++i)
            {
                const auto node = curve.getChild(i);
                if (node.hasType("POINT"))
                    restored.push_back({ node["x"], node["cents"], node["confidence"] });
            }
            if (static_cast<int>(curve.getProperty("schema", 0)) < 2)
            {
                float maximumAbsoluteCents = 0.0f;
                for (const auto& point : restored)
                    maximumAbsoluteCents =
                        juce::jmax(maximumAbsoluteCents, std::abs(point.cents));
                if (maximumAbsoluteCents > 600.0f)
                    for (auto& point : restored)
                        point.cents *= 0.5f;
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
