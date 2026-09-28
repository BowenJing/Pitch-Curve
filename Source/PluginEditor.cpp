#include "PluginEditor.h"
#include "CurveSmoothing.h"

namespace Palette
{
const auto background = juce::Colour::fromRGB(27, 32, 38);
const auto panel = juce::Colour::fromRGB(37, 44, 52);
const auto panelLight = juce::Colour::fromRGB(50, 59, 69);
const auto text = juce::Colour::fromRGB(234, 238, 242);
const auto muted = juce::Colour::fromRGB(161, 171, 182);
const auto accent = juce::Colour::fromRGB(126, 211, 177);
}

namespace
{
constexpr int framesPerSecond = 30;
constexpr int maximumDurationSeconds = 60;
constexpr int maximumDurationFrames = maximumDurationSeconds * framesPerSecond;

class LimitedAudioFormatReader final : public juce::AudioFormatReader
{
public:
    LimitedAudioFormatReader(std::unique_ptr<juce::AudioFormatReader> sourceReader,
                             juce::int64 maximumSamples)
        : juce::AudioFormatReader(nullptr, sourceReader->getFormatName()),
          source(std::move(sourceReader))
    {
        sampleRate = source->sampleRate;
        bitsPerSample = source->bitsPerSample;
        lengthInSamples = std::min<juce::int64>(source->lengthInSamples, maximumSamples);
        numChannels = juce::jmin(2u, source->numChannels);
        usesFloatingPointData = source->usesFloatingPointData;
    }

    bool readSamples(int* const* destination, int destinationChannels,
                     int destinationOffset, juce::int64 sourceStart,
                     int samples) override
    {
        if (sourceStart >= lengthInSamples)
        {
            for (int channel = 0; channel < destinationChannels; ++channel)
                if (destination[channel] != nullptr)
                    juce::zeromem(destination[channel] + destinationOffset,
                                  static_cast<size_t>(samples) * sizeof(int));
            return true;
        }
        const auto available = static_cast<int>(
            std::min<juce::int64>(samples, lengthInSamples - sourceStart));
        const bool succeeded =
            source->readSamples(destination, destinationChannels, destinationOffset,
                                sourceStart, available);
        for (int channel = 0; channel < destinationChannels; ++channel)
            if (destination[channel] != nullptr && available < samples)
                juce::zeromem(destination[channel] + destinationOffset + available,
                              static_cast<size_t>(samples - available) * sizeof(int));
        return succeeded;
    }

private:
    std::unique_ptr<juce::AudioFormatReader> source;
};
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
    auto colour = Palette::panelLight;
    if (highlighted)
        colour = colour.brighter(0.08f);
    if (down)
        colour = colour.darker(0.12f);
    g.setColour(colour.withAlpha(button.isEnabled() ? 1.0f : 0.62f));
    g.fillRoundedRectangle(button.getLocalBounds().toFloat(), 8.0f);
}

void ContourLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                        bool, bool)
{
    g.setColour(Palette::text);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0),
                     juce::Justification::centred, 1);
}

juce::Font ContourLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
}

CurveEditor::CurveEditor(ContourAudioProcessor& owner) : processor(owner)
{
    editablePoints = processor.getContour();
    observedRevision = processor.getContourRevision();
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    startTimerHz(60);
}

juce::Rectangle<float> CurveEditor::graphBounds() const
{
    return getLocalBounds().toFloat().reduced(18.0f, 22.0f);
}

float CurveEditor::centsFromY(float y) const
{
    const auto bounds = graphBounds();
    return juce::jmap(juce::jlimit(bounds.getY(), bounds.getBottom(), y),
                      bounds.getBottom(), bounds.getY(), -600.0f, 600.0f);
}

float CurveEditor::yFromCents(float cents) const
{
    const auto bounds = graphBounds();
    return juce::jmap(juce::jlimit(-600.0f, 600.0f, cents),
                      -600.0f, 600.0f, bounds.getBottom(), bounds.getY());
}

float CurveEditor::displayRangeSemitones() const
{
    const float amount = processor.parameters().getRawParameterValue("amount")->load();
    return juce::jlimit(0.0f, 12.0f, 6.0f * amount);
}

