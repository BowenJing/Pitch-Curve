#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <array>
#include <mutex>

class ContourAudioProcessor final : public juce::AudioProcessor
{
public:
    ContourAudioProcessor();
    ~ContourAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    static constexpr size_t maximumCurvePoints = 4096;

    struct CurveData
    {
        std::array<PitchPoint, maximumCurvePoints> points {};
        size_t pointCount = 0;
        float durationSeconds = 2.0f;
    };

    float curveValueAt(float position, const CurveData&) const;
    float smoothedCurveValueAt(float position, const CurveData&, int smooth) const;
    void processBlockInternal(juce::AudioBuffer<float>&, bool forceBypass);

    juce::AudioProcessorValueTreeState state;
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    juce::AudioBuffer<float> processed;
    juce::dsp::DelayLine<float> bypassDelay { 65536 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> effectMix;
    double currentSampleRate = 44100.0;
    int64_t freeRunningSample = 0;
    int64_t hostPlaybackSample = 0;
    bool hostWasPlaying = false;

    std::array<CurveData, 3> curveBuffers {};
    std::atomic<int> publishedCurveIndex { 0 };
    std::atomic<int> audioReadingCurveIndex { -1 };
    mutable std::mutex curveWriterMutex;
    std::atomic<uint64_t> contourRevision { 0 };
    std::atomic<float> displayPosition { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContourAudioProcessor)
};
