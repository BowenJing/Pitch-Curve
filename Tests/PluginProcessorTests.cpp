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
    source.setContourAutoFit(true);

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
        || std::abs(restored.parameters().getRawParameterValue("smooth")->load() - 10.0f)
               > 1.0e-6f
        || ! restored.isContourAutoFit())
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
    if (std::abs(processor.parameters().getRawParameterValue("smooth")->load() - 5.0f)
        > 1.0e-6f)
    {
        std::cerr << "Smooth must default to 5\n";
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
    const float firstStep = PitchCurveSmoothing::quantiseStepPosition(0.01f);
    const float sameStep = PitchCurveSmoothing::quantiseStepPosition(0.04f);
    const float nextStep = PitchCurveSmoothing::quantiseStepPosition(0.05f);
    if (std::abs(firstStep - sameStep) > 1.0e-6f
        || std::abs(nextStep - 1.0f / PitchCurveSmoothing::stepIntervals) > 1.0e-6f)
    {
        std::cerr << "Smooth 0 steps must preserve the minimum segment length\n";
        return false;
    }

    const auto linearValueAt = [] (float position)
    {
        return position * 100.0f;
    };
    const auto steppedValueAt = [] (float position)
    {
        return position < 0.5f ? -100.0f : 100.0f;
    };

    const float position = 0.49f;
    const float hardValue = PitchCurveSmoothing::valueAt(
        position, 0, linearValueAt, steppedValueAt);
    if (std::abs(hardValue + 100.0f) > 1.0e-6f)
    {
        std::cerr << "Smooth 0 must use the stepped curve exactly\n";
        return false;
    }

    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    constexpr int maximumSmooth = 10;
    constexpr float radius = 0.004f * maximumSmooth;
    for (int offset = -maximumSmooth; offset <= maximumSmooth; ++offset)
    {
        float wrapped = position
                      + static_cast<float>(offset) / maximumSmooth * radius;
        wrapped -= std::floor(wrapped);
        const float weight =
            static_cast<float>(maximumSmooth + 1 - std::abs(offset));
        weightedValue += linearValueAt(wrapped) * weight;
        totalWeight += weight;
    }
    const float expectedMaximum = weightedValue / totalWeight;
    const float maximumValue = PitchCurveSmoothing::valueAt(
        position, maximumSmooth, linearValueAt, steppedValueAt);
    if (std::abs(maximumValue - expectedMaximum) > 1.0e-6f)
    {
        std::cerr << "Smooth 10 must preserve the established rounded result\n";
        return false;
    }

    for (int smooth = 1; smooth < maximumSmooth; ++smooth)
    {
        const float value = PitchCurveSmoothing::valueAt(
            position, smooth, linearValueAt, steppedValueAt);
        if (! std::isfinite(value))
        {
            std::cerr << "Intermediate smooth level produced a non-finite value\n";
            return false;
        }
    }

    const auto flatLinear = [] (float) { return 0.0f; };
    const auto jaggedStep = [] (float) { return 100.0f; };
    for (const int smooth : { 5, 9 })
    {
        const float value = PitchCurveSmoothing::valueAt(
            0.25f, smooth, flatLinear, jaggedStep);
        if (std::abs(value) > 1.0e-6f)
        {
            std::cerr << "Medium and high Smooth must not retain staircase ripple\n";
            return false;
        }
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