float CurveEditor::linearCentsAt(float position) const
{
    if (editablePoints.empty())
        return 0.0f;

    position -= std::floor(position);
    if (editablePointsAreUniform && editablePoints.size() > 1)
    {
        const float scaled = position * static_cast<float>(editablePoints.size() - 1);
        const auto lowerIndex = static_cast<size_t>(std::floor(scaled));
        const auto upperIndex = juce::jmin(lowerIndex + 1, editablePoints.size() - 1);
        return juce::jmap(scaled - static_cast<float>(lowerIndex),
                          editablePoints[lowerIndex].cents,
                          editablePoints[upperIndex].cents);
    }

    const auto upper = std::lower_bound(
        editablePoints.begin(), editablePoints.end(), position,
        [] (const PitchPoint& point, float value) { return point.position < value; });
    if (upper == editablePoints.begin())
        return upper->cents;
    if (upper == editablePoints.end())
        return editablePoints.back().cents;
    const auto lower = upper - 1;
    const float span = upper->position - lower->position;
    const float proportion = span > 0.0f
        ? (position - lower->position) / span
        : 0.0f;
    return juce::jmap(proportion, lower->cents, upper->cents);
}

float CurveEditor::steppedCentsAt(float position) const
{
    if (editablePoints.empty())
        return 0.0f;

    position = PitchCurveSmoothing::quantiseStepPosition(position);
    if (editablePointsAreUniform && editablePoints.size() > 1)
    {
        const auto index = static_cast<size_t>(std::floor(
            position * static_cast<float>(editablePoints.size() - 1)));
        return editablePoints[juce::jmin(index, editablePoints.size() - 1)].cents;
    }

    const auto upper = std::upper_bound(
        editablePoints.begin(), editablePoints.end(), position,
        [] (float value, const PitchPoint& point) { return value < point.position; });
    return upper == editablePoints.begin() ? editablePoints.front().cents
                                           : (upper - 1)->cents;
}

