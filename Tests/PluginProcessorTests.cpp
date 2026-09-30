#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CurveSmoothing.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace
{
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        return positionAvailable ? juce::Optional<PositionInfo> { position } : std::nullopt;
    }
    PositionInfo position;
    bool positionAvailable = true;
};

juce::MemoryBlock makeRawXmlState(const juce::String& xml)
{
    juce::MemoryBlock result;
    juce::MemoryOutputStream output(result, false);
    output.writeInt(0x21324356);
    output.writeInt(xml.getNumBytesAsUTF8());
    output.write(xml.toRawUTF8(), static_cast<size_t>(xml.getNumBytesAsUTF8()));
    output.writeByte(0);
    return result;
}

bool testLatencyMatchedBypass()
{
    constexpr double sampleRate = 48000.0;
    constexpr int preparedBlockSize = 32;
    constexpr int hostBlockSize = 257;
    for (const bool useHostBypass : { false, true })
    {
        ContourAudioProcessor processor;
        processor.prepareToPlay(sampleRate, preparedBlockSize);

        const int latency = processor.getLatencySamples();
        if (latency <= 0)
        {
            std::cerr << "Processor must report the pitch shifter latency\n";
            return false;
        }

        const int totalSamples = latency + hostBlockSize * 2;
        std::vector<float> output(static_cast<size_t>(totalSamples), 0.0f);
        juce::MidiBuffer midi;
        for (int offset = 0; offset < totalSamples; offset += hostBlockSize)
        {
            const int blockSamples = juce::jmin(hostBlockSize, totalSamples - offset);
            juce::AudioBuffer<float> block(2, blockSamples);
            block.clear();
            if (offset == 0)
            {
                block.setSample(0, 0, 1.0f);
                block.setSample(1, 0, 1.0f);
            }
            if (useHostBypass)
                processor.processBlockBypassed(block, midi);
            else
                processor.processBlock(block, midi);
            for (int sample = 0; sample < blockSamples; ++sample)
            {
                if (! std::isfinite(block.getSample(0, sample))
                    || ! std::isfinite(block.getSample(1, sample)))
                {
                    std::cerr << "Bypass produced a non-finite sample\n";
                    return false;
                }
                output[static_cast<size_t>(offset + sample)] = block.getSample(0, sample);
            }
        }

        for (int sample = 0; sample < totalSamples; ++sample)
        {
            const float expected = sample == latency ? 1.0f : 0.0f;
            if (std::abs(output[static_cast<size_t>(sample)] - expected) > 1.0e-6f)
            {
                std::cerr << (useHostBypass ? "Host bypass" : "Empty contour")
                          << " latency mismatch at sample " << sample
                          << ", expected " << expected << " and got "
                          << output[static_cast<size_t>(sample)] << '\n';
                return false;
            }
        }
    }
    return true;
}

bool testContourWithOversizedBlocks()
{
    ContourAudioProcessor processor;
    constexpr double sampleRate = 48000.0;
    constexpr int preparedBlockSize = 16;
    constexpr int hostBlockSize = 509;
    processor.prepareToPlay(sampleRate, preparedBlockSize);
    processor.setContour({ { 0.0f, -35.0f, 1.0f }, { 1.0f, 35.0f, 1.0f } }, 1.0f);
    const auto closedCurve = processor.getContour();
    if (closedCurve.size() < 2
        || std::abs(closedCurve.front().cents - closedCurve.back().cents) > 1.0e-6f)
    {
        std::cerr << "Contour loop endpoints must be continuous\n";
        return false;
    }

    const int totalSamples = processor.getLatencySamples() + 4096;
    double phase = 0.0;
    float maximumOutput = 0.0f;
    juce::MidiBuffer midi;
    for (int offset = 0; offset < totalSamples; offset += hostBlockSize)
    {
        const int blockSamples = juce::jmin(hostBlockSize, totalSamples - offset);
        juce::AudioBuffer<float> block(2, blockSamples);
        for (int sample = 0; sample < blockSamples; ++sample)
        {
            phase += juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
            const float value = static_cast<float>(0.5 * std::sin(phase));
            block.setSample(0, sample, value);
            block.setSample(1, sample, value);
        }
        processor.processBlock(block, midi);
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int sample = 0; sample < blockSamples; ++sample)
            {
                const float value = block.getSample(channel, sample);
                if (! std::isfinite(value))
                {
                    std::cerr << "Contour processing produced a non-finite sample\n";
                    return false;
                }
                maximumOutput = juce::jmax(maximumOutput, std::abs(value));
            }
    }

    if (maximumOutput < 0.01f)
    {
        std::cerr << "Contour processing remained silent after its reported latency\n";
        return false;
    }
    return true;
}

