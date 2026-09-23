#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

class ContourAudioProcessor final : public juce::AudioProcessor
{
public:
    ContourAudioProcessor();
    ~ContourAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void setContour(std::vector<PitchPoint> points, float durationSeconds);
    std::vector<PitchPoint> getContour() const;
    float getContourDuration() const;
    uint64_t getContourRevision() const noexcept { return contourRevision.load(); }
    float getPlayheadPosition() const noexcept { return displayPosition.load(); }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return state; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct CurveData
    {
        std::vector<PitchPoint> points;
        float durationSeconds = 2.0f;
    };

    float curveValueAt(float position, const std::vector<PitchPoint>& points) const;

    juce::AudioProcessorValueTreeState state;
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    juce::AudioBuffer<float> processed;
    juce::dsp::DelayLine<float> bypassDelay { 65536 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchSmoother;
    double currentSampleRate = 44100.0;
    int64_t freeRunningSample = 0;

    std::shared_ptr<const CurveData> curveData = std::make_shared<const CurveData>();
    std::atomic<uint64_t> contourRevision { 0 };
    std::atomic<float> displayPosition { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContourAudioProcessor)
};
