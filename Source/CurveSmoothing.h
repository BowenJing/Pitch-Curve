#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
inline float monotoneTangent(float previousValue, float value, float nextValue,
                             float previousWidth, float nextWidth)
{
    if (previousWidth <= 0.0f || nextWidth <= 0.0f)
        return 0.0f;

    const float previousSlope = (value - previousValue) / previousWidth;
    const float nextSlope = (nextValue - value) / nextWidth;
    if (previousSlope == 0.0f || nextSlope == 0.0f
        || std::signbit(previousSlope) != std::signbit(nextSlope))
        return 0.0f;

    const float previousWeight = 2.0f * nextWidth + previousWidth;
    const float nextWeight = nextWidth + 2.0f * previousWidth;
    return (previousWeight + nextWeight)
         / (previousWeight / previousSlope + nextWeight / nextSlope);
}

// The final point must close the loop by repeating the first value at position
// 1. Smooth 0 is the original polyline. Higher levels progressively blend
// toward a cyclic, monotone cubic Hermite curve which passes through every
// source point. This rounds corners without attenuating extrema, reversing
// feature amplitudes, or creating overshoot.
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
    position -= std::floor(position);

    int low = 1;
    int high = uniquePointCount;
    while (low < high)
    {
        const int middle = low + (high - low) / 2;
        if (positionAt(middle) <= position)
            low = middle + 1;
        else
            high = middle;
    }

    const int next = low == uniquePointCount ? 0 : low;
    const int current = next == 0 ? uniquePointCount - 1 : next - 1;
    const int previous = (current + uniquePointCount - 1) % uniquePointCount;
    const int afterNext = (next + 1) % uniquePointCount;

    const float currentPosition = positionAt(current);
    const float nextPosition = next == 0 ? positionAt(0) + 1.0f : positionAt(next);
    float evaluationPosition = position;
    if (next == 0 && evaluationPosition < currentPosition)
        evaluationPosition += 1.0f;
    const float segmentWidth = nextPosition - currentPosition;
    if (segmentWidth <= 0.0f)
        return valueAtPoint(current);

    const float proportion =
        std::clamp((evaluationPosition - currentPosition) / segmentWidth, 0.0f, 1.0f);
    const float currentValue = valueAtPoint(current);
    const float nextValue = valueAtPoint(next);
    const float linear = currentValue + proportion * (nextValue - currentValue);
    if (smooth == 0)
        return linear;

    const float previousPosition = current == 0
        ? positionAt(previous) - 1.0f
        : positionAt(previous);
    const float afterNextPosition = afterNext == 0
        ? positionAt(0) + 1.0f
        : positionAt(afterNext);
    const float currentTangent = monotoneTangent(
        valueAtPoint(previous), currentValue, nextValue,
        currentPosition - previousPosition, segmentWidth);
    const float nextTangent = monotoneTangent(
        currentValue, nextValue, valueAtPoint(afterNext),
        segmentWidth, afterNextPosition - nextPosition);

    const float squared = proportion * proportion;
    const float cubed = squared * proportion;
    const float cubic =
        (2.0f * cubed - 3.0f * squared + 1.0f) * currentValue
        + (cubed - 2.0f * squared + proportion) * segmentWidth * currentTangent
        + (-2.0f * cubed + 3.0f * squared) * nextValue
        + (cubed - squared) * segmentWidth * nextTangent;
    const float shapePreserving = std::clamp(
        cubic, std::min(currentValue, nextValue), std::max(currentValue, nextValue));
    const float blend = static_cast<float>(smooth) / 10.0f;
    return linear + blend * (shapePreserving - linear);
}
}
