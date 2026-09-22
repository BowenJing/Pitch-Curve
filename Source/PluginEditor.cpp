#include "PluginEditor.h"

namespace Palette
{
const auto background = juce::Colour::fromRGB(13, 15, 19);
const auto panel = juce::Colour::fromRGB(23, 26, 32);
const auto panelLight = juce::Colour::fromRGB(32, 36, 44);
const auto text = juce::Colour::fromRGB(238, 240, 244);
const auto muted = juce::Colour::fromRGB(139, 145, 158);
const auto accent = juce::Colour::fromRGB(183, 255, 104);
const auto cyan = juce::Colour::fromRGB(90, 216, 224);
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

CurveEditor::CurveEditor(ContourAudioProcessor& owner) : processor(owner)
{
    editablePoints = processor.getContour();
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
        juce::ColourGradient gradient(Palette::cyan.withAlpha(0.20f), bounds.getCentreX(), bounds.getY(),
                                      Palette::cyan.withAlpha(0.01f), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(gradient);
        g.fillPath(fill);
        g.setColour(Palette::cyan.withAlpha(0.18f));
        g.strokePath(curve, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(Palette::cyan);
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const float playhead = bounds.getX() + processor.getPlayheadPosition() * bounds.getWidth();
    g.setColour(Palette::accent.withAlpha(0.85f));
    g.drawVerticalLine(juce::roundToInt(playhead), bounds.getY(), bounds.getBottom());
}

void CurveEditor::drawAt(juce::Point<float> point)
{
    const auto bounds = graphBounds();
    const float x = juce::jlimit(0.0f, 1.0f, (point.x - bounds.getX()) / bounds.getWidth());
    const float cents = centsFromY(point.y);

    if (! isDrawing)
    {
        editablePoints.clear();
        constexpr int pointCount = 160;
        editablePoints.reserve(pointCount);
        for (int i = 0; i < pointCount; ++i)
            editablePoints.push_back({ static_cast<float>(i) / (pointCount - 1), 0.0f, 1.0f });
        isDrawing = true;
    }

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
    processor.setContour(editablePoints, processor.getContourDuration());
    repaint();
}

void CurveEditor::mouseDown(const juce::MouseEvent& event)
{
    isDrawing = false;
    drawAt(event.position);
}

void CurveEditor::mouseDrag(const juce::MouseEvent& event)
{
    drawAt(event.position);
}

void CurveEditor::mouseDoubleClick(const juce::MouseEvent&)
{
    editablePoints.clear();
    processor.setContour({}, processor.getContourDuration());
    repaint();
}

void CurveEditor::timerCallback()
{
    if (! isMouseButtonDown())
    {
        const auto latest = processor.getContour();
        if (latest.size() != editablePoints.size()
            || (! latest.empty() && ! editablePoints.empty()
                && latest.front().position != editablePoints.front().position))
            editablePoints = latest;
    }
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

    title.setText("CONTOUR", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(25.0f, juce::Font::bold)));
    subtitle.setText("Pitch motion, transferred.", juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId, Palette::muted);
    status.setText("Draw a curve, or learn one from audio", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, Palette::muted);
    fileName.setText("WAV · AIFF · FLAC · MP3", juce::dontSendNotification);
    fileName.setColour(juce::Label::textColourId, Palette::muted);
    fileName.setJustificationType(juce::Justification::centred);
    amountLabel.setText("AMOUNT", juce::dontSendNotification);
    amountLabel.setColour(juce::Label::textColourId, Palette::muted);
    amountLabel.setJustificationType(juce::Justification::centred);

    fileButton.onClick = [this] { chooseFile(); };
    learnButton.setComponentID("primary");
    learnButton.setColour(juce::TextButton::textColourOffId, Palette::background);
    learnButton.setEnabled(false);
    learnButton.onClick = [this] { beginLearning(); };
    amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 22);
    amount.setDoubleClickReturnValue(true, 1.0);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters(), "amount", amount);

    for (auto* component : std::initializer_list<juce::Component*> {
             &title, &subtitle, &status, &fileName, &fileButton, &learnButton,
             &amountLabel, &amount, &curveEditor })
        addAndMakeVisible(component);
}

