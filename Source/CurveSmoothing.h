#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
template <typename LinearValueAt>
float valueAt(float position, int smooth,
              LinearValueAt&& linearValueAt)
{
    smooth = std::clamp(smooth, 0, 10);
    const float linear = linearValueAt(position);
    if (smooth == 0)
        return linear;

    // Each step expands the same triangular low-pass kernel by one percent of
    // the curve duration. This makes the transition from the unfiltered
    // polyline at 0 to the rounded curve at 10 continuous and clearly audible.
    constexpr int kernelRadius = 16;
    const float radius = 0.01f * static_cast<float>(smooth);
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    for (int offset = -kernelRadius; offset <= kernelRadius; ++offset)
    {
        const float proportion = static_cast<float>(offset)
                               / static_cast<float>(kernelRadius);
        float wrappedPosition = position + proportion * radius;
        wrappedPosition -= std::floor(wrappedPosition);
        const float weight =
            static_cast<float>(kernelRadius + 1 - std::abs(offset));
        weightedValue += linearValueAt(wrappedPosition) * weight;
        totalWeight += weight;
    }

    const float rounded = totalWeight > 0.0f
        ? weightedValue / totalWeight
        : linear;
    return rounded;
}
}
