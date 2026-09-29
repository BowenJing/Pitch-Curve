#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace PitchCurveSmoothing
{
inline float supportRadius(int smooth)
{
    return 0.016f * static_cast<float>(std::clamp(smooth, 0, 10));
}

template <typename WeightedSample>
void forEachWeightedSample(float position, int smooth,
                           WeightedSample&& weightedSample)
{
    smooth = std::clamp(smooth, 0, 10);
    if (smooth == 0)
    {
        position -= std::floor(position);
        weightedSample(position, 1.0f);
        return;
    }

    constexpr int kernelRadius = 64;
    const float radius = supportRadius(smooth);
    for (int offset = -kernelRadius; offset <= kernelRadius; ++offset)
    {
        const float proportion = static_cast<float>(offset)
                               / static_cast<float>(kernelRadius);
        float wrappedPosition = position + proportion * radius;
        wrappedPosition -= std::floor(wrappedPosition);
        const float distanceInSigma = proportion * 4.0f;
        const float weight = std::exp(-0.5f * distanceInSigma * distanceInSigma);
        weightedSample(wrappedPosition, weight);
    }
}

inline std::vector<float> discreteInfluence(float position, int smooth,
                                            int uniquePointCount)
{
    if (uniquePointCount <= 0)
        return {};

    std::vector<float> influence(static_cast<size_t>(uniquePointCount), 0.0f);
    float totalKernelWeight = 0.0f;
    forEachWeightedSample(
        position, smooth,
        [&] (float samplePosition, float kernelWeight)
        {
            const float scaled =
                samplePosition * static_cast<float>(uniquePointCount);
            const int lower = std::clamp(
                static_cast<int>(std::floor(scaled)), 0, uniquePointCount - 1);
            const int upper = (lower + 1) % uniquePointCount;
            const float fraction = scaled - static_cast<float>(lower);
            influence[static_cast<size_t>(lower)] +=
                kernelWeight * (1.0f - fraction);
            influence[static_cast<size_t>(upper)] += kernelWeight * fraction;
            totalKernelWeight += kernelWeight;
        });

    if (totalKernelWeight > 0.0f)
        for (auto& weight : influence)
            weight /= totalKernelWeight;
    return influence;
}

template <typename LinearValueAt>
float valueAt(float position, int smooth,
              LinearValueAt&& linearValueAt)
{
    smooth = std::clamp(smooth, 0, 10);
    const float linear = linearValueAt(position);
    if (smooth == 0)
        return linear;

    // Use a densely sampled Gaussian kernel so short, tightly-spaced movements
    // cannot alias into a new zig-zag pattern at high smoothing levels.
    // Smooth 0 remains the original polyline; each following level increases
    // sigma by the same amount for a predictable 0-10 progression.
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    forEachWeightedSample(position, smooth,
        [&] (float samplePosition, float weight)
        {
            weightedValue += linearValueAt(samplePosition) * weight;
            totalWeight += weight;
        });

    const float rounded = totalWeight > 0.0f
        ? weightedValue / totalWeight
        : linear;
    return rounded;
}
}
