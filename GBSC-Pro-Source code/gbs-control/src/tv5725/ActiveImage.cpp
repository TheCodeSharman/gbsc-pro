#include "ActiveImage.h"

#include <math.h>

namespace Tv5725 {

ActiveImage::ActiveImage() {}

ActiveImage::ActiveImage(const PanAndZoom &framing) : framing_(framing) {}

const PanAndZoom &ActiveImage::framing() const { return framing_; }

void ActiveImage::setFraming(const PanAndZoom &framing) { framing_ = framing; }

void ActiveImage::panBy(const VideoSourceLine &line, const SourceTiming &timing,
                        const Axis &axis, int16_t units)
{
    if (!framing_.tunedOn(axis))
        clampToLine(line, timing, axis);
    framing_.panBy(axis, units, line.capturable());
}

void ActiveImage::zoomBy(const VideoSourceLine &line, const SourceTiming &timing,
                         const Axis &axis, int16_t units)
{
    if (!framing_.tunedOn(axis))
        clampToLine(line, timing, axis);
    framing_.zoomBy(axis, units, line.capturable());
}

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
    uint16_t usable = line.capturable();
    long width, start;

    if (framing_.tunedOn(axis) && usable > 0) {
        width = clampWidth(lrintf(framing_.extentOn(axis) * (float)usable), line);
        start = (long)line.firstCapture()
              + lrintf(framing_.originOn(axis) * (float)usable);
    } else {
        // Nothing has framed this axis yet, so the computed default stands in
        // until the first solve seeds it. clampToLine() is where that happens.
        width = clampWidth((long)defaultWidth(line, timing, axis), line);
        const float from = timing.published() ? timing.activeStart(axis)
                                              : axis.activeStart();
        start = lrintf(from * (float)line.units());
    }

    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - width)
        start = (long)line.lastCapture() - width;

    Placement placed = {width, start};
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

void ActiveImage::clampToLine(const VideoSourceLine &line, const SourceTiming &timing,
                              const Axis &axis)
{
    if (line.units() == 0)
        return;

    uint16_t usable = line.capturable();
    if (usable == 0)
        return;

    // Seeds an axis nobody has framed yet from the default it just placed, and
    // brings a framed one back to what this line can realise. Both are the same
    // write, because the placement is the answer either way.
    Placement placed = place(line, timing, axis);
    framing_.seedOn(axis,
                    (float)(placed.start - (long)line.firstCapture()) / (float)usable,
                    (float)placed.width / (float)usable);
}

long ActiveImage::clampWidth(long width, const VideoSourceLine &line)
{
    if (width > (long)line.capturable())
        width = line.capturable();
    return width < (long)MinimumCapture ? (long)MinimumCapture : width;
}

}  // namespace Tv5725
