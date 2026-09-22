#pragma once

#include <JuceHeader.h>

struct PitchPoint
{
    float position = 0.0f;
    float cents = 0.0f;
    float confidence = 0.0f;
};

struct PitchAnalysis
{
    std::vector<PitchPoint> points;
    float referenceHz = 0.0f;
    float durationSeconds = 0.0f;
};

class PitchDetector
{
public:
    static PitchAnalysis analyse(const juce::AudioBuffer<float>& audio,
                                 double sampleRate,
                                 float minimumHz = 55.0f,
                                 float maximumHz = 1600.0f);

private:
    static std::pair<float, float> detectFrame(const float* samples,
                                                int frameSize,
                                                double sampleRate,
                                                float minimumHz,
                                                float maximumHz);
};
