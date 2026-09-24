#include "OutputWindow.h"

#include <math.h>

namespace Tv5725 {

const OutputWindow::WriteStart &OutputWindow::writeStart(const Axis &axis)
{
    static const WriteStart Horizontal = {55.0f, 25.0f, 8};
    static const WriteStart Vertical   = {0.2f, 0.8f, 0};
    return axis.vertical() ? Vertical : Horizontal;
}

uint16_t OutputWindow::totalOn(const Axis &axis, const OutputTiming &raster)
{
    return axis.vertical() ? raster.verticalTotal : raster.horizontalTotal;
}

uint16_t OutputWindow::activeStartOn(const Axis &axis, const OutputTiming &raster)
{
    return axis.vertical() ? raster.activeLinesStart : raster.activeStart;
}

uint16_t OutputWindow::activeStopOn(const Axis &axis, const OutputTiming &raster)
{
    return axis.vertical() ? raster.activeLinesStop : raster.activeStop;
}

uint16_t OutputWindow::narrowestCapture(const Axis &axis, const OutputTiming &raster)
{
    if (totalOn(axis, raster) == 0)
        return 0;
    return minimumCapture(axis, totalOn(axis, raster), activeStartOn(axis, raster),
                          activeStopOn(axis, raster));
}

uint16_t OutputWindow::widestCapture(const Axis &axis, const OutputTiming &raster)
{
    if (totalOn(axis, raster) == 0)
        return 0;
    return maximumCapture(axis, totalOn(axis, raster), activeStartOn(axis, raster),
                          activeStopOn(axis, raster));
}

OutputWindow::OutputWindow() {}

OutputWindow::OutputWindow(uint16_t horizontalCapture, uint16_t verticalCapture,
                           const OutputTiming &raster)
{
    horizontal_ = solve(AxisHorizontal, horizontalCapture,
                        fitToRaster(AxisHorizontal, horizontalCapture,
                                    raster.horizontalTotal, raster.activeStart,
                                    raster.activeStop).scale(),
                        raster.horizontalTotal, raster.activeStart, raster.activeStop);
    vertical_ = solve(AxisVertical, verticalCapture,
                      fitToRaster(AxisVertical, verticalCapture,
                                  raster.verticalTotal, raster.activeLinesStart,
                                  raster.activeLinesStop).scale(),
                      raster.verticalTotal, raster.activeLinesStart,
                      raster.activeLinesStop);
}

const OutputMapping &OutputWindow::horizontal() const { return horizontal_; }

const OutputMapping &OutputWindow::vertical() const { return vertical_; }

bool OutputWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

uint16_t OutputWindow::minimumCapture(const Axis &axis, uint16_t rasterTotal,
                                      uint16_t activeStart, uint16_t activeStop)
{
    // produced = capture x Unity / scale, and the scale bottoms out at
    // Scale::Min, so the capture that reaches the floor is room x Min / Unity
    // -- rounded UP, one unit short leaving a bar.
    //
    // Where the WRITE FLOOR binds rather than the porch, fitToRaster takes the
    // write origin out of that same room -- produced = room x capture /
    // (capture + startPerMag) -- so the capture reaching the floor is that much
    // larger. maximumCapture charges it at the other end for the same reason.
    const float room = maxDisplayWindow(axis, rasterTotal, activeStart, activeStop);
    if (room <= 0.0f)
        return 0;
    const float charged = writeFloorBinds(axis, activeStart)
                        ? writeStart(axis).perMagnification : 0.0f;
    const float smallest = room * (float)Scale::Min / (float)Scale::Unity - charged;
    return smallest <= 0.0f ? 0 : (uint16_t)ceilf(smallest);
}

uint16_t OutputWindow::maximumCapture(const Axis &axis, uint16_t rasterTotal,
                                      uint16_t activeStart, uint16_t activeStop)
{
    // fitToRaster solves produced = room x capture / (capture + startPerMag),
    // so the scale it asks for is Unity x (capture + startPerMag) / room. The
    // capture the room still holds is the largest that keeps that at or under
    // Scale::Max, and the write offset is charged because it comes out of the
    // same room.
    const float room = maxDisplayWindow(axis, rasterTotal, activeStart, activeStop);
    const float largest = room * (float)Scale::Max / (float)Scale::Unity
                        - writeStart(axis).perMagnification;
    return largest <= 0.0f ? 0 : (uint16_t)largest;
}

float OutputWindow::originOffset(const Axis &axis, float magnification)
{
    return writeStart(axis).constant + writeStart(axis).perMagnification * magnification;
}

bool OutputWindow::writeFloorBinds(const Axis &axis, uint16_t activeStart)
{
    return (float)activeStart < (float)writeStart(axis).floor + writeStart(axis).constant;
}

float OutputWindow::blankingBeforePicture(const Axis &axis, uint16_t activeStart)
{
    return writeFloorBinds(axis, activeStart)
               ? (float)writeStart(axis).floor + writeStart(axis).constant
                                              : (float)activeStart;
}

float OutputWindow::placementFloor(const Axis &axis, float offset, uint16_t activeStart)
{
    float floor = writeStart(axis).floor + offset;
    return (float)activeStart > floor ? (float)activeStart : floor;
}

uint16_t OutputWindow::farBound(uint16_t rasterTotal, uint16_t activeStop)
{
    // rasterTotal - 2, not - 1: VDS_DIS_?B_ST must stay strictly below the total
    // register, which is itself one below rasterTotal.
    uint16_t edge = rasterTotal > 2 ? (uint16_t)(rasterTotal - 2) : 0;
    return activeStop > 0 && activeStop < edge ? activeStop : edge;
}

float OutputWindow::maxDisplayWindow(const Axis &axis, uint16_t rasterTotal,
                                     uint16_t activeStart, uint16_t activeStop)
{
    return farBound(rasterTotal, activeStop) - blankingBeforePicture(axis, activeStart);
}

RasterFit OutputWindow::fitToRaster(const Axis &axis, uint16_t capture,
                                    uint16_t rasterTotal, uint16_t activeStart,
                                    uint16_t activeStop)
{
    float room = maxDisplayWindow(axis, rasterTotal, activeStart, activeStop);
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
                           - (float)writeStart(axis).floor - writeStart(axis).constant)
                        * capture / (capture + writeStart(axis).perMagnification);
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
           && floorf(placementFloor(axis, originOffset(axis, (float)Scale::Unity / scale), activeStart)
                     + produced)
                  > (float)farBound(rasterTotal, activeStop)) {
        ++scale;
        produced = capture * (float)Scale::Unity / scale;
    }

    return RasterFit(Scale((uint16_t)scale), produced);
}