ContourAudioProcessorEditor::~ContourAudioProcessorEditor()
{
    signalThreadShouldExit();
    stopThread(3000);
    setLookAndFeel(nullptr);
}

void ContourAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    auto dropArea = juce::Rectangle<float>(24.0f, 96.0f, getWidth() - 48.0f, 94.0f);
    g.setColour(Palette::panel);
    g.fillRoundedRectangle(dropArea, 12.0f);
    g.setColour(Palette::muted.withAlpha(0.35f));
    g.drawRoundedRectangle(dropArea, 12.0f, 1.0f);
}

void ContourAudioProcessorEditor::resized()
{
    const int margin = 24;
    title.setBounds(margin, 18, 180, 34);
    subtitle.setBounds(margin, 49, 240, 24);
    status.setBounds(getWidth() - 330, 27, 306, 28);

    auto drop = juce::Rectangle<int>(margin, 108, getWidth() - margin * 2, 70);
    fileButton.setBounds(drop.removeFromLeft(220));
    learnButton.setBounds(drop.removeFromRight(180));
    fileName.setBounds(drop.reduced(16, 0));

    const int controlWidth = 120;
    auto content = juce::Rectangle<int>(margin, 210, getWidth() - margin * 2,
                                        getHeight() - 234);
    auto control = content.removeFromRight(controlWidth);
    curveEditor.setBounds(content.reduced(0, 0).withTrimmedRight(16));
    amount.setBounds(control.getX(), control.getCentreY() - 65, controlWidth, 120);
    amountLabel.setBounds(control.getX(), control.getCentreY() + 54, controlWidth, 24);
}

bool ContourAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;
    const auto extension = juce::File(files[0]).getFileExtension().toLowerCase();
    return { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg" }.contains(extension);
}

void ContourAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag(files))
        setSelectedFile(juce::File(files[0]));
}

void ContourAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Choose a performance to learn", juce::File {},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [safe = juce::Component::SafePointer(this)] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile())
            safe->setSelectedFile(fc.getResult());
    });
}

void ContourAudioProcessorEditor::setSelectedFile(const juce::File& file)
{
    selectedFile = file;
    fileName.setText(file.getFileName(), juce::dontSendNotification);
    status.setText("Ready to analyse", juce::dontSendNotification);
    learnButton.setEnabled(true);
}

void ContourAudioProcessorEditor::beginLearning()
{
    if (! selectedFile.existsAsFile() || analysing.exchange(true))
        return;
    status.setText("Listening for pitch movement…", juce::dontSendNotification);
    learnButton.setEnabled(false);
    startThread();
}

void ContourAudioProcessorEditor::run()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(selectedFile));
    PitchAnalysis analysis;

    if (reader != nullptr && ! threadShouldExit())
    {
        constexpr double maximumLengthSeconds = 300.0;
        const auto sampleCount = static_cast<int>(
            juce::jmin<int64_t>(reader->lengthInSamples,
                static_cast<int64_t>(reader->sampleRate * maximumLengthSeconds)));
        juce::AudioBuffer<float> audio(
            juce::jlimit(1, 2, static_cast<int>(reader->numChannels)), sampleCount);
        if (reader->read(&audio, 0, sampleCount, 0, true, true) && ! threadShouldExit())
            analysis = PitchDetector::analyse(audio, reader->sampleRate);
    }

    if (threadShouldExit())
        return;

    auto safe = juce::Component::SafePointer(this);
    juce::MessageManager::callAsync([safe, result = std::move(analysis)] () mutable
    {
        if (safe == nullptr)
            return;
        safe->analysing.store(false);
        safe->learnButton.setEnabled(true);
        if (result.points.empty())
        {
            safe->status.setText("No stable pitch found — try a monophonic source",
                                 juce::dontSendNotification);
            return;
        }
        safe->processor.setContour(std::move(result.points), result.durationSeconds);
        safe->status.setText(juce::String(result.referenceHz, 1) + " Hz reference · "
                                 + juce::String(result.durationSeconds, 1) + " s contour",
                             juce::dontSendNotification);
    });
}
