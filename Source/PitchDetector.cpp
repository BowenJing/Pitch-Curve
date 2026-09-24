#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int frameSize = 2048;
constexpr int hopSize = 256;
constexpr double maximumAnalysisSampleRate = 48000.0;

float median(std::vector<float> values)
{
    if (values.empty())
        return 0.0f;

    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

juce::AudioBuffer<float> downsampleForAnalysis(const juce::AudioBuffer<float>& input,
                                                double sourceRate,
                                                std::function<bool()>& shouldCancel)
{
    const double ratio = sourceRate / maximumAnalysisSampleRate;
    const int outputSamples = static_cast<int>(
        std::floor(static_cast<double>(input.getNumSamples()) / ratio));
    juce::AudioBuffer<float> output(1, juce::jmax(0, outputSamples));

    for (int outputSample = 0; outputSample < outputSamples; ++outputSample)
    {
        if ((outputSample & 1023) == 0 && shouldCancel && shouldCancel())
            return {};

        const double start = outputSample * ratio;
        const double end = juce::jmin(static_cast<double>(input.getNumSamples()),
                                      (outputSample + 1) * ratio);
        const int firstInput = static_cast<int>(std::floor(start));
        const int finalInput = static_cast<int>(std::ceil(end));
        double sum = 0.0;
        double totalWeight = 0.0;
        for (int inputSample = firstInput; inputSample < finalInput; ++inputSample)
        {
            const double weight = juce::jmax(
                0.0, juce::jmin(end, inputSample + 1.0) - juce::jmax(start, static_cast<double>(inputSample)));
            if (inputSample >= 0 && inputSample < input.getNumSamples())
            {
                sum += input.getSample(0, inputSample) * weight;
                totalWeight += weight;
            }
        }
        output.setSample(0, outputSample,
                         totalWeight > 0.0 ? static_cast<float>(sum / totalWeight) : 0.0f);
    }
    return output;
}
}

std::pair<float, float> PitchDetector::detectFrame(const float* samples,
                                                    int size,
                                                    double sampleRate,
                                                    float minimumHz,
                                                    float maximumHz)
{
    const int minimumLag = juce::jmax(2, static_cast<int>(sampleRate / maximumHz));
    const int maximumLag = juce::jmin(size / 2, static_cast<int>(sampleRate / minimumHz));
    if (minimumLag > maximumLag)
        return {};
    std::vector<float> difference(static_cast<size_t>(maximumLag + 1), 0.0f);

    double energy = 0.0;
    for (int i = 0; i < size; ++i)
        energy += static_cast<double>(samples[i]) * samples[i];

    if (std::sqrt(energy / size) < 0.001)
        return {};

    for (int lag = 1; lag <= maximumLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < size - lag; ++i)
        {
            const auto delta = samples[i] - samples[i + lag];
            sum += static_cast<double>(delta) * delta;
        }
        difference[static_cast<size_t>(lag)] = static_cast<float>(sum);
    }

    float runningSum = 0.0f;
    int selectedLag = 0;
    float selectedValue = 1.0f;
    constexpr float yinThreshold = 0.14f;

    for (int lag = 1; lag <= maximumLag; ++lag)
    {
        runningSum += difference[static_cast<size_t>(lag)];
        difference[static_cast<size_t>(lag)] =
            runningSum > 0.0f ? difference[static_cast<size_t>(lag)] * lag / runningSum : 1.0f;
    }

    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        if (lag >= minimumLag && difference[static_cast<size_t>(lag)] < yinThreshold)
        {
            while (lag + 1 <= maximumLag
                   && difference[static_cast<size_t>(lag + 1)] < difference[static_cast<size_t>(lag)])
                ++lag;
            selectedLag = lag;
            selectedValue = difference[static_cast<size_t>(lag)];
            break;
        }
    }

    if (selectedLag == 0)
    {
        auto begin = difference.begin() + minimumLag;
        auto end = difference.begin() + maximumLag + 1;
        const auto best = std::min_element(begin, end);
        selectedLag = static_cast<int>(std::distance(difference.begin(), best));
        selectedValue = *best;
        if (selectedValue > 0.35f)
            return {};
    }

    float refinedLag = static_cast<float>(selectedLag);
    if (selectedLag > minimumLag && selectedLag < maximumLag)
    {
        const float left = difference[static_cast<size_t>(selectedLag - 1)];
        const float centre = difference[static_cast<size_t>(selectedLag)];
        const float right = difference[static_cast<size_t>(selectedLag + 1)];
        const float denominator = left - 2.0f * centre + right;
        if (std::abs(denominator) > 1.0e-8f)
            refinedLag += 0.5f * (left - right) / denominator;
    }

    if (! std::isfinite(refinedLag) || refinedLag <= 0.0f)
        return {};
    const float frequency = static_cast<float>(sampleRate) / refinedLag;
    const float confidence = juce::jlimit(0.0f, 1.0f, 1.0f - selectedValue);
    return std::isfinite(frequency) && std::isfinite(confidence)
        ? std::pair<float, float> { frequency, confidence }
        : std::pair<float, float> {};
}

