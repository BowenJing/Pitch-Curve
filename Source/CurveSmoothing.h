#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
template <typename LinearValueAt, typename SteppedValueAt>
float valueAt(float position, int smooth,
              LinearValueAt&& linearValueAt,
              SteppedValueAt&& steppedValueAt)
{
    smooth = std::clamp(smooth, 0, 10);
    const float stepped = steppedValueAt(position);
    if (smooth == 0)
        return stepped;

    const float radius = 0.004f * static_cast<float>(smooth);
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    for (int offset = -smooth; offset <= smooth; ++offset)
    {
        const float proportion = static_cast<float>(offset)
                               / static_cast<float>(smooth);
        float wrappedPosition = position + proportion * radius;
        wrappedPosition -= std::floor(wrappedPosition);
        const float weight = static_cast<float>(smooth + 1 - std::abs(offset));
        weightedValue += linearValueAt(wrappedPosition) * weight;
        totalWeight += weight;
    }

    const float rounded = totalWeight > 0.0f
        ? weightedValue / totalWeight
        : linearValueAt(position);
    const float blend = static_cast<float>(smooth) / 10.0f;
    return stepped + blend * (rounded - stepped);
}
}