bool testStateRoundTripAndBounds()
{
    ContourAudioProcessor source;
    source.setContour({ { 0.2f, -20.0f, 0.8f }, { 0.8f, 30.0f, 0.9f } }, 3.5f);
    if (auto* amount = source.parameters().getParameter("amount"))
        amount->setValueNotifyingHost(amount->convertTo0to1(1.5f));
    if (auto* smooth = source.parameters().getParameter("smooth"))
        smooth->setValueNotifyingHost(smooth->convertTo0to1(10.0f));
    source.setDurationLocked(true);

    juce::MemoryBlock state;
    source.getStateInformation(state);
    ContourAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto restoredCurve = restored.getContour();
    if (restoredCurve.size() != 4
        || std::abs(restoredCurve.front().position) > 1.0e-6f
        || std::abs(restoredCurve.back().position - 1.0f) > 1.0e-6f
        || std::abs(restoredCurve.front().cents - restoredCurve.back().cents) > 1.0e-6f
        || std::abs(restored.getContourDuration() - 3.5f) > 1.0e-6f
        || ! restored.isDurationLocked()
        || std::abs(restored.parameters().getRawParameterValue("amount")->load() - 1.5f)
               > 1.0e-6f
        || std::abs(restored.parameters().getRawParameterValue("smooth")->load() - 10.0f)
               > 1.0e-6f)
    {
        std::cerr << "Contour state did not round-trip safely\n";
        return false;
    }

    constexpr size_t oversizedStateBytes = 2 * 1024 * 1024 + 1;
    juce::MemoryBlock oversizedState(oversizedStateBytes, true);
    restored.setStateInformation(oversizedState.getData(),
                                 static_cast<int>(oversizedState.getSize()));
    if (restored.getContour().size() != restoredCurve.size())
    {
        std::cerr << "Oversized state must be rejected without changing the contour\n";
        return false;
    }

    const auto entityState = makeRawXmlState(
        "<?xml version=\"1.0\"?><!DOCTYPE PARAMETERS ["
        "<!ENTITY repeated \"xxxxxxxxxxxxxxxx\">]>"
        "<PARAMETERS amount=\"&repeated;\"/>");
    restored.setStateInformation(entityState.getData(),
                                 static_cast<int>(entityState.getSize()));
    if (restored.getContour().size() != restoredCurve.size())
    {
        std::cerr << "DTD state must be rejected before XML expansion\n";
        return false;
    }

    juce::String deepXml("<?xml version=\"1.0\"?><PARAMETERS>");
    for (int i = 0; i < 9; ++i)
        deepXml += "<N>";
    for (int i = 0; i < 9; ++i)
        deepXml += "</N>";
    deepXml += "</PARAMETERS>";
    const auto deepState = makeRawXmlState(deepXml);
    restored.setStateInformation(deepState.getData(),
                                 static_cast<int>(deepState.getSize()));
    if (restored.getContour().size() != restoredCurve.size())
    {
        std::cerr << "Deep state must be rejected before recursive conversion\n";
        return false;
    }

    ContourAudioProcessor bounded;
    bounded.setContour({ { 0.25f, -5000.0f, 1.0f },
                         { 0.75f, 5000.0f, 1.0f } }, 1.0f);
    const auto boundedCurve = bounded.getContour();
    if (boundedCurve.size() != 4
        || std::abs(boundedCurve[1].cents + 600.0f) > 1.0e-6f
        || std::abs(boundedCurve[2].cents - 600.0f) > 1.0e-6f)
    {
        std::cerr << "Editable base pitch range must be clamped to +/-6 semitones\n";
        return false;
    }

    bounded.setContour({ { 0.5f, -10.0f, 1.0f },
                         { 0.5f, 20.0f, 0.5f } }, 1.0f);
    const auto deduplicated = bounded.getContour();
    if (deduplicated.size() != 3
        || std::abs(deduplicated[1].position - 0.5f) > 1.0e-6f)
    {
        std::cerr << "Duplicate contour positions must be canonicalized\n";
        return false;
    }
    return true;
}

