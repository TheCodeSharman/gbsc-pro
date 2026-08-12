#include "Axis.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(float startConst, float startPerMag, uint16_t windowStopMin,
           uint16_t margin, uint16_t scaleMin)
    : startConst_(startConst), startPerMag_(startPerMag),
      windowStopMin_(windowStopMin), margin_(margin), scaleMin_(scaleMin) {}

float Axis::startConst() const { return startConst_; }

float Axis::startPerMag() const { return startPerMag_; }

uint16_t Axis::windowStopMin() const { return windowStopMin_; }

uint16_t Axis::margin() const { return margin_; }

uint16_t Axis::scaleMin() const { return scaleMin_; }

uint16_t Axis::minimumCapture(uint16_t rasterTotal) const
{
    // produced = capture * Unity / scale, and the scale bottoms out at
    // scaleMin(). So the capture that still just fills the raster is
    // raster * scaleMin / Unity, rounded UP -- one unit short leaves a bar.
    uint32_t smallest = ((uint32_t)rasterTotal * scaleMin_ + Scale::Unity - 1)
                        / Scale::Unity;
    return (uint16_t)smallest;
}

float Axis::originOffset(float magnification) const
{
    return startConst_ + startPerMag_ * magnification;
}

float Axis::blankingEachEnd(uint16_t activeStart) const
{
    float floor = windowStopMin_ + startConst_;
    return (float)activeStart > floor ? (float)activeStart : floor;
}

float Axis::placementFloor(float offset, uint16_t activeStart) const
{
    float floor = windowStopMin_ + offset;
    return (float)activeStart > floor ? (float)activeStart : floor;
}

float Axis::maxDisplayWindow(uint16_t rasterTotal, uint16_t activeStart) const
{
    return (rasterTotal - 2) - 2.0f * blankingEachEnd(activeStart);
}

RasterFit Axis::fitToRaster(uint16_t capture, uint16_t rasterTotal,
                      uint16_t activeStart) const
{
    float room = maxDisplayWindow(rasterTotal, activeStart);
    if (capture == 0 || room <= 0.0f)
        return RasterFit(Scale(Scale::Max), 0.0f);

    float produced = room * capture / (capture + 2.0f * startPerMag_);
    long scale = lrintf(Scale::Unity * capture / produced);
    if (scale < (long)scaleMin_)
        scale = scaleMin_;
    if (scale > Scale::Max)
        scale = Scale::Max;
    produced = capture * (float)Scale::Unity / scale;

    // Rounding the scale down makes the picture a shade larger than solved
    // for, which can push the near edge below the write floor. A pixel of
    // unused raster costs nothing; an overflow shows scratch.
    while (scale < Scale::Max
           && lrintf((rasterTotal - produced) / 2.0f)
                  < placementFloor(originOffset((float)Scale::Unity / scale),
                                   activeStart)) {
        ++scale;
        produced = capture * (float)Scale::Unity / scale;
    }
    return RasterFit(Scale((uint16_t)scale), produced);
}


PictureOrigin Axis::placePicture(float produced, uint16_t rasterTotal,
                             float magnification, uint16_t activeStart) const
{
    float offset = originOffset(magnification);
    int32_t corner = lrintf((rasterTotal - produced) / 2.0f);
    int32_t windowStop = lrintf(corner - offset);
    if (windowStop < (int32_t)windowStopMin_) {
        windowStop = windowStopMin_;
        corner = lrintf(windowStop + offset);
    }

    // The back porch is applied ON TOP of the write floor rather than folded into
    // one max() with it. The two forms are measurably equivalent; this one leaves
    // activeStart = 0 as the previous behaviour by construction rather than by an
    // equivalence a reader has to re-derive. The drift test does not distinguish
    // them -- mutation-tested -- so a passing suite is not evidence for either.
    if (corner < (int32_t)activeStart) {
        corner = activeStart;
        windowStop = lrintf(corner - offset);
        if (windowStop < (int32_t)windowStopMin_)
            windowStop = windowStopMin_;
    }
    return PictureOrigin(corner, windowStop);
}

AxisSolution Axis::solve(uint16_t capture, Scale scale,
                         uint16_t rasterTotal, uint16_t activeStart) const
{
    AxisSolution solved;
    solved.produced_ = scale.produced(capture);
    if (solved.produced_ <= 0.0f)
        return solved;

    PictureOrigin placed = placePicture(solved.produced_, rasterTotal,
                                    scale.magnification(), activeStart);
    solved.origin_ = placed.corner();
    solved.windowStop_ = placed.windowStop();

    // ST registers must stay STRICTLY below the raster's total register,
    // and they wrap rather than clamp -- a wrapped VDS_VB_ST rolls the
    // frame. The total register is one below rasterTotal, so the last
    // usable is two below.
    int32_t lastUsable = (int32_t)rasterTotal - 2;

    // Floor, never round: VDS_DIS_?B_ST is where blanking STARTS, so
    // rounding up exposes unwritten memory as a band of scratch.
    solved.displayStop_ = solved.origin_;
    solved.displayStart_ =
        solved.displayStop_ + (int32_t)solved.produced_ - margin_;
    if (solved.displayStart_ > lastUsable)
        solved.displayStart_ = lastUsable;

    // The memory window IS the display window: allocate nothing spare. Memory
    // past the picture is memory the playback stage still walks, and taking the
    // whole raster showed on the bench as artefacts down the LEFT edge.
    solved.windowStart_ = solved.displayStart_;
    return solved;
}

const Axis AxisHorizontal(55.0f, 25.0f, 8, 2, 500);

const Axis AxisVertical(0.2f, 0.8f, 0, 3, Scale::Min);

}  // namespace Tv5725