float CurveEditor::displayCentsAt(float position) const
{
    const int smooth = juce::roundToInt(
        processor.parameters().getRawParameterValue("smooth")->load());
    return PitchCurveSmoothing::valueAt(
        position, smooth,
        [this] (float samplePosition) { return linearCentsAt(samplePosition); },
        [this] (float samplePosition) { return steppedCentsAt(samplePosition); });
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
    const float semitoneRange = displayRangeSemitones();
    for (int tick = -2; tick <= 2; ++tick)
    {
        const float baseCents = static_cast<float>(tick) * 300.0f;
        const float semitones = semitoneRange * static_cast<float>(tick) / 2.0f;
        const float y = yFromCents(baseCents);
        g.setColour(tick == 0 ? Palette::muted.withAlpha(0.45f)
                              : Palette::muted.withAlpha(0.16f));
        g.drawHorizontalLine(juce::roundToInt(y), bounds.getX(), bounds.getRight());
        if (std::abs(tick) == 2)
        {
            const float rounded = std::round(semitones);
            const auto value = std::abs(semitones - rounded) < 0.01f
                ? juce::String(static_cast<int>(rounded))
                : juce::String(semitones, 1);
            constexpr float labelHeight = 18.0f;
            float labelY = y - labelHeight * 0.5f;
            labelY = juce::jlimit(bounds.getY() + 2.0f,
                                  bounds.getBottom() - labelHeight - 2.0f,
                                  labelY);
            const auto labelBounds = juce::Rectangle<float>(
                bounds.getX() + 5.0f, labelY, 64.0f, labelHeight);
            g.setColour(Palette::panel.withAlpha(0.9f));
            g.fillRoundedRectangle(labelBounds, 3.0f);
            g.setColour(Palette::text.withAlpha(0.82f));
            g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
            g.drawFittedText((semitones > 0.0f ? "+" : "") + value + " ST",
                             labelBounds.toNearestInt().reduced(3, 0),
                             juce::Justification::centredLeft, 1);
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
        const int smooth = juce::roundToInt(
            processor.parameters().getRawParameterValue("smooth")->load());
        if (smooth == 0)
        {
            float previousCents = steppedCentsAt(0.0f);
            curve.startNewSubPath(bounds.getX(), yFromCents(previousCents));
            for (int i = 1; i <= PitchCurveSmoothing::stepIntervals; ++i)
            {
                const float position = static_cast<float>(i)
                                     / PitchCurveSmoothing::stepIntervals;
                const float x = bounds.getX() + position * bounds.getWidth();
                curve.lineTo(x, yFromCents(previousCents));
                const float nextCents = steppedCentsAt(position);
                curve.lineTo(x, yFromCents(nextCents));
                previousCents = nextCents;
            }
        }
        else
        {
            constexpr int displaySamples = 256;
            for (int i = 0; i < displaySamples; ++i)
            {
                const float position = static_cast<float>(i) / (displaySamples - 1);
                const auto point = juce::Point<float>(
                    bounds.getX() + position * bounds.getWidth(),
                    yFromCents(displayCentsAt(position)));
                if (i == 0)
                    curve.startNewSubPath(point);
                else
                    curve.lineTo(point);
            }
        }

        juce::Path fill = curve;
        fill.lineTo(bounds.getRight(), yFromCents(0.0f));
        fill.lineTo(bounds.getX(), yFromCents(0.0f));
        fill.closeSubPath();
        const bool amountIsZero =
            processor.parameters().getRawParameterValue("amount")->load() <= 1.0e-6f;
        const auto curveColour = amountIsZero ? Palette::muted : Palette::accent;
        juce::ColourGradient gradient(curveColour.withAlpha(0.16f),
                                      bounds.getCentreX(), bounds.getY(),
                                      curveColour.withAlpha(0.01f),
                                      bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(gradient);
        g.fillPath(fill);
        g.setColour(curveColour.withAlpha(0.16f));
        g.strokePath(curve, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(curveColour);
        g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const float playhead = bounds.getX() + animatedPlayheadPosition * bounds.getWidth();
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
    const int smooth = juce::roundToInt(
        processor.parameters().getRawParameterValue("smooth")->load());
    const int radius = 3 + juce::roundToInt(0.8f * static_cast<float>(smooth));
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
    const double nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const bool playheadRunning = processor.isPlayheadRunning();
    bool repaintNeeded = playheadRunning != playheadWasRunning || playheadRunning;
    if (playheadRunning)
    {
        if (! playheadWasRunning)
        {
            animatedPlayheadPosition = 0.0f;
        }
        else
        {
            const double elapsed = juce::jmax(0.0, nowSeconds - playheadLastUpdateSeconds);
            const double duration = juce::jmax(
                1.0 / 30.0, static_cast<double>(processor.getContourDuration()));
            animatedPlayheadPosition = static_cast<float>(
                std::fmod(animatedPlayheadPosition + elapsed / duration, 1.0));
        }
    }
    else
    {
        animatedPlayheadPosition = 0.0f;
    }
    playheadWasRunning = playheadRunning;
    playheadLastUpdateSeconds = nowSeconds;

    const int currentSmooth = juce::roundToInt(
        processor.parameters().getRawParameterValue("smooth")->load());
    const float currentAmount =
        processor.parameters().getRawParameterValue("amount")->load();
    if (currentSmooth != observedSmooth
        || std::abs(currentAmount - observedAmount) > 1.0e-6f)
    {
        observedSmooth = currentSmooth;
        observedAmount = currentAmount;
        repaintNeeded = true;
    }

    if (! isMouseButtonDown())
    {
        const auto revision = processor.getContourRevision();
        if (revision != observedRevision)
        {
            editablePoints = processor.getContour();
            editablePointsAreUniform = false;
            observedRevision = revision;
            repaintNeeded = true;
        }
    }
    if (repaintNeeded)
        repaint();
}

AudioWaveformView::AudioWaveformView()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    setInterceptsMouseClicks(false, false);
}

bool AudioWaveformView::setFile(const juce::File& file)
{
    constexpr int64_t maximumFileBytes = 1024LL * 1024LL * 1024LL;
    constexpr int64_t maximumDecodedSamples = 12000000;
    constexpr double maximumLengthSeconds = 60.0;
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maximumFileBytes)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || ! std::isfinite(reader->sampleRate)
        || reader->sampleRate < 8000.0 || reader->sampleRate > 768000.0
        || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return false;

    const auto maximumSamples = juce::jmin<int64_t>(
        maximumDecodedSamples,
        static_cast<int64_t>(reader->sampleRate * maximumLengthSeconds));
    thumbnail.setReader(
        new LimitedAudioFormatReader(std::move(reader), maximumSamples),
        file.getFullPathName().hashCode64());
    repaint();
    return true;
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
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(720, 620, 1200, 900);
    setSize(900, 700);

    title.setText("PITCH CURVE", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    status.setText("Draw a curve, or learn one from audio", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, Palette::muted);
    status.setFont(juce::Font(juce::FontOptions(12.0f)));
    status.setJustificationType(juce::Justification::centredRight);
    fileName.setText("No Audio File", juce::dontSendNotification);
    fileName.setColour(juce::Label::textColourId, Palette::text);
    fileName.setJustificationType(juce::Justification::centred);
    fileName.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    secondsLabel.setText("SECOND", juce::dontSendNotification);
    framesLabel.setText("FRAME", juce::dontSendNotification);
    amountLabel.setText("AMOUNT", juce::dontSendNotification);
    smoothLabel.setText("SMOOTH", juce::dontSendNotification);
    for (auto* label : { &title, &status, &fileName, &secondsLabel, &framesLabel,
                         &amountLabel, &smoothLabel })
        label->setInterceptsMouseClicks(false, false);
    for (auto* label : { &secondsLabel, &framesLabel, &amountLabel, &smoothLabel })
    {
        label->setColour(juce::Label::textColourId, Palette::muted);
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    }

    for (auto* editor : { &secondsEditor, &framesEditor, &amountEditor, &smoothEditor })
    {
        editor->setJustification(juce::Justification::centred);
        editor->setSelectAllWhenFocused(true);
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
        editor->onFocusLost = [this]
        {
            if (secondsDirty || framesDirty)
                applyDurationTimecode();
        };
    }
    secondsEditor.onTextChange = [this] { secondsDirty = true; };
    framesEditor.onTextChange = [this] { framesDirty = true; };
    amountEditor.setInputRestrictions(3, "0123456789");
    amountEditor.onReturnKey = [this] { applyAmountText(); };
    amountEditor.onFocusLost = [this]
    {
        if (amountDirty)
            applyAmountText();
    };
    amountEditor.onTextChange = [this] { amountDirty = true; };
    smoothEditor.setInputRestrictions(2, "0123456789");
    smoothEditor.onReturnKey = [this] { applySmoothText(); };
    smoothEditor.onFocusLost = [this]
    {
        if (smoothDirty)
            applySmoothText();
    };
    smoothEditor.onTextChange = [this] { smoothDirty = true; };

    timeKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    timeKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    timeKnob.setRange(1.0, maximumDurationFrames, 1.0);
    timeKnob.setSkewFactorFromMidPoint(5.0 * framesPerSecond);
    timeKnob.setMouseClickGrabsKeyboardFocus(true);
    timeKnob.onDragStart = [this]
    {
        if (secondsDirty || framesDirty)
            applyDurationTimecode();
    };
    timeKnob.onValueChange = [this]
    {
        const int totalFrames = juce::roundToInt(timeKnob.getValue());
        processor.setContour(processor.getContour(),
                             static_cast<float>(totalFrames) / 30.0f);
        updateDurationTimecode();
    };
    updateDurationTimecode();

    fileButton.onClick = [this] { chooseFile(); };
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
    amount.setMouseClickGrabsKeyboardFocus(true);
    amount.onDragStart = [this]
    {
        if (amountDirty)
            applyAmountText();
    };
    amount.onValueChange = [this]
    {
        if (! amountDirty)
            updateAmountText();
    };
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters(), "amount", amount);
    updateAmountText();

    smooth.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    smooth.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    smooth.setRange(0.0, 10.0, 1.0);
    smooth.setMouseClickGrabsKeyboardFocus(true);
    smooth.onDragStart = [this]
    {
        if (smoothDirty)
            applySmoothText();
    };
    smooth.onValueChange = [this]
    {
        if (! smoothDirty)
            updateSmoothText();
    };
    smoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters(), "smooth", smooth);
    updateSmoothText();

    for (auto* button : { &fileButton, &learnButton, &clearButton })
        button->setMouseClickGrabsKeyboardFocus(true);

    for (auto* component : std::initializer_list<juce::Component*> {
             &title, &status, &waveform, &fileName, &fileButton, &learnButton,
             &clearButton, &secondsLabel, &framesLabel, &secondsEditor, &framesEditor,
             &amountEditor, &smoothEditor, &timeKnob, &amountLabel, &amount,
             &smoothLabel, &smooth, &curveEditor })
        addAndMakeVisible(component);

    observedProcessorRevision = processor.getContourRevision();
    startTimerHz(10);
}

ContourAudioProcessorEditor::~ContourAudioProcessorEditor()
{
    signalThreadShouldExit();
    stopThread(-1);
    setLookAndFeel(nullptr);
}

void ContourAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::background);
    auto dropArea = juce::Rectangle<float>(24.0f, 88.0f, getWidth() - 48.0f, 126.0f);
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

    auto drop = juce::Rectangle<int>(margin, 96, getWidth() - margin * 2, 110);
    const auto fileButtonArea = drop.removeFromLeft(200);
    const auto learnButtonArea = drop.removeFromRight(200);
    fileButton.setBounds(fileButtonArea.withSizeKeepingCentre(fileButtonArea.getWidth(), 96));
    learnButton.setBounds(learnButtonArea.withSizeKeepingCentre(learnButtonArea.getWidth(), 96));
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
    auto content = juce::Rectangle<int>(margin, 226, getWidth() - margin * 2,
                                        getHeight() - 250);
    auto control = content.removeFromRight(controlWidth);
    curveEditor.setBounds(content.reduced(0, 0).withTrimmedRight(16));

    const int clearHeight = 44;
    clearButton.setBounds(control.removeFromBottom(clearHeight));
    control.removeFromBottom(8);
    const int sectionHeight = control.getHeight() / 3;
    auto timeSection = control.removeFromTop(sectionHeight);
    auto amountSection = control.removeFromTop(sectionHeight);
    auto smoothSection = control;

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
                              juce::jmin(amountSection.getHeight() - valueBlockHeight,
                                         smoothSection.getHeight() - valueBlockHeight))));

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
    layoutControlGroup(smoothSection, smooth, smoothEditor, smoothLabel);
}

