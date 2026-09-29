#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CurveSmoothing.h"

#include <atomic>
#include <cmath>
#include <iostream>
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
    const auto linearValueAt = [] (float position)
    {
        return position * 100.0f;
    };
    const float position = 0.49f;
    const float hardValue = PitchCurveSmoothing::valueAt(
        position, 0, linearValueAt);
    if (std::abs(hardValue - linearValueAt(position)) > 1.0e-6f)
    {
        std::cerr << "Smooth 0 must preserve the continuous source curve\n";
        return false;
    }

    constexpr int maximumSmooth = 10;
    const auto flatLinear = [] (float) { return 0.0f; };
    const auto corner = [] (float position)
    {
        return std::abs(position - 0.5f) * 100.0f;
    };
    float previousCornerValue = PitchCurveSmoothing::valueAt(
        0.5f, 0, corner);
    for (int smooth = 1; smooth <= maximumSmooth; ++smooth)
    {
        if (std::abs(PitchCurveSmoothing::valueAt(
                0.25f, smooth, flatLinear)) > 1.0e-6f)
        {
            std::cerr << "Smoothing must preserve a flat curve\n";
            return false;
        }
        const float cornerValue = PitchCurveSmoothing::valueAt(
            0.5f, smooth, corner);
        if (! std::isfinite(cornerValue)
            || cornerValue - previousCornerValue < 0.3f)
        {
            std::cerr << "Each smooth level must produce a clearly stronger result\n";
            return false;
        }
        previousCornerValue = cornerValue;
    }

    const auto denseRipple = [] (float position)
    {
        return 100.0f * std::sin(6.28318530718f * 40.0f * position);
    };
    constexpr float ripplePosition = 0.003125f;
    float previousRipple = std::abs(PitchCurveSmoothing::valueAt(
        ripplePosition, 0, denseRipple));
    for (int smooth = 1; smooth <= maximumSmooth; ++smooth)
    {
        const float ripple = std::abs(PitchCurveSmoothing::valueAt(
            ripplePosition, smooth, denseRipple));
        if (! std::isfinite(ripple) || ripple > previousRipple + 0.01f)
        {
            std::cerr << "Smooth levels must monotonically suppress dense ripple\n";
            return false;
        }
        previousRipple = ripple;
    }
    if (previousRipple > 0.1f)
    {
        std::cerr << "Smooth 10 must remove fine, dense zig-zag movement\n";
        return false;
    }
    return true;
}

bool testMinimumChangeDrawingProjection()
{
    constexpr int pointCount = 255;
    const auto valueFromRing = [] (const std::vector<float>& values, float position)
    {
        position -= std::floor(position);
        const float scaled = position * static_cast<float>(values.size());
        const auto lower = static_cast<size_t>(std::floor(scaled)) % values.size();
        const auto upper = (lower + 1) % values.size();
        const float fraction = scaled - std::floor(scaled);
        return values[lower] * (1.0f - fraction) + values[upper] * fraction;
    };

    for (const int smooth : { 0, 1, 5, 10 })
    {
        for (const float position : { 0.5f, 0.995f })
        {
            const auto influence = PitchCurveSmoothing::discreteInfluence(
                position, smooth, pointCount);
            float weightSum = 0.0f;
            float energy = 0.0f;
            for (const float weight : influence)
            {
                if (! std::isfinite(weight) || weight < 0.0f)
                {
                    std::cerr << "Drawing influence contains an invalid weight\n";
                    return false;
                }
                weightSum += weight;
                energy += weight * weight;
            }
            if (std::abs(weightSum - 1.0f) > 1.0e-5f || energy <= 0.0f)
            {
                std::cerr << "Drawing influence is not normalized\n";
                return false;
            }

            std::vector<float> projected(static_cast<size_t>(pointCount), 0.0f);
            constexpr float target = 100.0f;
            for (int i = 0; i < pointCount; ++i)
                projected[static_cast<size_t>(i)] =
                    target * influence[static_cast<size_t>(i)] / energy;

            const float visible = PitchCurveSmoothing::valueAt(
                position, smooth,
                [&] (float samplePosition)
                {
                    return valueFromRing(projected, samplePosition);
                });
            if (std::abs(visible - target) > 0.02f)
            {
                std::cerr << "Minimum-change drawing did not reach the pointer\n";
                return false;
            }
        }
    }

    // Compared with shifting the complete filter support equally, the
    // minimum-energy projection must use less total raw movement and must not
    // carry a large plateau to the edge of the old brush.
    constexpr int smooth = 10;
    constexpr float position = 0.5f;
    constexpr float target = 100.0f;
    const auto influence = PitchCurveSmoothing::discreteInfluence(
        position, smooth, pointCount);
    float energy = 0.0f;
    for (const float weight : influence)
        energy += weight * weight;

    std::vector<float> projected(static_cast<size_t>(pointCount), 0.0f);
    float projectedMovement = 0.0f;
    for (int i = 0; i < pointCount; ++i)
    {
        auto& value = projected[static_cast<size_t>(i)];
        value = target * influence[static_cast<size_t>(i)] / energy;
        projectedMovement += value * value;
    }

    std::vector<float> oldBrush(static_cast<size_t>(pointCount), 0.0f);
    const int centre = static_cast<int>(position * pointCount);
    const int oldRadius = static_cast<int>(std::ceil(
        PitchCurveSmoothing::supportRadius(smooth) * pointCount)) + 2;
    float oldMovement = 0.0f;
    for (int offset = -oldRadius; offset <= oldRadius; ++offset)
    {
        const int index = (centre + offset + pointCount) % pointCount;
        oldBrush[static_cast<size_t>(index)] = target;
        oldMovement += target * target;
    }
    if (projectedMovement >= oldMovement * 0.7f)
    {
        std::cerr << "Drawing projection still disturbs too much source curve\n";
        return false;
    }

    const float probe = position + PitchCurveSmoothing::supportRadius(smooth);
    const auto visibleAt = [&] (const std::vector<float>& values)
    {
        return PitchCurveSmoothing::valueAt(
            probe, smooth,
            [&] (float samplePosition)
            {
                return valueFromRing(values, samplePosition);
            });
    };
    if (std::abs(visibleAt(projected)) >= std::abs(visibleAt(oldBrush)) * 0.25f)
    {
        std::cerr << "Drawing projection still creates a wide visible plateau\n";
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
        || ! testMinimumChangeDrawingProjection()
        || ! testConcurrentCurvePublication())
        return 1;

    std::cout << "Processor safety tests passed\n";
    return 0;
}
