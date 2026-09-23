#include "PluginEditor.h"

namespace Palette
{
const auto background = juce::Colour::fromRGB(27, 32, 38);
const auto panel = juce::Colour::fromRGB(37, 44, 52);
const auto panelLight = juce::Colour::fromRGB(50, 59, 69);
const auto text = juce::Colour::fromRGB(234, 238, 242);
const auto muted = juce::Colour::fromRGB(161, 171, 182);
const auto accent = juce::Colour::fromRGB(126, 211, 177);
}

ContourLookAndFeel::ContourLookAndFeel()
{
    setColour(juce::Label::textColourId, Palette::text);
    setColour(juce::TextButton::textColourOffId, Palette::text);
    setColour(juce::Slider::textBoxTextColourId, Palette::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void ContourLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float position, float startAngle, float endAngle,
                                          juce::Slider&)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(width), static_cast<float>(height))
                      .reduced(9.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float line = 5.0f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(Palette::panelLight);
    g.strokePath(track, juce::PathStrokeType(line, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                        startAngle, juce::jmap(position, startAngle, endAngle), true);
    g.setColour(Palette::accent);
    g.strokePath(value, juce::PathStrokeType(line, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    const float angle = juce::jmap(position, startAngle, endAngle);
    const auto dot = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * radius;
    g.setColour(Palette::text);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(dot));
}

void ContourLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour&, bool highlighted, bool down)
{
    auto colour = button.getComponentID() == "primary" ? Palette::accent : Palette::panelLight;
    if (highlighted)
        colour = colour.brighter(0.08f);
    if (down)
        colour = colour.darker(0.12f);
    g.setColour(colour.withAlpha(button.isEnabled() ? 1.0f : 0.35f));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat(), 8.0f);
}

juce::Font ContourLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return juce::Font(juce::FontOptions(12.0f, juce::Font::bold));
}

CurveEditor::CurveEditor(ContourAudioProcessor& owner) : processor(owner)
{
    editablePoints = processor.getContour();
    observedRevision = processor.getContourRevision();
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(30);
}

juce::Rectangle<float> CurveEditor::graphBounds() const
{
    return getLocalBounds().toFloat().reduced(18.0f, 22.0f);
}

float CurveEditor::centsFromY(float y) const
{
    const auto bounds = graphBounds();
    return juce::jmap(juce::jlimit(bounds.getY(), bounds.getBottom(), y),
                      bounds.getBottom(), bounds.getY(), -200.0f, 200.0f);
}

float CurveEditor::yFromCents(float cents) const
{
    const auto bounds = graphBounds();
    return juce::jmap(juce::jlimit(-200.0f, 200.0f, cents),
                      -200.0f, 200.0f, bounds.getBottom(), bounds.getY());
}