bool ContourAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (analysing.load() || files.isEmpty())
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
    const int64_t enteredSeconds = juce::jmax<int64_t>(0, secondsEditor.getText().getIntValue());
    const int64_t enteredFrames = juce::jmax<int64_t>(0, framesEditor.getText().getIntValue());
    const int totalFrames = juce::jlimit(
        1, maximumDurationFrames,
        static_cast<int>(juce::jmin<int64_t>(
            maximumDurationFrames, enteredSeconds * framesPerSecond + enteredFrames)));

    const float duration = static_cast<float>(totalFrames)
                         / static_cast<float>(framesPerSecond);
    secondsDirty = false;
    framesDirty = false;
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
    const int totalFrames = juce::jlimit(
        1, maximumDurationFrames,
        juce::roundToInt(processor.getContourDuration() * framesPerSecond));
    const auto secondsText = juce::String(totalFrames / framesPerSecond);
    const auto framesText = juce::String(totalFrames % framesPerSecond);
    if (! secondsDirty && secondsEditor.getText() != secondsText)
        secondsEditor.setText(secondsText, false);
    if (! framesDirty && framesEditor.getText() != framesText)
        framesEditor.setText(framesText, false);
    timeKnob.setValue(totalFrames, juce::dontSendNotification);
}

void ContourAudioProcessorEditor::applyAmountText()
{
    const int percentage = juce::jlimit(0, 200, amountEditor.getText().getIntValue());
    amountDirty = false;
    amount.setValue(static_cast<double>(percentage) / 100.0,
                    juce::sendNotificationSync);
    updateAmountText();
}

