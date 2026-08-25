#include "CaptureWindow.h"

#include "../../gbs_types.h"
#include "MemoryMap.h"

namespace Tv5725 {

const uint16_t CaptureWindow::SourceVerticalTotalMin;
const uint16_t CaptureWindow::SourceVerticalTotalMax;
const uint16_t CaptureWindow::ProgressiveStart;

CaptureWindow::CaptureWindow()
    : horizontalLine_(0), verticalLine_(0), timing_(0.0f) {}

bool CaptureWindow::readRasters(const SourceMeasurement &source, uint16_t hsyncLow)
{
    const uint16_t sourceLines = source.sourceLines();
    const uint16_t horizontalWrap = source.ifLine() + 1;

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
    // lineRateFrom() is the one owner of the bounds already, on both the count
    // and the rate.
    if (SourceMeasurement::lineRateFrom(sourceLines, source.fieldRateHz()) == 0)
        return false;

    horizontalLine_ = InputLine::measured(horizontalWrap, hsyncLow, source.divider());

    // The IF's line counter runs at twice the source line rate only while the
    // line doubler is in the path, so what it counts is half-lines there and
    // whole source lines otherwise. docs/scaler-geometry-model.md
    verticalLine_ = InputLine(source.lineDoubled() ? 2 * (sourceLines + 1)
                                                   : sourceLines + 1);

    timing_ = SourceTiming::matching(sourceLines, source.fieldRateHz(),
                                     (float)hsyncLow / (float)source.divider());
    return true;
}

void CaptureWindow::setRasters(uint16_t linePx, uint16_t frameLines,
                               uint16_t activeStop, uint16_t activeLinesStop)
{
    line_ = OutputRaster(linePx, activeStop);
    frame_ = OutputRaster(frameLines, activeLinesStop);
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
    image_.clampToLine(horizontalLine_, timing_, AxisHorizontal, line_);
    image_.clampToLine(verticalLine_, timing_, AxisVertical, frame_);

    horizontal_ = image_.capture(horizontalLine_, timing_, AxisHorizontal, line_);
    vertical_ = image_.capture(verticalLine_, timing_, AxisVertical, frame_);

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

const PanAndZoom &CaptureWindow::framing() const { return image_.framing(); }

const BlankingTiming &CaptureWindow::horizontal() const { return horizontal_; }

const BlankingTiming &CaptureWindow::vertical() const { return vertical_; }

const InputLine &CaptureWindow::horizontalLine() const { return horizontalLine_; }

uint16_t CaptureWindow::capturableOn(const Axis &axis) const
{
    return axis.vertical() ? verticalLine_.capturable() : horizontalLine_.capturable();
}

uint16_t CaptureWindow::linePx() const { return line_.total(); }

uint16_t CaptureWindow::frameLines() const { return frame_.total(); }

bool CaptureWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

}  // namespace Tv5725