void CurveEditor::paint(juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(outer, 12.0f);
    const auto bounds = graphBounds();

    g.setColour(Palette::muted.withAlpha(0.16f));
    for (int i = 0; i <= 8; ++i)
    {
        const float x = bounds.getX() + bounds.getWidth() * i / 8.0f;
        g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
    }
    for (int cents : { -200, -100, 0, 100, 200 })
    {
        const float y = yFromCents(static_cast<float>(cents));
        g.setColour(cents == 0 ? Palette::muted.withAlpha(0.45f) : Palette::muted.withAlpha(0.16f));
        g.drawHorizontalLine(juce::roundToInt(y), bounds.getX(), bounds.getRight());
        if (cents != 0)
        {
            g.setColour(Palette::muted.withAlpha(0.7f));
            g.setFont(10.0f);
            g.drawText((cents > 0 ? "+" : "") + juce::String(cents) + "c",
                       juce::Rectangle<float>(bounds.getX() + 5.0f, y - 13.0f, 40.0f, 12.0f),
                       juce::Justification::left);
        }
    }

    if (editablePoints.empty())
    {
        g.setColour(Palette::muted);
        g.setFont(14.0f);
        g.drawFittedText("Learn a performance or draw directly here",
                         bounds.toNearestInt(), juce::Justification::centred, 1);
    }
    else
    {
        juce::Path curve;
        curve.startNewSubPath(bounds.getX() + editablePoints.front().position * bounds.getWidth(),
                              yFromCents(editablePoints.front().cents));
        for (size_t i = 1; i < editablePoints.size(); ++i)
            curve.lineTo(bounds.getX() + editablePoints[i].position * bounds.getWidth(),
                         yFromCents(editablePoints[i].cents));

        juce::Path fill = curve;
        fill.lineTo(bounds.getRight(), yFromCents(0.0f));
        fill.lineTo(bounds.getX(), yFromCents(0.0f));
        fill.closeSubPath();
        juce::ColourGradient gradient(Palette::accent.withAlpha(0.16f),
                                      bounds.getCentreX(), bounds.getY(),
                                      Palette::accent.withAlpha(0.01f),
                                      bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(gradient);
        g.fillPath(fill);
        g.setColour(Palette::accent.withAlpha(0.16f));
        g.strokePath(curve, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(Palette::accent);
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const float playhead = bounds.getX() + processor.getPlayheadPosition() * bounds.getWidth();
    g.setColour(Palette::accent.withAlpha(0.85f));
    g.drawVerticalLine(juce::roundToInt(playhead), bounds.getY(), bounds.getBottom());
}

void CurveEditor::ensureUniformEditablePoints()
{
    if (editablePointsAreUniform)
        return;

    constexpr int pointCount = 256;
    if (editablePoints.empty())
    {
        editablePoints.reserve(pointCount);
        for (int i = 0; i < pointCount; ++i)
            editablePoints.push_back({ static_cast<float>(i) / (pointCount - 1), 0.0f, 1.0f });
        editablePointsAreUniform = true;
        return;
    }

    const auto source = editablePoints;
    std::vector<PitchPoint> uniform;
    uniform.reserve(pointCount);
    for (int i = 0; i < pointCount; ++i)
    {
        const float position = static_cast<float>(i) / (pointCount - 1);
        const auto upper = std::lower_bound(source.begin(), source.end(), position,
            [] (const PitchPoint& point, float value) { return point.position < value; });

        if (upper == source.begin())
        {
            uniform.push_back({ position, upper->cents, upper->confidence });
            continue;
        }
        if (upper == source.end())
        {
            uniform.push_back({ position, source.back().cents, source.back().confidence });
            continue;
        }

        const auto lower = upper - 1;
        const float span = upper->position - lower->position;
        const float proportion = span > 0.0f
            ? (position - lower->position) / span
            : 0.0f;
        uniform.push_back({
            position,
            juce::jmap(proportion, lower->cents, upper->cents),
            juce::jmap(proportion, lower->confidence, upper->confidence)
        });
    }

    editablePoints = std::move(uniform);
    editablePointsAreUniform = true;
}

void CurveEditor::drawAt(juce::Point<float> point)
{
    ensureUniformEditablePoints();
    const auto bounds = graphBounds();
    const float x = juce::jlimit(0.0f, 1.0f, (point.x - bounds.getX()) / bounds.getWidth());
    const float cents = centsFromY(point.y);

    const int index = juce::jlimit(0, static_cast<int>(editablePoints.size()) - 1,
                                   juce::roundToInt(x * (editablePoints.size() - 1)));
    const int radius = 3;
    for (int i = juce::jmax(0, index - radius);
         i <= juce::jmin(static_cast<int>(editablePoints.size()) - 1, index + radius); ++i)
    {
        const float weight = 1.0f - std::abs(i - index) / static_cast<float>(radius + 1);
        editablePoints[static_cast<size_t>(i)].cents =
            juce::jmap(weight, editablePoints[static_cast<size_t>(i)].cents, cents);
    }
}

void CurveEditor::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    drawAt(event.position);
    processor.setContour(editablePoints, processor.getContourDuration());
    previousDrawPosition = event.position;
    repaint();
}

void CurveEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (! previousDrawPosition)
    {
        drawAt(event.position);
        processor.setContour(editablePoints, processor.getContourDuration());
        previousDrawPosition = event.position;
        repaint();
        return;
    }

    const auto delta = event.position - *previousDrawPosition;
    const int steps = juce::jmax(1, juce::roundToInt(delta.getDistanceFromOrigin() / 3.0f));
    for (int step = 1; step <= steps; ++step)
        drawAt(*previousDrawPosition + delta * (static_cast<float>(step) / steps));
    processor.setContour(editablePoints, processor.getContourDuration());
    previousDrawPosition = event.position;
    repaint();
}

void CurveEditor::mouseUp(const juce::MouseEvent&)
{
    previousDrawPosition.reset();
}

void CurveEditor::mouseDoubleClick(const juce::MouseEvent&)
{
    previousDrawPosition.reset();
    editablePoints.clear();
    editablePointsAreUniform = false;
    processor.setContour({}, processor.getContourDuration());
    repaint();
}

void CurveEditor::timerCallback()
{
    if (! isMouseButtonDown())
    {
        const auto revision = processor.getContourRevision();
        if (revision != observedRevision)
        {
            editablePoints = processor.getContour();
            editablePointsAreUniform = false;
            observedRevision = revision;
        }
    }
    repaint();
}

AudioWaveformView::AudioWaveformView()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    setInterceptsMouseClicks(false, false);
}

