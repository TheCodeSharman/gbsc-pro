#include "CaptureWindow.h"

#include "../../gbs_types.h"
#include "MemoryMap.h"

namespace Tv5725 {

const uint16_t CaptureWindow::SourceVerticalTotalMin;
const uint16_t CaptureWindow::SourceVerticalTotalMax;
const uint16_t CaptureWindow::PalVerticalTotalMin;
const uint16_t CaptureWindow::ProgressiveStart;

CaptureWindow::CaptureWindow()
    : horizontalLine_(0), verticalLine_(0), linePx_(0), frameLines_(0) {}

bool CaptureWindow::readRasters(const SourceMeasurement &sampling, float fieldRateHz,
                                uint16_t sourceLines, uint16_t hsyncLow)
{
    linePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    frameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    const uint16_t horizontalWrap = sampling.ifLine() + 1;

    if (horizontalWrap < 64)
        return false;

    // **A MEASUREMENT IN RANGE IS NOT A MEASUREMENT THAT SETTLED**, and the
    // vertical axis is the one it fools: the horizontal line comes from the held
    // divider, while this is entirely 2 x (VTOTAL + 1). Sampled through a preset
    // load the count passes through 506, 251, 269, 259 and 511 -- all inside the
    // bounds a range check applies, and a solve that lands on one sizes the
    // vertical window for a frame the source is not sending. Having SUCCEEDED it
    // is never revisited.
    //
    // lineRateFrom() is the one owner of the settling rule already: the line
    // count picks the nominal rate, and the measured rate has to agree with it.
    if (SourceMeasurement::lineRateFrom(sourceLines, fieldRateHz) == 0)
        return false;

    horizontalLine_ = InputLine::measured(horizontalWrap, hsyncLow, sampling.divider());

    // IF_VB counts half-lines, so it rolls at twice the source frame.
    verticalLine_ = InputLine(2 * (sourceLines + 1));
    return true;
}

bool CaptureWindow::scaling() const { return linePx_ >= 64 && frameLines_ >= 64; }

void CaptureWindow::setFraming(const PanAndZoom &wanted, float fieldRateHz)
{
    framing_ = wanted;

    // Clamped before the windows are taken, so the framing kept is one these
    // bounds can realise. A press big enough to overshoot still moves the
    // window a unit or two, so it is accepted and the overshoot kept; every
    // smaller press back then produces an identical window and is reverted,
    // leaving the control dead in that direction. Only the hold ramp presses
    // that far -- measured pv -51 against a limit of -46, ph -144 against -134.
    framing_.clampToLine(horizontalLine_, fieldRateHz, false, linePx_);
    framing_.clampToLine(verticalLine_, fieldRateHz, true, frameLines_);

    horizontal_ = framing_.capture(horizontalLine_, fieldRateHz, false, linePx_);
    vertical_ = framing_.capture(verticalLine_, fieldRateHz, true, frameLines_);

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

const PanAndZoom &CaptureWindow::framing() const { return framing_; }

const BlankingTiming &CaptureWindow::horizontal() const { return horizontal_; }

const BlankingTiming &CaptureWindow::vertical() const { return vertical_; }

const InputLine &CaptureWindow::horizontalLine() const { return horizontalLine_; }

uint16_t CaptureWindow::linePx() const { return linePx_; }

uint16_t CaptureWindow::frameLines() const { return frameLines_; }

bool CaptureWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

}  // namespace Tv5725
