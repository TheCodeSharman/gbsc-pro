#include "PanAndZoom.h"

#include <math.h>

#include "Scale.h"


namespace Tv5725 {

const float DefaultHActiveFraction = 0.76f;
const float DefaultVActiveFraction60Hz = 0.95f;
const float DefaultVActiveFraction50Hz = 0.82f;

const float OverCapture = 1.04f;

const uint16_t MinimumCapture = 16;

PanAndZoom::PanAndZoom()
    : horizontalOrigin_(0.0f), horizontalExtent_(0.0f),
      verticalOrigin_(0.0f), verticalExtent_(0.0f),
      horizontalTuned_(false), verticalTuned_(false) {}

PanAndZoom::PanAndZoom(float horizontalOrigin, float horizontalExtent,
                       float verticalOrigin, float verticalExtent)
    : horizontalOrigin_(horizontalOrigin), horizontalExtent_(horizontalExtent),
      verticalOrigin_(verticalOrigin), verticalExtent_(verticalExtent),
      horizontalTuned_(true), verticalTuned_(true) {}

bool PanAndZoom::tunedOn(const Axis &axis) const
{
    return axis.vertical() ? verticalTuned_ : horizontalTuned_;
}

void PanAndZoom::seedOn(const Axis &axis, float origin, float extent)
{
    if (axis.vertical()) {
        verticalOrigin_ = origin;
        verticalExtent_ = extent;
        verticalTuned_ = true;
    } else {
        horizontalOrigin_ = origin;
        horizontalExtent_ = extent;
        horizontalTuned_ = true;
    }
    clampOn(axis);
}

float PanAndZoom::originOn(const Axis &axis) const
{
    return axis.vertical() ? verticalOrigin_ : horizontalOrigin_;
}

float PanAndZoom::extentOn(const Axis &axis) const
{
    return axis.vertical() ? verticalExtent_ : horizontalExtent_;
}

float PanAndZoom::moved(float value, int16_t units, uint16_t usable)
{
    if (usable == 0)
        return value;
    // Back to the grid first: every value a control produces is a whole number
    // of units over `usable`, so out and back returns the same float.
    long onGrid = lrintf(value * (float)usable) + units;
    return (float)onGrid / (float)usable;
}

void PanAndZoom::zoomBy(const Axis &axis, int16_t units, uint16_t usable)
{
    if (units == 0)
        return;
    // Half of what the extent loses, so the centre stays put.
    int16_t half = (int16_t)(units / 2);
    if (axis.vertical()) {
        verticalExtent_ = moved(verticalExtent_, (int16_t)-units, usable);
        verticalOrigin_ = moved(verticalOrigin_, half, usable);
    } else {
        horizontalExtent_ = moved(horizontalExtent_, (int16_t)-units, usable);
        horizontalOrigin_ = moved(horizontalOrigin_, half, usable);
    }
    clampOn(axis);
}

void PanAndZoom::panBy(const Axis &axis, int16_t units, uint16_t usable)
{
    if (units == 0)
        return;
    if (axis.vertical())
        verticalOrigin_ = moved(verticalOrigin_, units, usable);
    else
        horizontalOrigin_ = moved(horizontalOrigin_, units, usable);
    clampOn(axis);
}

void PanAndZoom::clampOn(const Axis &axis)
{
    float &origin = axis.vertical() ? verticalOrigin_ : horizontalOrigin_;
    float &extent = axis.vertical() ? verticalExtent_ : horizontalExtent_;

    if (extent > 1.0f)
        extent = 1.0f;
    if (extent < 0.0f)
        extent = 0.0f;
    if (origin < 0.0f)
        origin = 0.0f;
    if (origin + extent > 1.0f)
        origin = 1.0f - extent;
}

void PanAndZoom::reset()
{
    horizontalOrigin_ = horizontalExtent_ = 0.0f;
    verticalOrigin_ = verticalExtent_ = 0.0f;
    horizontalTuned_ = verticalTuned_ = false;
}

bool PanAndZoom::operator==(const PanAndZoom &other) const
{
    return horizontalTuned_ == other.horizontalTuned_
        && verticalTuned_ == other.verticalTuned_
        && horizontalOrigin_ == other.horizontalOrigin_
        && horizontalExtent_ == other.horizontalExtent_
        && verticalOrigin_ == other.verticalOrigin_
        && verticalExtent_ == other.verticalExtent_;
}

bool PanAndZoom::operator!=(const PanAndZoom &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
