#include "ActiveImage.h"

#include <math.h>

namespace Tv5725 {

ActiveImage::ActiveImage() {}

ActiveImage::ActiveImage(const PanAndZoom &framing) : framing_(framing) {}

const PanAndZoom &ActiveImage::framing() const { return framing_; }

void ActiveImage::setFraming(const PanAndZoom &framing) { framing_ = framing; }

bool ActiveImage::operator==(const ActiveImage &other) const
{
    return framing_ == other.framing_;
}

bool ActiveImage::operator!=(const ActiveImage &other) const
{
    return !(*this == other);
}

uint16_t ActiveImage::defaultWidth(const VideoSourceLine &line,
                                   const SourceTiming &timing, const Axis &axis)
{
    const float extent = timing.published() ? timing.activeExtent(axis)
                                            : axis.activeExtent();
    return (uint16_t)clampWidth(lrintf(line.units() * extent), line);
}

ActiveImage::Placement ActiveImage::place(const VideoSourceLine &line,
                                          const SourceTiming &timing,
                                          const Axis &axis) const
{
    uint16_t usable = line.units();
    long width, start;

    long wanted;
    if (framing_.tunedOn(axis) && usable > 0) {
        wanted = lrintf(framing_.extentOn(axis) * (float)usable);
        width = clampWidth(wanted, line);
        start = line.videoAt(framing_.originOn(axis));
    } else {
        // Nothing has framed this axis yet, so the computed default stands in
        // until the first solve seeds it. clampToLine() is where that happens.
        wanted = width = clampWidth((long)defaultWidth(line, timing, axis), line);
        const float from = timing.published() ? timing.activeStart(axis)
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

BlankingTiming ActiveImage::capture(const VideoSourceLine &line,
                                    const SourceTiming &timing,
                                    const Axis &axis) const
{
    if (line.units() == 0)
        return BlankingTiming();

    Placement placed = place(line, timing, axis);
    return BlankingTiming((uint16_t)placed.start,
                         (uint16_t)(placed.start + placed.width));
}

void ActiveImage::narrowTo(const Axis &axis, float extent)
{
    if (!framing_.tunedOn(axis) || framing_.extentOn(axis) <= extent)
        return;

    const float centre = framing_.originOn(axis) + framing_.extentOn(axis) / 2.0f;
    framing_.seedOn(axis, centre - extent / 2.0f, extent);
}

void ActiveImage::clampToLine(const VideoSourceLine &line, const SourceTiming &timing,
                              const Axis &axis)
{
    if (line.units() == 0)
        return;

    uint16_t usable = line.units();
    if (usable == 0)
        return;

    // Seeds an axis nobody has framed yet from the default it just placed, and
    // brings a framed one back only where a bound moved it. A framing this line
    // can realise is the user's and is left alone: rewriting it here puts it on
    // whichever grid the line offers, and that grid halves with the line
    // doubler.
    Placement placed = place(line, timing, axis);
    if (framing_.tunedOn(axis) && !placed.clamped)
        return;
    framing_.seedOn(axis, line.fractionAt((uint16_t)placed.start),
                    (float)placed.width / (float)usable);
}

long ActiveImage::clampWidth(long width, const VideoSourceLine &line)
{
    if (width > (long)line.maxCaptureWidth())
        width = line.maxCaptureWidth();
    return width < (long)MinimumCapture ? (long)MinimumCapture : width;
}

}  // namespace Tv5725
