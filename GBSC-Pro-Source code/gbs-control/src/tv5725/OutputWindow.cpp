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

OutputWindow::OutputWindow(uint16_t horizontalPicture, uint16_t verticalPicture,
                           const OutputTiming &raster,
                           uint16_t horizontalMargin, uint16_t verticalMargin)
{
    horizontal_ = solve(AxisHorizontal, horizontalPicture,
                        fitToRaster(AxisHorizontal, horizontalPicture,
                                    raster.horizontalTotal, raster.activeStart,
                                    raster.activeStop, horizontalMargin).scale(),
                        raster.horizontalTotal, raster.activeStart, raster.activeStop,
                        horizontalMargin);
    vertical_ = solve(AxisVertical, verticalPicture,
                      fitToRaster(AxisVertical, verticalPicture,
                                  raster.verticalTotal, raster.activeLinesStart,
                                  raster.activeLinesStop, verticalMargin).scale(),
                      raster.verticalTotal, raster.activeLinesStart,
                      raster.activeLinesStop, verticalMargin);
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
    // write origin and the capture's leading margin out of that same room --
    // produced = room x capture / (capture + startPerMag + captureMargin) -- so
    // the capture reaching the floor is that much larger. maximumCapture
    // charges it at the other end for the same reason.
    const float room = maxDisplayWindow(axis, rasterTotal, activeStart, activeStop);
    if (room <= 0.0f)
        return 0;
    const float charged = writeFloorBinds(axis, activeStart)
                        ? writeStart(axis).perMagnification
                              + (float)axis.captureMargin() : 0.0f;
    const float smallest = room * (float)Scale::Min / (float)Scale::Unity - charged;
    return smallest <= 0.0f ? 0 : (uint16_t)ceilf(smallest);
}

uint16_t OutputWindow::maximumCapture(const Axis &axis, uint16_t rasterTotal,
                                      uint16_t activeStart, uint16_t activeStop)
{
    // fitToRaster solves produced = room x capture / (capture + startPerMag +
    // captureMargin), so the scale it asks for is Unity x that sum over room.
    // The capture the room still holds is the largest that keeps it at or under
    // Scale::Max, and the write offset and the leading margin are charged
    // because they come out of the same room.
    const float room = maxDisplayWindow(axis, rasterTotal, activeStart, activeStop);
    const float largest = room * (float)Scale::Max / (float)Scale::Unity
                        - writeStart(axis).perMagnification
                        - (float)axis.captureMargin();
    return largest <= 0.0f ? 0 : (uint16_t)largest;
}

float OutputWindow::originOffset(const Axis &axis, float magnification)
{
    return writeStart(axis).constant + writeStart(axis).perMagnification * magnification;
}

uint16_t OutputWindow::marginOn(const Axis &axis, uint16_t margin)
{
    return margin == NominalMargin ? axis.captureMargin() : margin;
}

float OutputWindow::pictureOffset(const Axis &axis, float magnification,
                                  uint16_t margin)
{
    return originOffset(axis, magnification)
         + (float)marginOn(axis, margin) * magnification;
}

bool OutputWindow::writeFloorBinds(const Axis &axis, uint16_t activeStart)
{
    return (float)activeStart <= (float)writeStart(axis).floor + writeStart(axis).constant;
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
                                    uint16_t activeStop, uint16_t margin)
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
                        * capture / (capture + writeStart(axis).perMagnification
                                     + (float)marginOn(axis, margin));
    const float behindPorch = (float)farBound(rasterTotal, activeStop) - (float)activeStart;
    float produced = onFloor < behindPorch ? onFloor : behindPorch;
    if (produced > room)
        produced = room;
    // TO NEAREST, and the two directions are not equivalent: short of the room
    // is a black bar at the far edge, past it is picture the encoder never
    // carries, because the aperture cannot open past the front porch. Either
    // way the error belongs inside half a step -- about a pixel -- and biasing
    // it costs a whole source pixel off the far edge at a magnification near 2.
    // Measured at 800x600@60, where rounding down loses the card's frame.
    long scale = lrintf(Scale::Unity * capture / produced);
    if (scale < (long)Scale::Min)
        scale = Scale::Min;
    if (scale > Scale::Max)
        scale = Scale::Max;
    produced = capture * (float)Scale::Unity / scale;

    // A picture larger than solved for would eventually run off the END of the
    // line, where the ST registers wrap rather than clamp. Bounded at the
    // RASTER's last usable unit and not at the active window's: past activeStop
    // is the front porch, which the encoder discards, and the aperture in
    // solve() closes there anyway -- while one step of scale is produced /
    // scale of picture, 2.2 output rows at the bench 1080p framing.
    //
    // Bounded where placePicture PINS the picture rather than where it centres
    // it: a picture too big to centre lands on the write floor, and that is the
    // placement that can overrun.
    while (scale < Scale::Max
           && floorf(placementFloor(axis, pictureOffset(axis, (float)Scale::Unity / scale), activeStart)
                     + produced)
                  > (float)farBound(rasterTotal, 0)) {
        ++scale;
        produced = capture * (float)Scale::Unity / scale;
    }

    return RasterFit(Scale((uint16_t)scale), produced);
}