void AudioWaveformView::setFile(const juce::File& file)
{
    thumbnail.setSource(new juce::FileInputSource(file));
    repaint();
}

void AudioWaveformView::clear()
{
    thumbnail.clear();
    repaint();
}

void AudioWaveformView::paint(juce::Graphics& g)
{
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    const auto bounds = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
    g.setColour(Palette::muted.withAlpha(0.24f));
    g.drawHorizontalLine(juce::roundToInt(bounds.getCentreY()),
                         bounds.getX(), bounds.getRight());
    g.setColour(Palette::accent.withAlpha(0.88f));
    thumbnail.drawChannels(g, bounds.toNearestInt(), 0.0,
                           thumbnail.getTotalLength(), 1.0f);
}

void AudioWaveformView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint();
}

ContourAudioProcessorEditor::ContourAudioProcessorEditor(ContourAudioProcessor& owner)
    : AudioProcessorEditor(owner), juce::Thread("Contour analysis"),
      processor(owner), curveEditor(owner)
{
    setLookAndFeel(&lookAndFeel);
    setResizable(true, true);
    setResizeLimits(720, 500, 1200, 850);
    setSize(900, 620);

    title.setText("PITCHTRANSFORM", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    status.setText("Draw a curve, or learn one from audio", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, Palette::muted);
    status.setFont(juce::Font(juce::FontOptions(12.0f)));
    status.setJustificationType(juce::Justification::centredRight);
    fileName.setText("No Audio File", juce::dontSendNotification);
    fileName.setColour(juce::Label::textColourId, Palette::muted);
    fileName.setJustificationType(juce::Justification::centred);
    fileName.setFont(juce::Font(juce::FontOptions(12.0f)));
    secondsLabel.setText("SECOND", juce::dontSendNotification);
    framesLabel.setText("FRAME", juce::dontSendNotification);
    amountLabel.setText("AMOUNT", juce::dontSendNotification);
    for (auto* label : { &secondsLabel, &framesLabel, &amountLabel })
    {
        label->setColour(juce::Label::textColourId, Palette::muted);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    }

    for (auto* editor : { &secondsEditor, &framesEditor, &amountEditor })
    {
        editor->setJustification(juce::Justification::centred);
        editor->setSelectAllWhenFocused(false);
        editor->setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        editor->setColour(juce::TextEditor::backgroundColourId, Palette::panelLight);
        editor->setColour(juce::TextEditor::textColourId, Palette::text);
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, Palette::accent);
    }
    for (auto* editor : { &secondsEditor, &framesEditor })
    {
        editor->setInputRestrictions(4, "0123456789");
        editor->onReturnKey = [this] { applyDurationTimecode(); };
        editor->onFocusLost = [this] { applyDurationTimecode(); };
    }
    amountEditor.setInputRestrictions(3, "0123456789");
    amountEditor.onReturnKey = [this] { applyAmountText(); };
    amountEditor.onFocusLost = [this] { applyAmountText(); };

    timeKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    timeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    timeKnob.setRange(1.0, 600.0 * 30.0, 1.0);
    timeKnob.setSkewFactorFromMidPoint(300.0);
    timeKnob.onValueChange = [this]
    {
        const int totalFrames = juce::roundToInt(timeKnob.getValue());
        processor.setContour(processor.getContour(),
                             static_cast<float>(totalFrames) / 30.0f);
        updateDurationTimecode();
    };
    updateDurationTimecode();

    fileButton.onClick = [this] { chooseFile(); };
    learnButton.setComponentID("primary");
    learnButton.setColour(juce::TextButton::textColourOffId, Palette::background);
    learnButton.setEnabled(false);
    learnButton.onClick = [this] { beginLearning(); };
    clearButton.onClick = [this]
    {
        processor.setContour({}, processor.getContourDuration());
        status.setText("Curve cleared - draw or learn a new contour",
                       juce::dontSendNotification);
    };
    amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amount.setDoubleClickReturnValue(true, 1.0);
    amount.onValueChange = [this] { updateAmountText(); };
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters(), "amount", amount);
    updateAmountText();

    for (auto* component : std::initializer_list<juce::Component*> {
             &title, &status, &waveform, &fileName, &fileButton, &learnButton,
             &clearButton, &secondsLabel, &framesLabel, &secondsEditor, &framesEditor,
             &amountEditor, &timeKnob, &amountLabel, &amount, &curveEditor })
        addAndMakeVisible(component);
}

