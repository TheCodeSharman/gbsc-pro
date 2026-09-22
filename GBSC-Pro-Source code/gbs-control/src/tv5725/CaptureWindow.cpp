#include <math.h>
#include "CaptureWindow.h"

#include "../../gbs_types.h"
#include "Adc.h"
#include "InputFormatter.h"
#include "MemoryMap.h"

namespace Tv5725 {

const uint16_t CaptureWindow::ProgressiveStart;

CaptureWindow::CaptureWindow()
    : horizontalLine_(0), verticalLine_(0), timing_(0.0f) {}

bool CaptureWindow::readRasters(const SourceMeasurement &source,
                                const HsyncPulse &reading,
                                const SourceTiming &timing, bool lineDoubled)
{
    const uint16_t sourceLines = source.sourceLines();
    const uint16_t horizontalWrap = InputFormatter::lineCounterFor(Adc::dividerInForce(), lineDoubled) + 1;

    if (horizontalWrap < 64)
        return false;

    // **A MEASUREMENT IN RANGE IS NOT A MEASUREMENT THAT SETTLED**, and the
    // vertical axis is the one it fools: the horizontal line comes from the held
    // divider, while this is entirely the source's line count. Sampled through
    // a preset load the count passes 506, 251, 269, 259 and 511 -- all inside the
    // bounds a range check applies, and a solve that lands on one sizes the
    // vertical window for a frame the source is not sending. Having SUCCEEDED it
    // is never revisited.
    //
    // VideoSignal is the one owner of the bounds, on both the count and the
    // rate.
    if (!VideoSignal::isVideo(sourceLines, source.fieldRateHz()))
        return false;

    horizontalLine_ = VideoSourceLine::forDuty(horizontalWrap, reading, lineDoubled);

    // The IF's line counter runs at twice the source line rate only while the
    // line doubler is in the path, so what it counts is half-lines there and
    // whole source lines otherwise. docs/scaler-geometry-model.md
    verticalLine_ = VideoSourceLine::frame(lineDoubled ? 2 * (sourceLines + 1)
                                                       : sourceLines + 1);

    timing_ = timing;
    return true;
}

void CaptureWindow::setRasters(uint16_t linePx, uint16_t frameLines,
                               uint16_t activeStop, uint16_t activeLinesStop,
                               uint16_t activeStart, uint16_t activeLinesStart)
{
    line_ = OutputRaster(linePx, activeStop, activeStart);
    frame_ = OutputRaster(frameLines, activeLinesStop, activeLinesStart);
}

bool CaptureWindow::scaling() const
{
    return line_.total() >= 64 && frame_.total() >= 64;
}

void CaptureWindow::setFraming(const PanAndZoom &wanted)
{
    image_.setFraming(wanted);

    // Clamped before the windows are taken, so the framing kept is one these
    // bounds can realise. A press big enough to overshoot still moves the
    // window a unit or two, so it is accepted and the overshoot kept; every
    // smaller press back then produces an identical window and is reverted,
    // leaving the control dead in that direction. Only the hold ramp presses
    // that far -- measured pv -51 against a limit of -46, ph -144 against -134.
    image_.clampToLine(horizontalLine_, timing_, AxisHorizontal);
    image_.clampToLine(verticalLine_, timing_, AxisVertical);

    clampToRaster(horizontalLine_, line_, AxisHorizontal);
    clampToRaster(verticalLine_, frame_, AxisVertical);

    horizontal_ = image_.capture(horizontalLine_, timing_, AxisHorizontal);
    vertical_ = image_.capture(verticalLine_, timing_, AxisVertical);

    // A pixel costs one 32-bit word, and a line wide enough to overrun the
    // capture buffer is reachable because the width is in ADC samples and
    // PLLAD_MD is 12 bits. The chip answers an overrun by wrapping, putting a
    // wrong address on screen and reporting nothing, so the window is narrowed
    // rather than refused -- for the reason the framing is clamped rather than
    // rejected, a dead picture with no way back being the worse failure.
    const uint16_t fits = MemoryMap::clampWidth(horizontal_.width(), vertical_.width());
    if (fits < horizontal_.width())
        horizontal_ = BlankingTiming(horizontal_.stop(), horizontal_.stop() + fits);
}

void CaptureWindow::clampToRaster(const VideoSourceLine &line,
                                  const OutputRaster &raster, const Axis &axis)
{
    // The bound is a capture WIDTH, so it is compared against what the line can
    // realise; the proportion it becomes is of the whole line, which is what the
    // framing is anchored to.
    const uint16_t reachable = line.capturable();
    const uint16_t whole = line.units();
    if (!raster.solved() || whole == 0)
        return;

    const uint16_t most = axis.maximumCapture(raster.total(), raster.activeStart(),
                                             raster.activeStop());
    if (most == 0 || most >= reachable)
        return;

    image_.narrowTo(axis, (float)most / (float)whole);
}

const PanAndZoom &CaptureWindow::framing() const { return image_.framing(); }

const BlankingTiming &CaptureWindow::horizontal() const { return horizontal_; }

const BlankingTiming &CaptureWindow::vertical() const { return vertical_; }

const VideoSourceLine &CaptureWindow::horizontalLine() const { return horizontalLine_; }

uint16_t CaptureWindow::lineUnitsOn(const Axis &axis) const
{
    return axis.vertical() ? verticalLine_.units() : horizontalLine_.units();
}

uint16_t CaptureWindow::firstUnitOn(const Axis &axis) const
{
    return axis.vertical() ? verticalLine_.firstCapture()
                           : horizontalLine_.firstCapture();
}

uint16_t CaptureWindow::reachOn(const Axis &axis) const
{
    return axis.vertical() ? verticalLine_.lastReachable()
                           : horizontalLine_.lastReachable();
}

int16_t CaptureWindow::videoLagOn(const Axis &axis) const
{
    return (int16_t)lrintf(axis.vertical() ? verticalLine_.videoLag()
                                           : horizontalLine_.videoLag());
}

uint16_t CaptureWindow::linePx() const { return line_.total(); }

uint16_t CaptureWindow::frameLines() const { return frame_.total(); }

bool CaptureWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

}  // namespace Tv5725