bool testDefaultsAndDurationLimit()
{
    ContourAudioProcessor processor;
    if (processor.isDurationLocked()
        || std::abs(processor.parameters().getRawParameterValue("smooth")->load() - 5.0f)
        > 1.0e-6f)
    {
        std::cerr << "Smooth and duration lock defaults are incorrect\n";
        return false;
    }

    processor.setContour({ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } }, 2.0f);
    processor.setDurationLocked(true);
    const float lockedDuration = processor.setLearnedContour(
        { { 0.0f, -40.0f, 1.0f }, { 1.0f, 40.0f, 1.0f } }, 8.0f);
    if (std::abs(lockedDuration - 2.0f) > 1.0e-6f
        || std::abs(processor.getContourDuration() - 2.0f) > 1.0e-6f)
    {
        std::cerr << "Learning must preserve a locked curve duration\n";
        return false;
    }

    processor.setDurationLocked(false);
    const float unlockedDuration = processor.setLearnedContour(
        { { 0.0f, -20.0f, 1.0f }, { 1.0f, 20.0f, 1.0f } }, 8.0f);
    if (std::abs(unlockedDuration - 8.0f) > 1.0e-6f)
    {
        std::cerr << "Learning must update an unlocked curve duration\n";
        return false;
    }

    processor.setContour({ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f } },
                         600.0f);
    if (std::abs(processor.getContourDuration() - 60.0f) > 1.0e-6f)
    {
        std::cerr << "Curve duration must be limited to 60 seconds\n";
        return false;
    }
    return true;
}

bool testStoppedTransportResetsDisplay()
{
    ContourAudioProcessor processor;
    constexpr double sampleRate = 48000.0;
    processor.prepareToPlay(sampleRate, 128);
    processor.setContour({ { 0.0f, -20.0f, 1.0f }, { 1.0f, -20.0f, 1.0f } }, 1.0f);

    TestPlayHead playHead;
    playHead.positionAvailable = false;
    processor.setPlayHead(&playHead);

    juce::AudioBuffer<float> block(2, 128);
    block.clear();
    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)
        processor.processBlock(block, midi);
    if (std::abs(processor.getPlayheadPosition()) > 1.0e-6f)
    {
        std::cerr << "Unavailable initial host position must hold the curve at its start\n";
        return false;
    }
    if (processor.isPlayheadRunning())
    {
        std::cerr << "Unavailable host position must mark the playhead as stopped\n";
        return false;
    }

    playHead.positionAvailable = true;
    playHead.position.setIsPlaying(true);
    playHead.position.setTimeInSamples(24000);
    processor.processBlock(block, midi);
    if (processor.getPlayheadPosition() > 0.01f)
    {
        std::cerr << "New playback must begin at the start of the curve\n";
        return false;
    }
    if (! processor.isPlayheadRunning())
    {
        std::cerr << "Playing transport must mark the playhead as running\n";
        return false;
    }

    for (int i = 0; i < 100; ++i)
        processor.processBlock(block, midi);
    if (processor.getPlayheadPosition() < 0.1f)
    {
        std::cerr << "Continuous playback must advance the curve display\n";
        return false;
    }

    playHead.position.setIsPlaying(false);
    processor.processBlock(block, midi);
    if (std::abs(processor.getPlayheadPosition()) > 1.0e-6f)
    {
        std::cerr << "Stopped transport must reset the curve display\n";
        return false;
    }
    if (processor.isPlayheadRunning())
    {
        std::cerr << "Stopped transport must mark the playhead as stopped\n";
        return false;
    }

    playHead.position.setIsPlaying(true);
    playHead.position.setTimeInSamples(960000);
    processor.processBlock(block, midi);
    if (processor.getPlayheadPosition() > 0.01f)
    {
        std::cerr << "Restarted transport must restart the curve from its beginning\n";
        return false;
    }
    return true;
}