PictureOrigin OutputWindow::placePicture(const Axis &axis, float produced,
                                         uint16_t rasterTotal, float magnification,
                                         uint16_t activeStart)
{
    float offset = originOffset(axis, magnification);
    int32_t corner = lrintf((rasterTotal - produced) / 2.0f);
    int32_t windowStop = lrintf(corner - offset);
    if (windowStop < (int32_t)writeStart(axis).floor) {
        windowStop = writeStart(axis).floor;
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
        if (windowStop < (int32_t)writeStart(axis).floor)
            windowStop = writeStart(axis).floor;
    }
    return PictureOrigin(corner, windowStop);
}

OutputMapping OutputWindow::solve(const Axis &axis, uint16_t capture, Scale scale,
                                 uint16_t rasterTotal, uint16_t activeStart,
                                 uint16_t activeStop)
{
    OutputMapping solved;
    solved.scale_ = scale;
    solved.produced_ = scale.produced(capture);
    if (solved.produced_ <= 0.0f)
        return solved;

    PictureOrigin placed = placePicture(axis, solved.produced_, rasterTotal,
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
    // HORIZONTALLY the write is usable one capture unit's worth of output short
    // of where it ends: the scaler interpolates between two capture units, so
    // the output unit landing on the last one written reads the one after it,
    // which no capture filled.
    //
    // VERTICALLY THE INTERPOLATOR GIVES NOTHING BACK, MEASURED. Crept at
    // 800x600@60 into Mode960p with the source's last picture line as the
    // capture's last unit, the picture extends a row per step out to the far
    // bound with the falloff keeping its shape and no row of stale memory at
    // any of them. docs/known-issues.md
    //
    // THE TRAILING MARGIN IS A SEPARATE SUBTRACTION and is not that. `produced`
    // is the whole capture window scaled, margin and all, because that is what
    // the hardware plays out -- but the path drops the last margin units, so
    // the WRITE stops that much sooner and an aperture closing on `produced`
    // shows rows nothing wrote. Measured at 800x600@60: the aperture solved at
    // 998 carries two rows of stale memory under the source's last line, and
    // 996 is clean with that line intact.
    const float interpolatorReach = axis.vertical() ? 0.0f : scale.magnification();
    const float marginReach = (float)axis.captureMargin() * scale.magnification();
    const float writeEnds = (float)placed.windowStop()
                          + originOffset(axis, scale.magnification())
                          + solved.produced_ - interpolatorReach - marginReach;
    int32_t apertureStart = (int32_t)floorf(writeEnds);
    if (apertureStart < placed.corner())
        apertureStart = placed.corner();
    if (apertureStart > lastUsable)
        apertureStart = lastUsable;

    // An even memory window shears the picture and an odd one is clean, so the
    // width is biased by a unit, FORWARD: the fetch reaching one further costs
    // nothing, where stepping back short-changes it. Horizontal only, because
    // VDS_VB_SP has never been crept.
    // docs/investigations/horizontal-scale-corruption.md
    //
    // THE APERTURE DOES NOT FOLLOW IT. Moving both far edges together puts the
    // last shown column one past the interpolator's reach, which is a column of
    // junk down the right-hand edge; blanking it costs no picture, because that
    // column was never captured. So the two windows differ at the far end by
    // the bias, and the memory window is the wider of the two -- the safe
    // direction, since the fetch then covers every column the aperture shows.
    int32_t memoryStart = apertureStart;
    if (!axis.vertical() && (memoryStart - placed.windowStop()) % 2 == 0) {
        if (memoryStart < lastUsable)
            ++memoryStart;
        else if (memoryStart > placed.corner())
            --memoryStart;
    }
    if (apertureStart > memoryStart)
        apertureStart = memoryStart;

    // The near end mirrors the far one, HORIZONTALLY. The write origin marks
    // where content first appears, which is the first unit the capture only
    // PARTLY filled -- it was measured by creeping until the picture started.
    // One capture unit later is the first unit fully written, and an aperture
    // opening before it shows memory the previous mode left behind.
    // docs/investigations/moving-write-origin.md
    //
    // Vertically the aperture opens ON the picture. Reading before the first
    // written LINE reaches past the start of the frame and comes back as
    // nothing, where reading before the first written COLUMN reaches the
    // previous line's storage -- so the unit buys nothing here, and the picture
    // is placed on the output mode's first active line, which is the first line
    // the panel paints. An inset there is a black bar across the top of the
    // screen rather than overscan.
    // docs/investigations/the-aperture-is-inset-one-capture-unit-at-each-end.md
    int32_t displayStop = placed.corner();
    if (!axis.vertical()) {
        displayStop = (int32_t)ceilf((float)placed.windowStop()
                                     + originOffset(axis, scale.magnification())
                                     + scale.magnification());
        if (displayStop < placed.corner())
            displayStop = placed.corner();
    }
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

}  // namespace Tv5725
