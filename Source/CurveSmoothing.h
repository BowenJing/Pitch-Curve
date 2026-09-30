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

    int globalMinimum = 0;
    float minimumValue = valueAtPoint(0);
    float maximumValue = minimumValue;
    for (int index = 1; index < uniquePointCount; ++index)
    {
        const float value = valueAtPoint(index);
        if (value < minimumValue)
        {
            minimumValue = value;
            globalMinimum = index;
        }
        maximumValue = std::max(maximumValue, value);
    }
    const float valueRange = maximumValue - minimumValue;
    if (valueRange <= 1.0e-6f)
        return linear;

    // A small hysteresis rejects point-to-point drawing/detection jitter that
    // would otherwise make every sample an "extremum" and leave no interval
    // long enough to round visibly. It scales with the contour, while the
    // two-cent floor still allows genuinely subtle pitch motion to survive.
    const float turningThreshold = std::max(2.0f, valueRange * 0.015f);
    float targetPosition = position;
    const float startPosition = positionAt(globalMinimum);
    if (targetPosition < startPosition)
        targetPosition += 1.0f;

    int leftAnchor = globalMinimum;
    int rightAnchor = -1;
    float leftPosition = startPosition;
    float rightPosition = 0.0f;
    bool seekingMaximum = true;
    int candidate = globalMinimum;
    float candidatePosition = startPosition;
    float candidateValue = minimumValue;
    for (int step = 1; step <= uniquePointCount * 2; ++step)
    {
        const int unwrappedIndex = globalMinimum + step;
        const int index = unwrappedIndex % uniquePointCount;
        const float unwrappedPosition =
            positionAt(index)
            + static_cast<float>(unwrappedIndex / uniquePointCount);
        const float value = valueAtPoint(index);

        bool completedTurn = false;
        if (seekingMaximum)
        {
            if (value > candidateValue)
            {
                candidate = index;
                candidatePosition = unwrappedPosition;
                candidateValue = value;
            }
            else if (candidateValue - value >= turningThreshold)
            {
                completedTurn = true;
                seekingMaximum = false;
            }
        }
        else
        {
            if (value < candidateValue)
            {
                candidate = index;
                candidatePosition = unwrappedPosition;
                candidateValue = value;
            }
            else if (value - candidateValue >= turningThreshold)
            {
                completedTurn = true;
                seekingMaximum = true;
            }
        }

        if (! completedTurn)
            continue;

        if (candidatePosition >= targetPosition)
        {
            rightAnchor = candidate;
            rightPosition = candidatePosition;
            break;
        }
        leftAnchor = candidate;
        leftPosition = candidatePosition;
        candidate = index;
        candidatePosition = unwrappedPosition;
        candidateValue = value;
    }
    if (rightAnchor < 0 || leftAnchor == rightAnchor)
        return linear;

    const float anchorWidth = rightPosition - leftPosition;
    if (anchorWidth <= 0.0f)
        return linear;

    const float anchorProportion =
        std::clamp((targetPosition - leftPosition) / anchorWidth, 0.0f, 1.0f);
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
