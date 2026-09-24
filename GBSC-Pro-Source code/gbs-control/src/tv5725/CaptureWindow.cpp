#include <math.h>
#include "CaptureWindow.h"

#include "MemoryMap.h"

namespace Tv5725 {

const uint16_t CaptureWindow::ProgressiveStart;

CaptureWindow::CaptureWindow()
    : horizontalLine_(0), verticalLine_(0), timing_(0.0f) {}

CaptureWindow::CaptureWindow(const VideoSourceLine &line, const VideoSourceLine &frame,
                             const SourceTiming &timing)
    : horizontalLine_(line), verticalLine_(frame), timing_(timing) {}

void CaptureWindow::setFraming(const PanAndZoom &wanted)
{
    framing_ = wanted;

    clampFramingTo(AxisHorizontal);
    clampFramingTo(AxisVertical);

    horizontal_ = captureOn(AxisHorizontal);
    vertical_ = captureOn(AxisVertical);

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

const VideoSourceLine &CaptureWindow::horizontalLine() const { return horizontalLine_; }
const VideoSourceLine &CaptureWindow::verticalLine() const { return verticalLine_; }

const VideoSourceLine &CaptureWindow::lineOn(const Axis &axis) const
{
    return axis.vertical() ? verticalLine_ : horizontalLine_;
}

uint16_t CaptureWindow::lineUnitsOn(const Axis &axis) const
{
    return lineOn(axis).units();
}

uint16_t CaptureWindow::firstUnitOn(const Axis &axis) const
{
    return lineOn(axis).firstCapture();
}

uint16_t CaptureWindow::reachOn(const Axis &axis) const
{
    return lineOn(axis).lastCapture();
}

bool CaptureWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

uint16_t CaptureWindow::defaultWidth(const VideoSourceLine &line,
                                     const SourceTiming &timing, const Axis &axis)
{
    const float extent = timing.published() ? timing.activeExtent(axis)
                                            : axis.activeExtent();
    return (uint16_t)clampWidth(lrintf(line.units() * extent), line);
}

CaptureWindow::Placement CaptureWindow::place(const Axis &axis) const
{
    const VideoSourceLine &line = lineOn(axis);
    uint16_t usable = line.units();
    long width, start;

    long wanted;
    if (framing_.tunedOn(axis) && usable > 0) {
        wanted = lrintf(framing_.extentOn(axis) * (float)usable);
        width = clampWidth(wanted, line);
        start = line.videoAt(framing_.originOn(axis));
    } else {
        // Nothing has framed this axis yet, so the computed default stands in
        // until the first solve seeds it. clampFramingTo() is where that happens.
        wanted = width = clampWidth((long)defaultWidth(line, timing_, axis), line);
        const float from = timing_.published() ? timing_.activeStart(axis)
                                               : axis.activeStart();
        // The standard states that position in ITS line. This one is counted
        // from whichever sync edge the chip triggered on, and carries video a
        // lag behind it.
        start = line.videoAt(from);
    }

    // The near edge is the pan's and the far edge is the zoom's, so each is
    // given back by the control that owns it: a window running past the end of
    // the line gives up WIDTH and leaves the start where the pan put it. Moving
    // the start instead shifts the picture on a zoom, which the zoom must never
    // do. The start still stops short of the end, or a pan far enough right
    // would leave no window at all.
    const long asked = start, wide = width;
    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - (long)MinimumCapture)
        start = (long)line.lastCapture() - (long)MinimumCapture;
    if (start + width > (long)line.lastCapture())
        width = (long)line.lastCapture() - start;

    Placement placed = {width, start, width != wanted || width != wide
                                      || start != asked};
    return placed;
}

BlankingTiming CaptureWindow::captureOn(const Axis &axis) const
{
    const VideoSourceLine &line = lineOn(axis);
    if (line.units() == 0)
        return BlankingTiming();

    // The axis's own margin at each end, because the capture path drops that
    // much at each end: a window opened on the picture loses the source's first
    // and last lines. Horizontally the margin is 0 and the near edge is already
    // crept against corruption, so this is the vertical pair in practice.
    //
    // The near end floors at the counter's origin -- a window already there has
    // nowhere to open into -- and the far end is clamped to the last unit
    // before the counter wraps, because a margin past it rolls the frame.
    Placement placed = place(axis);
    const long margin = axis.captureMargin();
    const long near = placed.start > margin ? placed.start - margin : 0;
    const long far = placed.start + placed.width + margin;
    const long last = line.lastCapture();
    return BlankingTiming((uint16_t)near, (uint16_t)(far < last ? far : last));
}

void CaptureWindow::clampFramingTo(const Axis &axis)
{
    const VideoSourceLine &line = lineOn(axis);
    uint16_t usable = line.units();
    if (usable == 0)
        return;

    // Seeds an axis nobody has framed yet from the default it just placed, and
    // brings a framed one back only where a bound moved it. A framing this line
    // can realise is the user's and is left alone: rewriting it here puts it on
    // whichever grid the line offers, and that grid halves with the line
    // doubler.
    Placement placed = place(axis);
    if (framing_.tunedOn(axis) && !placed.clamped)
        return;
    framing_.seedOn(axis, line.fractionAt((uint16_t)placed.start),
                    (float)placed.width / (float)usable);
}

long CaptureWindow::clampWidth(long width, const VideoSourceLine &line)
{
    if (width > (long)line.maxCaptureWidth())
        width = line.maxCaptureWidth();
    return width < (long)MinimumCapture ? (long)MinimumCapture : width;
}

}  // namespace Tv5725
