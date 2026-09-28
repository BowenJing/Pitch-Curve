#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ContourLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ContourLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                          float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
};

class NumericTextEditor final : public juce::TextEditor
{
public:
    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseDown(event);
        selectAll();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::TextEditor::mouseUp(event);
        selectAll();
    }

    void focusLost(FocusChangeType cause) override
    {
        juce::TextEditor::focusLost(cause);
        setHighlightedRegion({ 0, 0 });
    }
};

class DurationLockButton final : public juce::Button
{
public:
    DurationLockButton();
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

class CurveEditor final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CurveEditor(ContourAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void ensureUniformEditablePoints();
    void drawAt(juce::Point<float>);
    juce::Rectangle<float> graphBounds() const;
    float centsFromY(float) const;
    float yFromCents(float) const;
    float displayRangeSemitones() const;
    float linearCentsAt(float) const;
    float displayCentsAt(float) const;

    ContourAudioProcessor& processor;
    std::vector<PitchPoint> editablePoints;
    std::optional<juce::Point<float>> previousDrawPosition;
    uint64_t observedRevision = 0;
    bool editablePointsAreUniform = false;
    bool playheadWasRunning = false;
    double playheadLastUpdateSeconds = 0.0;
    float animatedPlayheadPosition = 0.0f;
    int observedSmooth = -1;
    float observedAmount = -1.0f;
};

class AudioWaveformView final : public juce::Component,
                                private juce::ChangeListener
{
public:
    AudioWaveformView();
    bool setFile(const juce::File&);
    void clear();
    void paint(juce::Graphics&) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail thumbnail { 256, formatManager, thumbnailCache };
};

class ContourAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::FileDragAndDropTarget,
                                          private juce::Thread,
                                          private juce::Timer
{
public:
    explicit ContourAudioProcessorEditor(ContourAudioProcessor&);
    ~ContourAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void run() override;
    void timerCallback() override;
    void chooseFile();
    void beginLearning();
    void setSelectedFile(const juce::File&);
    void applyDurationTimecode();
    void updateDurationTimecode();
    void updateDurationLockControls();
    void applyAmountText();
    void updateAmountText();
    void applySmoothText();
    void updateSmoothText();

    ContourAudioProcessor& processor;
    ContourLookAndFeel lookAndFeel;
    CurveEditor curveEditor;
    AudioWaveformView waveform;
    juce::TextButton fileButton { "DROP AUDIO OR BROWSE" };
    juce::TextButton learnButton { "LEARN CURVE" };
    juce::TextButton clearButton { "CLEAR CURVE" };
    juce::Label title;
    juce::Label fileName;
    juce::Label status;
    juce::Label secondsLabel;
    juce::Label framesLabel;
    juce::Label amountLabel;
    juce::Label smoothLabel;
    NumericTextEditor secondsEditor;
    NumericTextEditor framesEditor;
    NumericTextEditor amountEditor;
    NumericTextEditor smoothEditor;
    juce::Slider timeKnob;
    DurationLockButton durationLock;
    juce::Slider amount;
    juce::Slider smooth;
    juce::File selectedFile;
    juce::File analysisFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothAttachment;
    std::atomic<bool> analysing { false };
    uint64_t observedProcessorRevision = 0;
    bool secondsDirty = false;
    bool framesDirty = false;
    bool amountDirty = false;
    bool smoothDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContourAudioProcessorEditor)
};