bool testSmoothScaleEndpoints()
{
    struct Point
    {
        float position;
        float value;
    };
    const std::vector<Point> points {
        { 0.00f,   0.0f },
        { 0.05f, 500.0f },
        { 0.10f,   0.0f },
        { 0.15f, -500.0f },
        { 0.20f,   0.0f },
        { 0.30f, 250.0f },
        { 0.40f,   0.0f },
        { 0.50f, -250.0f },
        { 0.65f,   0.0f },
        { 0.80f, 120.0f },
        { 0.90f,   0.0f },
        { 1.00f,   0.0f }
    };
    const auto smoothedValue = [&] (float position, int smooth)
    {
        return PitchCurveSmoothing::valueAt(
            position, smooth, static_cast<int>(points.size()),
            [&] (int index) { return points[static_cast<size_t>(index)].position; },
            [&] (int index) { return points[static_cast<size_t>(index)].value; });
    };

    constexpr int maximumSmooth = 10;
    float previousDerivativeJump = std::numeric_limits<float>::max();
    float hardDerivativeJump = 0.0f;
    for (int smooth = 0; smooth <= maximumSmooth; ++smooth)
    {
        // Every local peak and valley, including both large rapid features and
        // smaller slow ones, may move only a small fraction at every level.
        for (const int index : { 1, 3, 5, 7, 9 })
        {
            const auto& point = points[static_cast<size_t>(index)];
            const float allowedChange =
                juce::jmax(2.0f, std::abs(point.value) * 0.05f);
            if (std::abs(smoothedValue(point.position, smooth) - point.value)
                > allowedChange)
            {
                std::cerr << "Smoothing changed an authored curve amplitude\n";
                return false;
            }
        }

        float maximum = -std::numeric_limits<float>::infinity();
        float minimum = std::numeric_limits<float>::infinity();
        for (int sample = 0; sample <= 2000; ++sample)
        {
            const float value = smoothedValue(sample / 2000.0f, smooth);
            if (! std::isfinite(value))
            {
                std::cerr << "Smoothing produced a non-finite value\n";
                return false;
            }
            maximum = juce::jmax(maximum, value);
            minimum = juce::jmin(minimum, value);
        }
        if (maximum > 500.001f || minimum < -500.001f)
        {
            std::cerr << "Shape-preserving smoothing must not overshoot\n";
            return false;
        }

        // The large, fast feature must remain larger than every slower,
        // smaller feature; smoothing may not reverse their relationship.
        if (std::abs(smoothedValue(0.05f, smooth))
            <= std::abs(smoothedValue(0.30f, smooth)))
        {
            std::cerr << "Smoothing reversed feature amplitude ordering\n";
            return false;
        }

        constexpr float corner = 0.05f;
        constexpr float epsilon = 0.0001f;
        const float leftDerivative =
            (smoothedValue(corner, smooth)
             - smoothedValue(corner - epsilon, smooth)) / epsilon;
        const float rightDerivative =
            (smoothedValue(corner + epsilon, smooth)
             - smoothedValue(corner, smooth)) / epsilon;
        const float derivativeJump = std::abs(leftDerivative - rightDerivative);
        if (smooth == 0)
            hardDerivativeJump = derivativeJump;
        if (derivativeJump > previousDerivativeJump + 5.0f)
        {
            std::cerr << "Each Smooth level must round corners progressively\n";
            return false;
        }
        previousDerivativeJump = derivativeJump;
    }

    if (previousDerivativeJump > hardDerivativeJump * 0.9f)
    {
        std::cerr << "Smooth 10 must substantially round hard corners\n";
        return false;
    }

    // Match the editor's 256-segment grid and add alternating low-level jitter.
    // The jitter must not turn every sample into an anchor and make all Smooth
    // settings look identical.
    std::vector<Point> densePoints;
    densePoints.reserve(257);
    constexpr float pi = 3.14159265358979323846f;
    for (int i = 0; i <= 256; ++i)
    {
        const float position = static_cast<float>(i) / 256.0f;
        float value = 0.0f;
        if (position < 0.5f)
            value = 500.0f * (2.0f / pi)
                  * std::asin(std::sin(8.0f * pi * position));
        else
            value = 200.0f * (2.0f / pi)
                  * std::asin(std::sin(4.0f * pi * (position - 0.5f)));
        if (i != 0 && i != 256)
            value += (i % 2 == 0 ? 3.0f : -3.0f);
        densePoints.push_back({ position, value });
    }
    densePoints.back().value = densePoints.front().value;
    const auto denseValue = [&] (float position, int smooth)
    {
        return PitchCurveSmoothing::valueAt(
            position, smooth, static_cast<int>(densePoints.size()),
            [&] (int index)
            {
                return densePoints[static_cast<size_t>(index)].position;
            },
            [&] (int index)
            {
                return densePoints[static_cast<size_t>(index)].value;
            });
    };

    float totalVisibleChange = 0.0f;
    float previousVisibleChange = 0.0f;
    for (int smooth = 1; smooth <= 10; ++smooth)
    {
        totalVisibleChange = 0.0f;
        for (int i = 0; i < 256; ++i)
        {
            const float position = densePoints[static_cast<size_t>(i)].position;
            totalVisibleChange +=
                std::abs(denseValue(position, smooth) - denseValue(position, 0));
        }
        if (totalVisibleChange <= previousVisibleChange + 20.0f)
        {
            std::cerr << "Smooth levels must produce visible progressive changes\n";
            return false;
        }
        previousVisibleChange = totalVisibleChange;
    }
    if (totalVisibleChange < 1000.0f
        || std::abs(denseValue(15.0f / 256.0f, 1)
                    - denseValue(15.0f / 256.0f, 0)) < 2.0f)
    {
        std::cerr << "Dense editor points made low Smooth levels ineffective\n";
        return false;
    }

    float largeAmplitude = 0.0f;
    float smallAmplitude = 0.0f;
    for (int i = 0; i < 256; ++i)
    {
        const float position = densePoints[static_cast<size_t>(i)].position;
        const float magnitude = std::abs(denseValue(position, 10));
        if (position < 0.5f)
            largeAmplitude = juce::jmax(largeAmplitude, magnitude);
        else
            smallAmplitude = juce::jmax(smallAmplitude, magnitude);
    }
    if (largeAmplitude < 475.0f || smallAmplitude < 185.0f
        || largeAmplitude <= smallAmplitude)
    {
        std::cerr << "Dense smoothing failed to preserve feature amplitudes\n";
        return false;
    }

    auto editedPoints = densePoints;
    editedPoints[100].value += 80.0f;
    const auto editedValue = [&] (float position)
    {
        return PitchCurveSmoothing::valueAt(
            position, 10, static_cast<int>(editedPoints.size()),
            [&] (int index)
            {
                return editedPoints[static_cast<size_t>(index)].position;
            },
            [&] (int index)
            {
                return editedPoints[static_cast<size_t>(index)].value;
            });
    };
    bool changedLocally = false;
    for (int i = 0; i < 256; ++i)
    {
        const int directDistance = std::abs(i - 100);
        const int circularDistance = juce::jmin(directDistance, 256 - directDistance);
        const float position = densePoints[static_cast<size_t>(i)].position;
        const float difference =
            std::abs(editedValue(position) - denseValue(position, 10));
        if (circularDistance <= 7)
            changedLocally = changedLocally || difference > 0.01f;
        else if (difference > 1.0e-4f)
        {
            std::cerr << "A local edit changed the curve outside its fixed window\n";
            return false;
        }
    }
    if (! changedLocally)
    {
        std::cerr << "A local edit did not affect its own neighbourhood\n";
        return false;
    }
    return true;
}