ContourAudioProcessorEditor::~ContourAudioProcessorEditor()
{
    signalThreadShouldExit();
    stopThread(10000);
    setLookAndFeel(nullptr);
}

void ContourAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    auto dropArea = juce::Rectangle<float>(24.0f, 92.0f, getWidth() - 48.0f, 110.0f);
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(dropArea, 12.0f);
    g.setColour(Palette::muted.withAlpha(0.35f));
    g.drawRoundedRectangle(dropArea, 12.0f, 1.0f);
}

void ContourAudioProcessorEditor::resized()
{
    const int margin = 24;
    title.setBounds(margin, 27, 280, 34);
    status.setBounds(getWidth() - 330, 27, 306, 28);

    auto drop = juce::Rectangle<int>(margin, 102, getWidth() - margin * 2, 90);
    const auto fileButtonArea = drop.removeFromLeft(220);
    const auto learnButtonArea = drop.removeFromRight(180);
    fileButton.setBounds(fileButtonArea.withSizeKeepingCentre(fileButtonArea.getWidth(), 70));
    learnButton.setBounds(learnButtonArea.withSizeKeepingCentre(learnButtonArea.getWidth(), 70));
    auto preview = drop.reduced(16, 2);
    if (selectedFile.existsAsFile())
    {
        fileName.setBounds(preview.removeFromTop(16));
        waveform.setBounds(preview.reduced(0, 1));
    }
    else
    {
        fileName.setBounds(preview);
        waveform.setBounds({});
    }

    const int controlWidth = 120;
    auto content = juce::Rectangle<int>(margin, 222, getWidth() - margin * 2,
                                        getHeight() - 246);
    auto control = content.removeFromRight(controlWidth);
    curveEditor.setBounds(content.reduced(0, 0).withTrimmedRight(16));

    const int clearHeight = 38;
    clearButton.setBounds(control.removeFromBottom(clearHeight));
    control.removeFromBottom(8);
    auto timeSection = control.removeFromTop(control.getHeight() / 2);
    auto amountSection = control;

    const int fieldWidth = 54;
    const int fieldGap = 8;
    const int fieldHeight = 28;
    const int labelHeight = 18;
    const int knobToFieldGap = 6;
    const int valueBlockHeight = knobToFieldGap + fieldHeight + labelHeight;
    const int ringSize = juce::jlimit(
        54, 90,
        juce::jmin(controlWidth,
                   juce::jmin(timeSection.getHeight() - valueBlockHeight,
                              amountSection.getHeight() - valueBlockHeight)));

    const auto layoutControlGroup = [ringSize, valueBlockHeight, knobToFieldGap,
                                     fieldWidth, fieldHeight, labelHeight]
        (juce::Rectangle<int> section, juce::Slider& knob,
         juce::TextEditor& editor, juce::Label& label)
    {
        const int groupHeight = ringSize + valueBlockHeight;
        const int top = section.getY() + juce::jmax(0, (section.getHeight() - groupHeight) / 2);
        knob.setBounds(section.getCentreX() - ringSize / 2, top, ringSize, ringSize);
        const int fieldY = knob.getBottom() + knobToFieldGap;
        editor.setBounds(section.getCentreX() - fieldWidth / 2, fieldY,
                         fieldWidth, fieldHeight);
        label.setBounds(section.getCentreX() - fieldWidth / 2,
                        editor.getBottom(), fieldWidth, labelHeight);
    };

    const int timeGroupHeight = ringSize + valueBlockHeight;
    const int timeTop = timeSection.getY()
                      + juce::jmax(0, (timeSection.getHeight() - timeGroupHeight) / 2);
    timeKnob.setBounds(timeSection.getCentreX() - ringSize / 2,
                       timeTop, ringSize, ringSize);
    const int fieldsY = timeKnob.getBottom() + knobToFieldGap;
    const int timeFieldsWidth = fieldWidth * 2 + fieldGap;
    const int fieldsX = timeSection.getCentreX() - timeFieldsWidth / 2;
    secondsEditor.setBounds(fieldsX, fieldsY, fieldWidth, fieldHeight);
    framesEditor.setBounds(fieldsX + fieldWidth + fieldGap, fieldsY,
                           fieldWidth, fieldHeight);
    secondsLabel.setBounds(secondsEditor.getX(), secondsEditor.getBottom(),
                           fieldWidth, labelHeight);
    framesLabel.setBounds(framesEditor.getX(), framesEditor.getBottom(),
                          fieldWidth, labelHeight);

    layoutControlGroup(amountSection, amount, amountEditor, amountLabel);
}