void ContourAudioProcessorEditor::updateAmountText()
{
    const auto text = juce::String(juce::roundToInt(amount.getValue() * 100.0));
    if (! amountDirty && amountEditor.getText() != text)
        amountEditor.setText(text, false);
}

void ContourAudioProcessorEditor::applySmoothText()
{
    const int value = juce::jlimit(0, 10, smoothEditor.getText().getIntValue());
    smoothDirty = false;
    smooth.setValue(value, juce::sendNotificationSync);
    updateSmoothText();
}

void ContourAudioProcessorEditor::updateSmoothText()
{
    const auto text = juce::String(juce::roundToInt(smooth.getValue()));
    if (! smoothDirty && smoothEditor.getText() != text)
        smoothEditor.setText(text, false);
}

void ContourAudioProcessorEditor::timerCallback()
{
    const auto revision = processor.getContourRevision();
    if (revision != observedProcessorRevision)
    {
        observedProcessorRevision = revision;
        updateDurationTimecode();
    }
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
        if (safe != nullptr && ! safe->analysing.load()
            && fc.getResult().existsAsFile())
            safe->setSelectedFile(fc.getResult());
    });
}

void ContourAudioProcessorEditor::setSelectedFile(const juce::File& file)
{
    if (analysing.load())
        return;

    if (! waveform.setFile(file))
    {
        selectedFile = juce::File {};
        waveform.clear();
        fileName.setText("No Audio File", juce::dontSendNotification);
        fileName.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        status.setText("Unsupported or unsafe audio file", juce::dontSendNotification);
        learnButton.setEnabled(false);
        resized();
        return;
    }

    selectedFile = file;
    fileName.setText(file.getFileName(), juce::dontSendNotification);
    fileName.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
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
    if (! startThread())
    {
        analysing.store(false);
        learnButton.setEnabled(true);
        fileButton.setEnabled(true);
        status.setText("Could not start audio analysis", juce::dontSendNotification);
    }
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
