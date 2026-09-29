#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace PitchCurveSmoothing
{
constexpr int kernelRadius = 64;

inline const std::array<float, kernelRadius * 2 + 1>& gaussianWeights()
{
    static const auto weights = []
    {
        std::array<float, kernelRadius * 2 + 1> result {};
        for (int offset = -kernelRadius; offset <= kernelRadius; ++offset)
        {
            const float proportion = static_cast<float>(offset)
                                   / static_cast<float>(kernelRadius);
            const float distanceInSigma = proportion * 4.0f;
            result[static_cast<size_t>(offset + kernelRadius)] =
                std::exp(-0.5f * distanceInSigma * distanceInSigma);
        }
        return result;
    }();
    return weights;
}

inline void prepare()
{
    (void) gaussianWeights();
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
    const float sigma = 0.004f * static_cast<float>(smooth);
    const float radius = 4.0f * sigma;
    const auto& weights = gaussianWeights();
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    for (int offset = -kernelRadius; offset <= kernelRadius; ++offset)
    {
        const float proportion = static_cast<float>(offset)
                               / static_cast<float>(kernelRadius);
        float wrappedPosition = position + proportion * radius;
        wrappedPosition -= std::floor(wrappedPosition);
        const float weight =
            weights[static_cast<size_t>(offset + kernelRadius)];
        weightedValue += linearValueAt(wrappedPosition) * weight;
        totalWeight += weight;
    }

    const float rounded = totalWeight > 0.0f
        ? weightedValue / totalWeight
        : linear;
    return rounded;
}
}
