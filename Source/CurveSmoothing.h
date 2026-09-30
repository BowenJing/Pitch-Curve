#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace PitchCurveSmoothing
{
// The final point must close the loop by repeating the first value at position
// 1. Smooth 0 is the original polyline. Higher levels first use a fixed local
// window, then connect the filtered samples with monotone cubic interpolation.
// The range gate protects amplitudes; the monotone interpolation gives peaks
// continuous tangents without overshoot or non-local editing.
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

    // Grow the time footprint across the full control travel. The upper levels
    // open progressively faster so 10 produces a clearly rounded contour,
    // while the maximum still affects only a small neighbourhood around the
    // current position.
    static constexpr std::array<int, 11> windowRadius {
        0, 2, 3, 4, 5, 7, 9, 11, 13, 15, 18
    };
    const int radius = windowRadius[static_cast<size_t>(smooth)];
    // Keep the time footprint independent of source point density. Sparse
    // learned contours must not receive a much wider brush than dense hand
    // drawing, and dense contours must remain visibly smooth.
    constexpr float sampleSpacing = 1.0f / 256.0f;
    static constexpr std::array<float, 11> perceptualBlend {
        0.0f, 0.24f, 0.35f, 0.46f, 0.56f, 0.66f,
        0.75f, 0.83f, 0.90f, 0.96f, 1.0f
    };

    const auto filteredValueAt = [&] (float samplePosition)
    {
        const float centre = linearValueAt(samplePosition);
        float localMinimum = centre;
        float localMaximum = centre;
        float weightedValue = 0.0f;
        float totalWeight = 0.0f;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            const float value = linearValueAt(
                samplePosition + static_cast<float>(offset) * sampleSpacing);
            const float weight =
                static_cast<float>(radius + 1 - std::abs(offset));
            weightedValue += value * weight;
            totalWeight += weight;
            localMinimum = std::min(localMinimum, value);
            localMaximum = std::max(localMaximum, value);
        }
        if (totalWeight <= 0.0f || localMaximum - localMinimum <= 1.0e-6f)
            return centre;

        const float localMidpoint = 0.5f * (localMinimum + localMaximum);
        const float halfRange = 0.5f * (localMaximum - localMinimum);
        const float extremeness = std::clamp(
            std::abs(centre - localMidpoint) / halfRange, 0.0f, 1.0f);
        // Cubic interpolation provides the rounded cap, so extrema themselves
        // need only a small residual filter blend to retain authored depth.
        constexpr float extremaSmoothingFloor = 0.08f;
        const float rangePreservation =
            extremaSmoothingFloor
            + (1.0f - extremaSmoothingFloor)
                * (1.0f - extremeness * extremeness);
        const float blend =
            perceptualBlend[static_cast<size_t>(smooth)] * rangePreservation;
        const float localAverage = weightedValue / totalWeight;
        return std::clamp(centre + blend * (localAverage - centre),
                          localMinimum, localMaximum);
    };

    // Interpolate the fixed filtered grid rather than returning another
    // polyline. Harmonic tangents become zero at a peak or valley, producing a
    // rounded cap, and cannot create overshoot between adjacent samples.
    position -= std::floor(position);
    const float gridPosition = position / sampleSpacing;
    const int gridIndex = static_cast<int>(std::floor(gridPosition));
    const float proportion = gridPosition - static_cast<float>(gridIndex);
    const auto gridValue = [&] (int index)
    {
        return filteredValueAt(static_cast<float>(index) * sampleSpacing);
    };

    const float previous = gridValue(gridIndex - 1);
    const float current = gridValue(gridIndex);
    const float next = gridValue(gridIndex + 1);
    const float following = gridValue(gridIndex + 2);
    const auto monotoneTangent = [] (float leftDelta, float rightDelta)
    {
        if (leftDelta * rightDelta <= 0.0f)
            return 0.0f;
        return 2.0f * leftDelta * rightDelta / (leftDelta + rightDelta);
    };
    const float currentTangent =
        monotoneTangent(current - previous, next - current);
    const float nextTangent =
        monotoneTangent(next - current, following - next);
    const float t2 = proportion * proportion;
    const float t3 = t2 * proportion;
    const float interpolated =
        (2.0f * t3 - 3.0f * t2 + 1.0f) * current
        + (t3 - 2.0f * t2 + proportion) * currentTangent
        + (-2.0f * t3 + 3.0f * t2) * next
        + (t3 - t2) * nextTangent;
    return std::clamp(interpolated, std::min(current, next),
                      std::max(current, next));
}
}
