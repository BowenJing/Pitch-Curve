#include <JuceHeader.h>
#include "PitchDetector.h"

#include <cmath>
#include <iostream>

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr float duration = 2.0f;
    juce::AudioBuffer<float> audio(1, static_cast<int>(sampleRate * duration));
    double phase = 0.0;

    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const double time = sample / sampleRate;
        const double cents = 35.0 * std::sin(juce::MathConstants<double>::twoPi * 5.0 * time);
        const double frequency = 220.0 * std::pow(2.0, cents / 1200.0);
        phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
        audio.setSample(0, sample, static_cast<float>(0.7 * std::sin(phase)));
    }

    const auto result = PitchDetector::analyse(audio, sampleRate);
    if (result.points.size() < 100)
    {
        std::cerr << "Expected a continuous contour, got " << result.points.size() << " points\n";
        return 1;
    }
    if (std::abs(result.referenceHz - 220.0f) > 3.0f)
    {
        std::cerr << "Reference pitch was " << result.referenceHz << " Hz\n";
        return 1;
    }

    float minimum = 1000.0f;
    float maximum = -1000.0f;
    for (const auto& point : result.points)
    {
        minimum = juce::jmin(minimum, point.cents);
        maximum = juce::jmax(maximum, point.cents);
    }
    if (maximum < 20.0f || minimum > -20.0f)
    {
        std::cerr << "Vibrato range was not recovered: " << minimum << " to " << maximum << " cents\n";
        return 1;
    }

    juce::AudioBuffer<float> silence(1, 4096);
    silence.clear();
    if (! PitchDetector::analyse(silence, sampleRate).points.empty())
    {
        std::cerr << "Silence should not produce pitch points\n";
        return 1;
    }

    std::cout << "Pitch detector tests passed\n";
    return 0;
}
