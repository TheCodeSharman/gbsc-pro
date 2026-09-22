#include "PanAndZoom.h"

#include <math.h>

#include "Scale.h"


namespace Tv5725 {

const uint16_t MinimumCapture = 16;

PanAndZoom::PanAndZoom() { reset(); }

PanAndZoom::PanAndZoom(float horizontalOrigin, float horizontalExtent,
                       float verticalOrigin, float verticalExtent)
{
    horizontal_.origin = horizontalOrigin;
    horizontal_.extent = horizontalExtent;
    horizontal_.tuned = true;
    vertical_.origin = verticalOrigin;
    vertical_.extent = verticalExtent;
    vertical_.tuned = true;
}

PanAndZoom::AxisFraming &PanAndZoom::on(const Axis &axis)
{
    return axis.vertical() ? vertical_ : horizontal_;
}

const PanAndZoom::AxisFraming &PanAndZoom::on(const Axis &axis) const
{
    return axis.vertical() ? vertical_ : horizontal_;
}

bool PanAndZoom::tunedOn(const Axis &axis) const { return on(axis).tuned; }

float PanAndZoom::originOn(const Axis &axis) const { return on(axis).origin; }

float PanAndZoom::extentOn(const Axis &axis) const { return on(axis).extent; }

void PanAndZoom::seedOn(const Axis &axis, float origin, float extent)
{
    AxisFraming &framing = on(axis);
    framing.origin = origin;
    framing.extent = extent;
    framing.tuned = true;
    clampSeed(framing);
}

float PanAndZoom::moved(float value, int16_t units, uint16_t usable)
{
    if (usable == 0)
        return value;
    // Moved by exactly `units`, and not re-gridded on the way. A proportion
    // names a position in the SOURCE where the window register is a position in
    // the COUNTER, so the grid a solve seeds this on is the counter's offset by
    // the capture lag, which is no whole number of units. Rounding that offset
    // away can land the step back on the unit it started from, and
    // VideoPath::step() reverts a framing that moved no register -- so the
    // control dies rather than coarsens. docs/known-issues.md
    return value + (float)units / (float)usable;
}

float PanAndZoom::limitFor(uint16_t reach, uint16_t usable)
{
    if (reach == 0 || usable == 0 || reach >= usable)
        return 1.0f;
    return (float)reach / (float)usable;
}

void PanAndZoom::zoomBy(const Axis &axis, int16_t units, uint16_t usable,
                        uint16_t reach, uint16_t narrowest)
{
    if (units == 0)
        return;
    AxisFraming &framing = on(axis);
    const float before = framing.extent;
    framing.extent = moved(framing.extent, (int16_t)-units, usable);
    clampExtent(framing, limitFor(reach, usable));

    if (narrowest == 0 || usable == 0)
        return;
    // A framing already below the stop -- saved under a narrower raster -- is
    // left where it is rather than widened, so the press moves nothing instead
    // of moving the wrong way.
    const float least = (float)narrowest / (float)usable;
    if (framing.extent < least)
        framing.extent = before < least ? before : least;
}

void PanAndZoom::panBy(const Axis &axis, int16_t units, uint16_t usable,
                       uint16_t reach)
{
    if (units == 0)
        return;
    AxisFraming &framing = on(axis);
    framing.origin = moved(framing.origin, units, usable);
    clampOrigin(framing, limitFor(reach, usable));
}

void PanAndZoom::clampOrigin(AxisFraming &framing, float limit)
{
    const float furthest = limit - framing.extent;

    if (framing.origin > furthest)
        framing.origin = furthest;
    if (framing.origin < 0.0f)
        framing.origin = 0.0f;
}

void PanAndZoom::clampExtent(AxisFraming &framing, float limit)
{
    const float widest = limit - framing.origin;

    if (framing.extent > widest)
        framing.extent = widest;
    if (framing.extent < 0.0f)
        framing.extent = 0.0f;
}

void PanAndZoom::clampSeed(AxisFraming &framing)
{
    if (framing.extent > 1.0f)
        framing.extent = 1.0f;
    if (framing.extent < 0.0f)
        framing.extent = 0.0f;
    clampOrigin(framing);
}

void PanAndZoom::reset()
{
    horizontal_.origin = horizontal_.extent = 0.0f;
    vertical_.origin = vertical_.extent = 0.0f;
    horizontal_.tuned = vertical_.tuned = false;
}

bool PanAndZoom::same(const AxisFraming &a, const AxisFraming &b)
{
    return a.tuned == b.tuned && a.origin == b.origin && a.extent == b.extent;
}

bool PanAndZoom::operator==(const PanAndZoom &other) const
{
    return same(horizontal_, other.horizontal_) && same(vertical_, other.vertical_);
}

bool PanAndZoom::operator!=(const PanAndZoom &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
