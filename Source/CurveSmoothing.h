#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
inline float supportRadius(int smooth)
{
    return 0.016f * static_cast<float>(std::clamp(smooth, 0, 10));
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
    constexpr int kernelRadius = 64;
    const float sigma = 0.004f * static_cast<float>(smooth);
    const float radius = supportRadius(smooth);
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    for (int offset = -kernelRadius; offset <= kernelRadius; ++offset)
    {
        const float proportion = static_cast<float>(offset)
                               / static_cast<float>(kernelRadius);
        float wrappedPosition = position + proportion * radius;
        wrappedPosition -= std::floor(wrappedPosition);
        const float distanceInSigma = proportion * 4.0f;
        const float weight = std::exp(-0.5f * distanceInSigma * distanceInSigma);
        weightedValue += linearValueAt(wrappedPosition) * weight;
        totalWeight += weight;
    }

    const float rounded = totalWeight > 0.0f
        ? weightedValue / totalWeight
        : linear;
    return rounded;
}
}
