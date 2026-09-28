#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
constexpr int stepIntervals = 24;

inline float quantiseStepPosition(float position)
{
    position -= std::floor(position);
    return std::floor(position * static_cast<float>(stepIntervals))
         / static_cast<float>(stepIntervals);
}

template <typename LinearValueAt, typename SteppedValueAt>
float valueAt(float position, int smooth,
              LinearValueAt&& linearValueAt,
              SteppedValueAt&& steppedValueAt)
{
    smooth = std::clamp(smooth, 0, 10);
    const float stepped = steppedValueAt(position);
    if (smooth == 0)
        return stepped;

    const float linear = linearValueAt(position);
    const float normalized = static_cast<float>(smooth) / 10.0f;
    const float blend = normalized * normalized * (3.0f - 2.0f * normalized);
    constexpr int kernelRadius = 10;
    const float radius = 0.04f * std::sqrt(normalized);
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
    return linear + blend * (rounded - linear);
}
}
