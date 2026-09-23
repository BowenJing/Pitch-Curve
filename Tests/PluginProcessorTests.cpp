#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
bool testLatencyMatchedBypass()
{
    ContourAudioProcessor processor;
    constexpr double sampleRate = 48000.0;
    constexpr int preparedBlockSize = 32;
    constexpr int hostBlockSize = 257;
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
            std::cerr << "Bypass latency mismatch at sample " << sample
                      << ", expected " << expected << " and got "
                      << output[static_cast<size_t>(sample)] << '\n';
            return false;
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
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    if (! testLatencyMatchedBypass() || ! testContourWithOversizedBlocks())
        return 1;

    std::cout << "Processor safety tests passed\n";
    return 0;
}
