#include "Axis.h"

#include <math.h>

namespace Tv5725 {

Axis::Axis(float startConst, float startPerMag, uint16_t windowStopMin,
           uint16_t captureGranularity,
           float activeStart, float activeExtent, bool vertical)
    : startConst_(startConst), startPerMag_(startPerMag),
      windowStopMin_(windowStopMin),
      captureGranularity_(captureGranularity),
      activeStart_(activeStart), activeExtent_(activeExtent),
      vertical_(vertical) {}

bool Axis::vertical() const { return vertical_; }

float Axis::activeStart() const { return activeStart_; }

float Axis::activeExtent() const { return activeExtent_; }

float Axis::startConst() const { return startConst_; }

float Axis::startPerMag() const { return startPerMag_; }

uint16_t Axis::windowStopMin() const { return windowStopMin_; }

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

uint16_t Axis::minimumCapture(uint16_t rasterTotal, uint16_t activeStart,
                              uint16_t activeStop) const
{
    // produced = capture x Unity / scale, and the scale bottoms out at
    // Scale::Min, so the capture that reaches the floor is room x Min / Unity
    // -- rounded UP, one unit short leaving a bar.
    //
    // Where the WRITE FLOOR binds rather than the porch, fitToRaster takes the
    // write origin out of that same room -- produced = room x capture /
    // (capture + startPerMag) -- so the capture reaching the floor is that much
    // larger. maximumCapture charges it at the other end for the same reason.
    const float room = maxDisplayWindow(rasterTotal, activeStart, activeStop);
    if (room <= 0.0f)
        return 0;
    const float charged = writeFloorBinds(activeStart) ? startPerMag_ : 0.0f;
    const float smallest = room * (float)Scale::Min / (float)Scale::Unity - charged;
    return smallest <= 0.0f ? 0 : (uint16_t)ceilf(smallest);
}

uint16_t Axis::maximumCapture(uint16_t rasterTotal, uint16_t activeStop) const
{
    // fitToRaster solves produced = room x capture / (capture + startPerMag),
    // so the scale it asks for is Unity x (capture + startPerMag) / room. The
    // capture the room still holds is the largest that keeps that at or under
    // Scale::Max, and the write offset is charged because it comes out of the
    // same room.
    const float room = maxDisplayWindow(rasterTotal, 0, activeStop);
    const float largest = room * (float)Scale::Max / (float)Scale::Unity
                        - startPerMag_;
    return largest <= 0.0f ? 0 : (uint16_t)largest;
}

float Axis::originOffset(float magnification) const
{
    return startConst_ + startPerMag_ * magnification;
}

bool Axis::writeFloorBinds(uint16_t activeStart) const
{
    return (float)activeStart <= (float)windowStopMin_ + startConst_;
}

float Axis::blankingBeforePicture(uint16_t activeStart) const
{
    return writeFloorBinds(activeStart) ? (float)windowStopMin_ + startConst_
                                        : (float)activeStart;
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

    // Where the picture starts is the LATER of the output mode's back porch and
    // the write floor plus the origin, and which one binds decides whether the
    // origin comes out of the picture. On the write floor it does, and the
    // solve for it is the standing one -- produced + originOffset(produced /
    // capture) = room. Behind a back porch wide enough to hold the origin it
    // does NOT: the memory window opens inside the porch, the picture still
    // starts at activeStart, and charging the origin again leaves the picture
    // short of the far bound by it.
    const float onFloor = (farBound(rasterTotal, activeStop)
                           - (float)windowStopMin_ - startConst_)
                        * capture / (capture + startPerMag_);
    const float behindPorch = (float)farBound(rasterTotal, activeStop) - (float)activeStart;
    float produced = onFloor < behindPorch ? onFloor : behindPorch;
    if (produced > room)
        produced = room;
    long scale = lrintf(Scale::Unity * capture / produced);
    if (scale < (long)Scale::Min)
        scale = Scale::Min;
    if (scale > Scale::Max)
        scale = Scale::Max;
    produced = capture * (float)Scale::Unity / scale;

    // Rounding the scale down makes the picture a shade larger than solved for,
    // which would run it off the END of the line. Bounded where placePicture PINS
    // the picture rather than where it centres it: a picture too big to centre
    // lands on the write floor, and that is the placement that can overrun.
    //
    // Measured in WHOLE units, because solve() closes the display window on the
    // floor of where the write ends: an overshoot inside the last unit is
    // blanked there and shows as nothing. One step of scale is produced / scale
    // of picture -- 2.37 lines at the bench 1080p framing -- so bumping for a
    // fraction of a unit pays lines to save a quarter of one.
    while (scale < Scale::Max
           && floorf(placementFloor(originOffset((float)Scale::Unity / scale), activeStart)
                     + produced)
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

    // Floor the WRITE, not the length: VDS_DIS_?B_ST is where blanking STARTS,
    // and the write runs from VDS_?B_SP + originOffset() for produced(), both
    // of them fractional. Flooring the length and adding a corner rounded on
    // its own lands up to a whole unit past the write, leaving the last unit of
    // the aperture showing memory nothing wrote.
    //
    // The write is usable one capture unit's worth of output short of where it
    // ends: the scaler interpolates between two capture units, so the output
    // unit landing on the last one written reads the one after it, which no
    // capture filled. docs/known-issues.md
    const float writeEnds = (float)placed.windowStop()
                          + originOffset(scale.magnification())
                          + solved.produced_ - scale.magnification();
    int32_t apertureStart = (int32_t)floorf(writeEnds);
    if (apertureStart < placed.corner())
        apertureStart = placed.corner();
    if (apertureStart > lastUsable)
        apertureStart = lastUsable;

    // An even memory window shears the picture and an odd one is clean, so the
    // width is biased by a unit, FORWARD: the fetch reaching one further costs
    // nothing, where stepping back short-changes it. Horizontal only, because
    // VDS_VB_SP has never been crept.
    // docs/investigations/the-shear-follows-the-produced-widths-parity.md
    //
    // THE APERTURE DOES NOT FOLLOW IT. Moving both far edges together puts the
    // last shown column one past the interpolator's reach, which is a column of
    // junk down the right-hand edge; blanking it costs no picture, because that
    // column was never captured. So the two windows differ at the far end by
    // the bias, and the memory window is the wider of the two -- the safe
    // direction, since the fetch then covers every column the aperture shows.
    int32_t memoryStart = apertureStart;
    if (!vertical() && (memoryStart - placed.windowStop()) % 2 == 0) {
        if (memoryStart < lastUsable)
            ++memoryStart;
        else if (memoryStart > placed.corner())
            --memoryStart;
    }
    if (apertureStart > memoryStart)
        apertureStart = memoryStart;

    // The near end mirrors the far one. The write origin marks where content
    // first appears, which is the first unit the capture only PARTLY filled --
    // it was measured by creeping until the picture started. One capture unit
    // later is the first unit fully written, and an aperture opening before it
    // shows memory the previous mode left behind.
    // docs/investigations/moving-write-origin.md
    int32_t displayStop = (int32_t)ceilf((float)placed.windowStop()
                                         + originOffset(scale.magnification())
                                         + scale.magnification());
    if (displayStop < placed.corner())
        displayStop = placed.corner();
    if (displayStop > apertureStart)
        displayStop = apertureStart;

    solved.display_ = BlankingTiming(displayStop, apertureStart);

    // Allocate nothing spare beyond the bias: memory past the picture is memory
    // the playback stage still walks, and taking the whole raster showed on the
    // bench as artefacts down the LEFT edge. The near edges differ by the write
    // origin and the far ones by the parity unit, which is why the windows are
    // not one thing.
    solved.memory_ = BlankingTiming(placed.windowStop(), memoryStart);
    return solved;
}

const Axis AxisHorizontal(55.0f, 25.0f, 8, 2, 0.117f, 0.864f, false);

const Axis AxisVertical(0.2f, 0.8f, 0, 1, 0.061f, 0.933f, true);

}  // namespace Tv5725