bool testConcurrentCurvePublication()
{
    ContourAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);
    std::atomic<bool> failed { false };
    std::thread audioThread([&]
    {
        juce::MidiBuffer midi;
        for (int blockIndex = 0; blockIndex < 1000; ++blockIndex)
        {
            juce::AudioBuffer<float> block(2, 64);
            block.clear();
            processor.processBlock(block, midi);
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                    if (! std::isfinite(block.getSample(channel, sample)))
                        failed.store(true);
        }
    });

    for (int revision = 0; revision < 1000; ++revision)
    {
        std::vector<PitchPoint> points;
        const int pointCount = 2 + revision % 63;
        points.reserve(static_cast<size_t>(pointCount));
        for (int i = 0; i < pointCount; ++i)
            points.push_back({
                static_cast<float>(i) / static_cast<float>(pointCount - 1),
                static_cast<float>((revision + i) % 1200 - 600),
                1.0f
            });
        processor.setContour(std::move(points), 0.5f + 0.01f * (revision % 100));
    }

    audioThread.join();
    if (failed.load())
    {
        std::cerr << "Concurrent curve publication produced invalid audio\n";
        return false;
    }
    return true;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    if (! testLatencyMatchedBypass()
        || ! testContourWithOversizedBlocks()
        || ! testStateRoundTripAndBounds()
        || ! testDefaultsAndDurationLimit()
        || ! testStoppedTransportResetsDisplay()
        || ! testSmoothScaleEndpoints()
        || ! testConcurrentCurvePublication())
        return 1;

    std::cout << "Processor safety tests passed\n";
    return 0;
}
