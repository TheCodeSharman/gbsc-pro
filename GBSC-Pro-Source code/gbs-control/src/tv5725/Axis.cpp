#include "Axis.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(float startConst, float startPerMag, uint16_t windowStopMin,
           uint16_t margin)
    : startConst_(startConst), startPerMag_(startPerMag),
      windowStopMin_(windowStopMin), margin_(margin) {}

float Axis::startConst() const { return startConst_; }

float Axis::startPerMag() const { return startPerMag_; }

uint16_t Axis::windowStopMin() const { return windowStopMin_; }

uint16_t Axis::margin() const { return margin_; }

float Axis::originOffset(float magnification) const
{
    return startConst_ + startPerMag_ * magnification;
}

float Axis::maxDisplayWindow(uint16_t rasterTotal) const
{
    return (rasterTotal - 2) - 2.0f * (windowStopMin_ + startConst_);
}

RasterFit Axis::fitToRaster(uint16_t capture, uint16_t rasterTotal) const
{
    float room = maxDisplayWindow(rasterTotal);
    if (capture == 0 || room <= 0.0f)
        return RasterFit(Scale(Scale::Max), 0.0f);

    float produced = room * capture / (capture + 2.0f * startPerMag_);
    long scale = lrintf(Scale::Unity * capture / produced);
    if (scale < Scale::Min)
        scale = Scale::Min;
    if (scale > Scale::Max)
        scale = Scale::Max;
    produced = capture * (float)Scale::Unity / scale;

    // Rounding the scale down makes the picture a shade larger than solved
    // for, which can push the near edge below the write floor. A pixel of
    // unused raster costs nothing; an overflow shows scratch.
    while (scale < Scale::Max
           && lrintf((rasterTotal - produced) / 2.0f)
                  < windowStopMin_
                        + originOffset((float)Scale::Unity / scale)) {
        ++scale;
        produced = capture * (float)Scale::Unity / scale;
    }
    return RasterFit(Scale((uint16_t)scale), produced);
}

PictureOrigin Axis::placePicture(float produced, uint16_t rasterTotal,
                             float magnification) const
{
    float offset = originOffset(magnification);
    int32_t corner = lrintf((rasterTotal - produced) / 2.0f);
    int32_t windowStop = lrintf(corner - offset);
    if (windowStop < (int32_t)windowStopMin_) {
        windowStop = windowStopMin_;
        corner = lrintf(windowStop + offset);
    }
    return PictureOrigin(corner, windowStop);
}

AxisSolution Axis::solve(uint16_t capture, Scale scale,
                         uint16_t rasterTotal) const
{
    AxisSolution solved;
    solved.produced_ = scale.produced(capture);
    if (solved.produced_ <= 0.0f)
        return solved;

    PictureOrigin placed = placePicture(solved.produced_, rasterTotal,
                                    scale.magnification());
    solved.origin_ = placed.corner();
    solved.windowStop_ = placed.windowStop();

    // ST registers must stay STRICTLY below the raster's total register,
    // and they wrap rather than clamp -- a wrapped VDS_VB_ST rolls the
    // frame. The total register is one below rasterTotal, so the last
    // usable is two below. The window takes all of it; no margin reserved.
    int32_t lastUsable = (int32_t)rasterTotal - 2;
    solved.windowStart_ = lastUsable;

    // Floor, never round: VDS_DIS_?B_ST is where blanking STARTS, so
    // rounding up exposes unwritten memory as a band of scratch.
    solved.displayStop_ = solved.origin_;
    solved.displayStart_ =
        solved.displayStop_ + (int32_t)solved.produced_ - margin_;
    if (solved.displayStart_ > lastUsable)
        solved.displayStart_ = lastUsable;
    return solved;
}

const Axis AxisHorizontal(55.0f, 25.0f, 8, 2);

const Axis AxisVertical(0.2f, 0.8f, 0, 3);

}  // namespace Tv5725
