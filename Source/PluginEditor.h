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
};

class CurveEditor final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CurveEditor(ContourAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void drawAt(juce::Point<float>);
    juce::Rectangle<float> graphBounds() const;
    float centsFromY(float) const;
    float yFromCents(float) const;

    ContourAudioProcessor& processor;
    std::vector<PitchPoint> editablePoints;
    bool isDrawing = false;
};

class ContourAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          public juce::FileDragAndDropTarget,
                                          private juce::Thread
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
    void chooseFile();
    void beginLearning();
    void setSelectedFile(const juce::File&);

    ContourAudioProcessor& processor;
    ContourLookAndFeel lookAndFeel;
    CurveEditor curveEditor;
    juce::TextButton fileButton { "DROP AUDIO OR BROWSE" };
    juce::TextButton learnButton { "LEARN CONTOUR" };
    juce::Label title;
    juce::Label subtitle;
    juce::Label fileName;
    juce::Label status;
    juce::Label amountLabel;
    juce::Slider amount;
    juce::File selectedFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::atomic<bool> analysing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContourAudioProcessorEditor)
};
