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

bool CaptureWindow::setSource(const SourceMeasurement &source,
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

const PanAndZoom &CaptureWindow::framing() const { return image_.framing(); }

const BlankingTiming &CaptureWindow::horizontal() const { return horizontal_; }

const BlankingTiming &CaptureWindow::vertical() const { return vertical_; }

const VideoSourceLine &CaptureWindow::horizontalLine() const { return horizontalLine_; }
const VideoSourceLine &CaptureWindow::verticalLine() const { return verticalLine_; }

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
    return axis.vertical() ? verticalLine_.lastCapture()
                           : horizontalLine_.lastCapture();
}

bool CaptureWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

}  // namespace Tv5725
