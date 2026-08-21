#include "Axis.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(float startConst, float startPerMag, uint16_t windowStopMin,
           uint16_t margin, uint16_t scaleMin, uint16_t captureGranularity,
           float activeFraction50Hz, float activeFraction60Hz, bool vertical)
    : startConst_(startConst), startPerMag_(startPerMag),
      windowStopMin_(windowStopMin), margin_(margin), scaleMin_(scaleMin),
      captureGranularity_(captureGranularity),
      activeFraction50Hz_(activeFraction50Hz),
      activeFraction60Hz_(activeFraction60Hz), vertical_(vertical) {}

bool Axis::vertical() const { return vertical_; }

float Axis::activeFraction(float fieldRateHz) const
{
    return fieldRateHz < 55.0f ? activeFraction50Hz_ : activeFraction60Hz_;
}

float Axis::startConst() const { return startConst_; }

float Axis::startPerMag() const { return startPerMag_; }

uint16_t Axis::windowStopMin() const { return windowStopMin_; }

uint16_t Axis::margin() const { return margin_; }

uint16_t Axis::scaleMin() const { return scaleMin_; }

uint16_t Axis::captureGranularity() const { return captureGranularity_; }

int16_t Axis::stepUnits(int16_t pixels, float magnification) const
{
    float wanted = (pixels < 0 ? -pixels : pixels) / magnification;

    // Rounded ONCE, in output pixels. Rounding to units first and to granules
    // after biases every request upwards: 4.73 units becomes 5, then 6, when 4
    // is the nearer of the two the hardware can reach.
    long granules = lrintf(wanted / captureGranularity_);
    if (granules < 1)
        granules = 1;

    long units = granules * captureGranularity_;
    return pixels < 0 ? (int16_t)-units : (int16_t)units;
}

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

float Axis::blankingBeforePicture(uint16_t activeStart) const
{
    float floor = windowStopMin_ + startConst_;
    return (float)activeStart > floor ? (float)activeStart : floor;
}

float Axis::placementFloor(float offset, uint16_t activeStart) const
{
    float floor = windowStopMin_ + offset;
    return (float)activeStart > floor ? (float)activeStart : floor;
}

uint16_t Axis::farBound(uint16_t rasterTotal, uint16_t activeStop) const
{
    // rasterTotal - 2, not - 1: VDS_DIS_?B_ST must stay strictly below the total
    // register, which is itself one below rasterTotal.
    uint16_t edge = rasterTotal > 2 ? (uint16_t)(rasterTotal - 2) : 0;
    return activeStop > 0 && activeStop < edge ? activeStop : edge;
}

float Axis::maxDisplayWindow(uint16_t rasterTotal, uint16_t activeStart,
                             uint16_t activeStop) const
{
    return farBound(rasterTotal, activeStop) - blankingBeforePicture(activeStart);
}

RasterFit Axis::fitToRaster(uint16_t capture, uint16_t rasterTotal,
                      uint16_t activeStart, uint16_t activeStop) const
{
    float room = maxDisplayWindow(rasterTotal, activeStart, activeStop);
    if (capture == 0 || room <= 0.0f)
        return RasterFit(Scale(Scale::Max), 0.0f);

    float produced = room * capture / (capture + startPerMag_);
    long scale = lrintf(Scale::Unity * capture / produced);
    if (scale < (long)scaleMin_)
        scale = scaleMin_;
    if (scale > Scale::Max)
        scale = Scale::Max;
    produced = capture * (float)Scale::Unity / scale;

    // Rounding the scale down makes the picture a shade larger than solved for,
    // which would run it off the END of the line. Bounded where placePicture PINS
    // the picture rather than where it centres it: a picture too big to centre
    // lands on the write floor, and that is the placement that can overrun.
    while (scale < Scale::Max
           && placementFloor(originOffset((float)Scale::Unity / scale), activeStart)
                      + produced
                  > (float)farBound(rasterTotal, activeStop)) {
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

AxisSolution Axis::solve(uint16_t capture, Scale scale, uint16_t rasterTotal,
                         uint16_t activeStart, uint16_t activeStop) const
{
    AxisSolution solved;
    solved.produced_ = scale.produced(capture);
    if (solved.produced_ <= 0.0f)
        return solved;

    PictureOrigin placed = placePicture(solved.produced_, rasterTotal,
                                    scale.magnification(), activeStart);
    // The front porch, or the raster's edge where no porch is known. ST registers
    // wrap rather than clamp, and a wrapped VDS_VB_ST rolls the frame.
    int32_t lastUsable = (int32_t)farBound(rasterTotal, activeStop);

    // Floor, never round: VDS_DIS_?B_ST is where blanking STARTS, so
    // rounding up exposes unwritten memory as a band of scratch.
    int32_t displayStart = placed.corner() + (int32_t)solved.produced_ - margin_;
    if (displayStart > lastUsable)
        displayStart = lastUsable;
    solved.display_ = BlankingTiming(placed.corner(), displayStart);

    // The two windows share a far edge: allocate nothing spare. Memory past the
    // picture is memory the playback stage still walks, and taking the whole
    // raster showed on the bench as artefacts down the LEFT edge. The near edges
    // differ by the write origin, which is why the windows are not one thing.
    solved.memory_ = BlankingTiming(placed.windowStop(), displayStart);
    return solved;
}

const Axis AxisHorizontal(55.0f, 25.0f, 8, 2, Scale::Min, 2, 0.76f, 0.76f, false);

const Axis AxisVertical(0.2f, 0.8f, 0, 3, Scale::Min, 1, 0.82f, 0.95f, true);

}  // namespace Tv5725
