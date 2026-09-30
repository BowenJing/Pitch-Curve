#pragma once

#include <algorithm>
#include <cmath>

namespace PitchCurveSmoothing
{
// The final point must close the loop by repeating the first value at position
// 1. Smooth 0 is the original polyline. Higher levels progressively replace
// each monotonic run between a local peak and valley with a raised-cosine arc.
// Peaks and valleys remain exact anchors, so smoothing is clearly visible
// without attenuating extrema, reversing feature amplitudes, or overshooting.
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

    const auto isExtremum = [&] (int index)
    {
        const int before = (index + uniquePointCount - 1) % uniquePointCount;
        const int after = (index + 1) % uniquePointCount;
        const float value = valueAtPoint(index);
        const float incoming = value - valueAtPoint(before);
        const float outgoing = valueAtPoint(after) - value;
        return (incoming > 0.0f && outgoing <= 0.0f)
            || (incoming < 0.0f && outgoing >= 0.0f);
    };

    int leftAnchor = -1;
    int rightAnchor = -1;
    for (int offset = 0; offset < uniquePointCount; ++offset)
    {
        const int candidate =
            (current - offset + uniquePointCount) % uniquePointCount;
        if (isExtremum(candidate))
        {
            leftAnchor = candidate;
            break;
        }
    }
    for (int offset = 0; offset < uniquePointCount; ++offset)
    {
        const int candidate = (next + offset) % uniquePointCount;
        if (isExtremum(candidate))
        {
            rightAnchor = candidate;
            break;
        }
    }
    if (leftAnchor < 0 || rightAnchor < 0 || leftAnchor == rightAnchor)
        return linear;

    float leftPosition = positionAt(leftAnchor);
    float rightPosition = positionAt(rightAnchor);
    if (leftPosition > position)
        leftPosition -= 1.0f;
    if (rightPosition <= position)
        rightPosition += 1.0f;
    const float anchorWidth = rightPosition - leftPosition;
    if (anchorWidth <= 0.0f)
        return linear;

    const float anchorProportion =
        std::clamp((position - leftPosition) / anchorWidth, 0.0f, 1.0f);
    constexpr float pi = 3.14159265358979323846f;
    const float cosineBlend = 0.5f
        - 0.5f * std::cos(pi * anchorProportion);
    const float shapePreserving =
        valueAtPoint(leftAnchor)
        + cosineBlend * (valueAtPoint(rightAnchor) - valueAtPoint(leftAnchor));
    const float blend = static_cast<float>(smooth) / 10.0f;
    return linear + blend * (shapePreserving - linear);
}
}