PictureOrigin OutputWindow::placePicture(const Axis &axis, float produced,
                                         uint16_t rasterTotal, float magnification,
                                         uint16_t activeStart, uint16_t margin)
{
    float offset = pictureOffset(axis, magnification, margin);
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
                                 uint16_t activeStop, uint16_t margin)
{
    OutputMapping solved;
    solved.scale_ = scale;
    solved.produced_ = scale.produced(capture);
    if (solved.produced_ <= 0.0f)
        return solved;

    PictureOrigin placed = placePicture(axis, solved.produced_, rasterTotal,
                                    scale.magnification(), activeStart, margin);
    // The front porch, or the raster's edge where no porch is known. ST registers
    // wrap rather than clamp, and a wrapped VDS_VB_ST rolls the frame.
    int32_t lastUsable = (int32_t)farBound(rasterTotal, activeStop);

    // Floor the WRITE, not the length: VDS_DIS_?B_ST is where blanking STARTS,
    // and the write runs from VDS_?B_SP + originOffset() for produced(), both
    // of them fractional. Flooring the length and adding a corner rounded on
    // its own lands up to a whole unit past the write, leaving the last unit of
    // the aperture showing memory nothing wrote.
    //
    // NOTHING IS HELD BACK HERE. The aperture closes on the write, so a framing
    // that fills the raster reaches it. Where the last shown unit needs a
    // sample the capture did not take -- the scaler interpolates between two
    // capture units, and the path drops the trailing margin -- the CAPTURE pays
    // for it by opening wider, which is what Axis::captureMargin is for. Giving
    // it back out of the aperture instead is a black bar no zoom can close.
    const float pictureEnds = (float)placed.windowStop()
                            + pictureOffset(axis, scale.magnification(), margin)
                            + solved.produced_;
    int32_t apertureStart = (int32_t)floorf(pictureEnds);
    if (apertureStart < placed.corner())
        apertureStart = placed.corner();
    if (apertureStart > lastUsable)
        apertureStart = lastUsable;

    // An even memory window shears the picture and an odd one is clean, so the
    // width is biased by a unit. Horizontal only, because VDS_VB_SP has never
    // been crept. docs/investigations/horizontal-scale-corruption.md
    //
    // BACKWARD, and it is the one pixel a full-screen picture gives away. The
    // bias used to step forward into the unit the aperture held back for the
    // interpolator; the aperture now closes on the write, so there is no unit
    // there to take and stepping forward would open the fetch past what the
    // write filled. The aperture follows it down, because the fetch has to
    // cover every column shown.
    //
    // Replacing it needs a knob on the produced width that does not move the
    // picture -- the input formatter's scaling-down DDA is one, and is unused.
    int32_t memoryStart = apertureStart;
    if (!axis.vertical() && (memoryStart - placed.windowStop()) % 2 == 0) {
        if (memoryStart > placed.corner())
            --memoryStart;
        else if (memoryStart < lastUsable)
            ++memoryStart;
    }
    if (apertureStart > memoryStart)
        apertureStart = memoryStart;

    // The near end opens ON the picture, both axes, for the reason the far end
    // closes on it: the first capture unit is only partly written, and what
    // pays for that is the capture opening a unit earlier, not the aperture
    // opening a unit later. Inset, it is a black column down the left of a
    // full-screen picture.
    //
    // FLOORED, like the far end, rather than taken from the rounded corner:
    // the write starts at a fraction of a pixel and the register is a whole
    // one, so rounding to nearest blanks a column the write had reached.
    int32_t displayStop = (int32_t)floorf((float)placed.windowStop()
                                          + pictureOffset(axis, scale.magnification(),
                                                          margin));
    if (displayStop < 0)
        displayStop = 0;
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
