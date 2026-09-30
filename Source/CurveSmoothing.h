#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace PitchCurveSmoothing
{
// The final point must close the loop by repeating the first value at position
// 1. Smooth 0 is the original polyline. Higher levels use a small, fixed local
// window. The local-range gate pins peaks and valleys while smoothing shoulders
// and corners, so edits cannot propagate beyond the window and the result
// cannot overshoot its source neighbourhood.
template <typename PositionAt, typename ValueAt>
float valueAt(float position, int smooth, int pointCount,
              PositionAt&& positionAt, ValueAt&& valueAtPoint)
{
    smooth = std::clamp(smooth, 0, 10);
    if (pointCount <= 0)
        return 0.0f;
    if (pointCount <= 2)
        return valueAtPoint(0);

    const int uniquePointCount = pointCount - 1;
    const auto linearValueAt = [&] (float samplePosition)
    {
        samplePosition -= std::floor(samplePosition);
        int low = 1;
        int high = uniquePointCount;
        while (low < high)
        {
            const int middle = low + (high - low) / 2;
            if (positionAt(middle) <= samplePosition)
                low = middle + 1;
            else
                high = middle;
        }

        const int next = low == uniquePointCount ? 0 : low;
        const int current = next == 0 ? uniquePointCount - 1 : next - 1;
        const float currentPosition = positionAt(current);
        const float nextPosition =
            next == 0 ? positionAt(0) + 1.0f : positionAt(next);
        if (next == 0 && samplePosition < currentPosition)
            samplePosition += 1.0f;
        const float width = nextPosition - currentPosition;
        if (width <= 0.0f)
            return valueAtPoint(current);
        const float proportion = std::clamp(
            (samplePosition - currentPosition) / width, 0.0f, 1.0f);
        return valueAtPoint(current)
             + proportion * (valueAtPoint(next) - valueAtPoint(current));
    };

    const float linear = linearValueAt(position);
    if (smooth == 0)
        return linear;

    static constexpr std::array<int, 11> windowRadius {
        0, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6
    };
    const int radius = windowRadius[static_cast<size_t>(smooth)];
    // Keep the time footprint independent of source point density. Sparse
    // learned contours must not receive a much wider brush than dense hand
    // drawing, and dense contours must remain visibly smooth.
    constexpr float sampleSpacing = 1.0f / 256.0f;
    float localMinimum = linear;
    float localMaximum = linear;
    float weightedValue = 0.0f;
    float totalWeight = 0.0f;
    for (int offset = -radius; offset <= radius; ++offset)
    {
        const float value =
            linearValueAt(position + static_cast<float>(offset) * sampleSpacing);
        const float weight = static_cast<float>(radius + 1 - std::abs(offset));
        weightedValue += value * weight;
        totalWeight += weight;
        localMinimum = std::min(localMinimum, value);
        localMaximum = std::max(localMaximum, value);
    }
    if (totalWeight <= 0.0f || localMaximum - localMinimum <= 1.0e-6f)
        return linear;

    const float localMidpoint = 0.5f * (localMinimum + localMaximum);
    const float halfRange = 0.5f * (localMaximum - localMinimum);
    const float extremeness = std::clamp(
        std::abs(linear - localMidpoint) / halfRange, 0.0f, 1.0f);
    constexpr float extremaSmoothingFloor = 0.18f;
    const float rangePreservation =
        extremaSmoothingFloor
        + (1.0f - extremaSmoothingFloor)
            * (1.0f - extremeness * extremeness);

    static constexpr std::array<float, 11> perceptualBlend {
        0.0f, 0.25f, 0.36f, 0.47f, 0.57f, 0.66f,
        0.74f, 0.82f, 0.89f, 0.95f, 1.0f
    };
    const float blend =
        perceptualBlend[static_cast<size_t>(smooth)] * rangePreservation;
    const float localAverage = weightedValue / totalWeight;
    return std::clamp(linear + blend * (localAverage - linear),
                      localMinimum, localMaximum);
}
}
