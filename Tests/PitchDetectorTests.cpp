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

    juce::AudioBuffer<float> antiPhase(2, audio.getNumSamples());
    antiPhase.copyFrom(0, 0, audio, 0, 0, audio.getNumSamples());
    antiPhase.copyFrom(1, 0, audio.getReadPointer(0), audio.getNumSamples(), -1.0f);
    const auto antiPhaseResult = PitchDetector::analyse(antiPhase, sampleRate);
    if (antiPhaseResult.points.size() < 100
        || std::abs(antiPhaseResult.referenceHz - 220.0f) > 3.0f)
    {
        std::cerr << "Anti-phase stereo pitch analysis failed\n";
        return 1;
    }

    constexpr double highSampleRate = 192000.0;
    juce::AudioBuffer<float> highRateAudio(1, static_cast<int>(highSampleRate));
    phase = 0.0;
    for (int sample = 0; sample < highRateAudio.getNumSamples(); ++sample)
    {
        const double time = sample / highSampleRate;
        const double cents = 25.0 * std::sin(juce::MathConstants<double>::twoPi * 4.0 * time);
        const double frequency = 110.0 * std::pow(2.0, cents / 1200.0);
        phase += juce::MathConstants<double>::twoPi * frequency / highSampleRate;
        highRateAudio.setSample(0, sample, static_cast<float>(0.7 * std::sin(phase)));
    }
    const auto highRateResult = PitchDetector::analyse(highRateAudio, highSampleRate);
    if (highRateResult.points.size() < 50
        || std::abs(highRateResult.referenceHz - 110.0f) > 3.0f)
    {
        std::cerr << "High-rate low pitch analysis failed: "
                  << highRateResult.referenceHz << " Hz, "
                  << highRateResult.points.size() << " points\n";
        return 1;
    }

    for (const double ultrasonicSampleRate :
         { 96000.0, 192000.0, 384000.0, 768000.0 })
    {
        juce::AudioBuffer<float> ultrasonic(
            1, static_cast<int>(ultrasonicSampleRate));
        phase = 0.0;
        for (int sample = 0; sample < ultrasonic.getNumSamples(); ++sample)
        {
            phase += juce::MathConstants<double>::twoPi * 47000.0
                   / ultrasonicSampleRate;
            ultrasonic.setSample(0, sample,
                                 static_cast<float>(0.9 * std::sin(phase)));
        }
        if (! PitchDetector::analyse(ultrasonic, ultrasonicSampleRate).points.empty())
        {
            std::cerr << "Ultrasonic content at " << ultrasonicSampleRate
                      << " Hz must not alias into a learned pitch\n";
            return 1;
        }
    }

    juce::AudioBuffer<float> silence(1, 4096);
    silence.clear();
    if (! PitchDetector::analyse(silence, sampleRate).points.empty())
    {
        std::cerr << "Silence should not produce pitch points\n";
        return 1;
    }

    if (! PitchDetector::analyse(audio, 0.0).points.empty())
    {
        std::cerr << "Invalid sample rates must be rejected\n";
        return 1;
    }

    if (! PitchDetector::analyse(audio, sampleRate, 20000.0f, 30000.0f).points.empty())
    {
        std::cerr << "An impossible lag range must be rejected\n";
        return 1;
    }

    bool cancellationChecked = false;
    const auto cancelled = PitchDetector::analyse(
        audio, sampleRate, 55.0f, 1600.0f, [&cancellationChecked]
        {
            cancellationChecked = true;
            return true;
        });
    if (! cancellationChecked || ! cancelled.points.empty())
    {
        std::cerr << "Cancelled analysis must stop without a partial result\n";
        return 1;
    }

    std::cout << "Pitch detector tests passed\n";
    return 0;
}
