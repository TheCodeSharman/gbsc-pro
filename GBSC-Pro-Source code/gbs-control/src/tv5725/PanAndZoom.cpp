#include "PanAndZoom.h"

#include <math.h>

namespace Tv5725 {

const float DefaultHActiveFraction = 0.76f;
const float DefaultVActiveFraction60Hz = 0.95f;
const float DefaultVActiveFraction50Hz = 0.82f;

const float OverCapture = 1.04f;

const uint16_t MinimumCapture = 16;

PanAndZoom::PanAndZoom() : zoomH_(0), zoomV_(0), panH_(0), panV_(0) {}

PanAndZoom::PanAndZoom(int16_t zoomH, int16_t zoomV, int16_t panH, int16_t panV)
    : zoomH_(zoomH), zoomV_(zoomV), panH_(panH), panV_(panV) {}

int16_t PanAndZoom::zoomH() const { return zoomH_; }

int16_t PanAndZoom::zoomV() const { return zoomV_; }

int16_t PanAndZoom::panH() const { return panH_; }

int16_t PanAndZoom::panV() const { return panV_; }

void PanAndZoom::setZoomH(int16_t units) { zoomH_ = units; }

void PanAndZoom::setZoomV(int16_t units) { zoomV_ = units; }

void PanAndZoom::setPanH(int16_t units) { panH_ = units; }

void PanAndZoom::setPanV(int16_t units) { panV_ = units; }

void PanAndZoom::zoomBy(int16_t dh, int16_t dv) { zoomH_ += dh; zoomV_ += dv; }

void PanAndZoom::panBy(int16_t dx, int16_t dy) { panH_ += dx; panV_ += dy; }

void PanAndZoom::reset() { zoomH_ = zoomV_ = panH_ = panV_ = 0; }

bool PanAndZoom::operator==(const PanAndZoom &other) const
{
    return zoomH_ == other.zoomH_ && zoomV_ == other.zoomV_
        && panH_ == other.panH_ && panV_ == other.panV_;
}

bool PanAndZoom::operator!=(const PanAndZoom &other) const
{
    return !(*this == other);
}

uint16_t PanAndZoom::defaultWidth(uint16_t units, float fieldRateHz,
                               bool vertical)
{
    float fraction = DefaultHActiveFraction;
    if (vertical)
        fraction = fieldRateHz < 55.0f ? DefaultVActiveFraction50Hz
                                       : DefaultVActiveFraction60Hz;
    return (uint16_t)clampWidth(lrintf(units * fraction * OverCapture),
                                units);
}

CaptureWindow PanAndZoom::capture(uint16_t units, float fieldRateHz,
                        bool vertical) const
{
    if (units == 0)
        return CaptureWindow();

    // The capture STOP may never reach `units`, only units - 1. That value
    // is the wrap point -- IF_VB_ST rolls at 2 x (VTOTAL + 1) and IF_HB_ST2
    // at IF_HSYNC_RST + 1 -- and a window written onto it does not clamp,
    // it rolls, which reads as the picture jumping rather than as a capture
    // fault. geometry_math.py is the reference and has always had it, as
    // `wrap_at - 1` in scale_step() and pan_capture() alike.
    uint16_t span = units - 1;

    int16_t zoomUnits = vertical ? zoomV_ : zoomH_;
    int16_t pan = vertical ? panV_ : panH_;

    long width = clampWidth(
        (long)defaultWidth(units, fieldRateHz, vertical) - zoomUnits, span);

    long start = (long)(units - width) / 2 + pan;
    if (start < 0)
        start = 0;
    if (start > (long)span - width)
        start = (long)span - width;
    return CaptureWindow((uint16_t)start, (uint16_t)(start + width));
}

long PanAndZoom::clampWidth(long width, uint16_t units)
{
    if (width > (long)units)
        return units;
    if (width < (long)MinimumCapture)
        return MinimumCapture;
    return width;
}

}  // namespace Tv5725