PitchAnalysis PitchDetector::analyse(const juce::AudioBuffer<float>& audio,
                                     double sampleRate,
                                     float minimumHz,
                                     float maximumHz,
                                     std::function<bool()> shouldCancel)
{
    PitchAnalysis result;
    if (! std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0
        || ! std::isfinite(minimumHz) || ! std::isfinite(maximumHz)
        || minimumHz <= 0.0f || maximumHz <= minimumHz
        || audio.getNumChannels() <= 0 || audio.getNumSamples() < frameSize)
        return result;

    result.durationSeconds = static_cast<float>(audio.getNumSamples() / sampleRate);

    juce::AudioBuffer<float> mono(1, audio.getNumSamples());
    mono.clear();
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        mono.addFrom(0, 0, audio, channel, 0, audio.getNumSamples(),
                     1.0f / static_cast<float>(audio.getNumChannels()));

    juce::AudioBuffer<float> downsampled;
    const juce::AudioBuffer<float>* analysisAudio = &mono;
    double analysisSampleRate = sampleRate;
    if (sampleRate > maximumAnalysisSampleRate)
    {
        downsampled = downsampleForAnalysis(mono, sampleRate, shouldCancel);
        if (downsampled.getNumSamples() == 0)
            return {};
        analysisAudio = &downsampled;
        analysisSampleRate = maximumAnalysisSampleRate;
    }

    struct Detection { float position; float hz; float confidence; };
    std::vector<Detection> detections;
    std::vector<float> frequencies;

    for (int start = 0; start + frameSize <= analysisAudio->getNumSamples(); start += hopSize)
    {
        if (shouldCancel && shouldCancel())
            return {};

        const auto [frequency, confidence] =
            detectFrame(analysisAudio->getReadPointer(0, start), frameSize,
                        analysisSampleRate, minimumHz, maximumHz);
        if (frequency > 0.0f && confidence >= 0.65f)
        {
            const float position = static_cast<float>(start + frameSize / 2)
                                 / static_cast<float>(analysisAudio->getNumSamples());
            detections.push_back({ position, frequency, confidence });
            frequencies.push_back(frequency);
        }
    }

    if (detections.size() < 3)
        return result;

    result.referenceHz = median(frequencies);
    std::vector<float> rawCents;
    rawCents.reserve(detections.size());
    for (const auto& detection : detections)
        rawCents.push_back(1200.0f * std::log2(detection.hz / result.referenceHz));

    for (size_t i = 0; i < detections.size(); ++i)
    {
        std::vector<float> neighbourhood;
        for (size_t j = i > 2 ? i - 2 : 0; j <= juce::jmin(i + 2, detections.size() - 1); ++j)
            neighbourhood.push_back(rawCents[j]);

        result.points.push_back({
            detections[i].position,
            juce::jlimit(-600.0f, 600.0f, median(std::move(neighbourhood))),
            detections[i].confidence
        });
    }

    return result;
}