bool ContourAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;
    const auto extension = juce::File(files[0]).getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif" || extension == ".aiff"
        || extension == ".flac" || extension == ".mp3" || extension == ".ogg";
}

void ContourAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag(files))
        setSelectedFile(juce::File(files[0]));
}

void ContourAudioProcessorEditor::applyDurationTimecode()
{
    constexpr int framesPerSecond = 30;
    constexpr int maximumTotalFrames = 600 * framesPerSecond;
    const int64_t enteredSeconds = juce::jmax<int64_t>(0, secondsEditor.getText().getIntValue());
    const int64_t enteredFrames = juce::jmax<int64_t>(0, framesEditor.getText().getIntValue());
    const int totalFrames = juce::jlimit(
        1, maximumTotalFrames,
        static_cast<int>(juce::jmin<int64_t>(
            maximumTotalFrames, enteredSeconds * framesPerSecond + enteredFrames)));

    const float duration = static_cast<float>(totalFrames)
                         / static_cast<float>(framesPerSecond);
    processor.setContour(processor.getContour(), duration);
    timeKnob.setValue(totalFrames, juce::dontSendNotification);
    updateDurationTimecode();
    status.setText("Curve duration: " + juce::String(totalFrames / framesPerSecond)
                       + " second " + juce::String(totalFrames % framesPerSecond)
                       + " frame",
                   juce::dontSendNotification);
}

void ContourAudioProcessorEditor::updateDurationTimecode()
{
    constexpr int framesPerSecond = 30;
    constexpr int maximumTotalFrames = 600 * framesPerSecond;
    const int totalFrames = juce::jlimit(
        1, maximumTotalFrames,
        juce::roundToInt(processor.getContourDuration() * framesPerSecond));
    const auto secondsText = juce::String(totalFrames / framesPerSecond);
    const auto framesText = juce::String(totalFrames % framesPerSecond);
    if (secondsEditor.getText() != secondsText)
        secondsEditor.setText(secondsText, false);
    if (framesEditor.getText() != framesText)
        framesEditor.setText(framesText, false);
    timeKnob.setValue(totalFrames, juce::dontSendNotification);
}

