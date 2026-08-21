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

bool CaptureWindow::readRasters(const Sampling &sampling)
{
    linePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    frameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    const uint16_t horizontalWrap = sampling.ifLine() + 1;

    // How much of the line the hsync pulse takes is a property of the source, so
    // it is measured. Both are in ADC samples, the one space they share -- the
    // denominator is the divider, not STATUS_SYNC_PROC_HTOTAL, which only echoes
    // PLLAD_MD back.
    const uint16_t hlowLen = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
    const uint16_t sourceVerticalTotal = GBS::STATUS_SYNC_PROC_VTOTAL::read();

    if (horizontalWrap < 64 || sourceVerticalTotal < SourceVerticalTotalMin
        || sourceVerticalTotal > SourceVerticalTotalMax)
        return false;

    horizontalLine_ = InputLine::measured(horizontalWrap, hlowLen, sampling.divider());

    // IF_VB counts half-lines, so it rolls at twice the source frame.
    verticalLine_ = InputLine(2 * (sourceVerticalTotal + 1));
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