void ContourAudioProcessorEditor::applyAmountText()
{
    const int percentage = juce::jlimit(0, 200, amountEditor.getText().getIntValue());
    amount.setValue(static_cast<double>(percentage) / 100.0,
                    juce::sendNotificationSync);
    updateAmountText();
}

void ContourAudioProcessorEditor::updateAmountText()
{
    const auto text = juce::String(juce::roundToInt(amount.getValue() * 100.0));
    if (amountEditor.getText() != text)
        amountEditor.setText(text, false);
}

void ContourAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Choose a performance to learn", juce::File {},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    const auto browserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(browserFlags,
        [safe = juce::Component::SafePointer<ContourAudioProcessorEditor>(this)]
        (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile())
            safe->setSelectedFile(fc.getResult());
    });
}

void ContourAudioProcessorEditor::setSelectedFile(const juce::File& file)
{
    selectedFile = file;
    fileName.setText(file.getFileName(), juce::dontSendNotification);
    waveform.setFile(file);
    status.setText("Ready to analyse", juce::dontSendNotification);
    learnButton.setEnabled(true);
    resized();
}

void ContourAudioProcessorEditor::beginLearning()
{
    if (! selectedFile.existsAsFile() || analysing.exchange(true))
        return;
    analysisFile = selectedFile;
    status.setText("Listening for pitch movement...", juce::dontSendNotification);
    learnButton.setEnabled(false);
    fileButton.setEnabled(false);
    startThread();
}

void ContourAudioProcessorEditor::run()
{
    PitchAnalysis analysis;
    try
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(analysisFile));

        if (reader != nullptr && ! threadShouldExit()
            && std::isfinite(reader->sampleRate)
            && reader->sampleRate >= 8000.0 && reader->sampleRate <= 768000.0
            && reader->lengthInSamples > 0 && reader->numChannels > 0)
        {
            constexpr double maximumLengthSeconds = 60.0;
            constexpr int64_t maximumDecodedSamples = 12000000;
            constexpr int decodeChunkSamples = 65536;
            const auto sampleCount = static_cast<int>(juce::jmin<int64_t>(
                reader->lengthInSamples,
                juce::jmin<int64_t>(maximumDecodedSamples,
                    static_cast<int64_t>(reader->sampleRate * maximumLengthSeconds))));
            juce::AudioBuffer<float> audio(
                juce::jlimit(1, 2, static_cast<int>(reader->numChannels)), sampleCount);
            bool readSucceeded = true;
            for (int offset = 0; offset < sampleCount && readSucceeded; offset += decodeChunkSamples)
            {
                if (threadShouldExit())
                    return;
                const int chunk = juce::jmin(decodeChunkSamples, sampleCount - offset);
                readSucceeded = reader->read(&audio, offset, chunk, offset, true, true);
            }
            if (readSucceeded && ! threadShouldExit())
                analysis = PitchDetector::analyse(audio, reader->sampleRate, 55.0f, 1600.0f,
                                                   [this] { return threadShouldExit(); });
        }
    }
    catch (...)
    {
        analysis = {};
    }

    if (threadShouldExit())
        return;

    auto safe = juce::Component::SafePointer<ContourAudioProcessorEditor>(this);
    juce::MessageManager::callAsync([safe, result = std::move(analysis)] () mutable
    {
        if (safe == nullptr)
            return;
        safe->analysing.store(false);
        safe->learnButton.setEnabled(true);
        safe->fileButton.setEnabled(true);
        if (result.points.empty())
        {
            safe->status.setText("No stable pitch found - try a monophonic source",
                                 juce::dontSendNotification);
            return;
        }
        safe->processor.setContour(std::move(result.points), result.durationSeconds);
        safe->updateDurationTimecode();
        safe->status.setText(juce::String(result.referenceHz, 1) + " Hz reference | "
                                 + juce::String(result.durationSeconds, 1) + " s contour",
                             juce::dontSendNotification);
    });
}
