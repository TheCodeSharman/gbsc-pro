#include "ActiveImage.h"

#include <math.h>

namespace Tv5725 {

ActiveImage::ActiveImage() {}

ActiveImage::ActiveImage(const PanAndZoom &framing) : framing_(framing) {}

const PanAndZoom &ActiveImage::framing() const { return framing_; }

void ActiveImage::setFraming(const PanAndZoom &framing) { framing_ = framing; }

void ActiveImage::panBy(const InputLine &line, const SourceTiming &timing,
                        const Axis &axis, int16_t units, const OutputRaster &raster)
{
    if (!framing_.tunedOn(axis))
        clampToLine(line, timing, axis, raster);
    framing_.panBy(axis, units, line.capturable());
}

void ActiveImage::zoomBy(const InputLine &line, const SourceTiming &timing,
                         const Axis &axis, int16_t units, const OutputRaster &raster)
{
    if (!framing_.tunedOn(axis))
        clampToLine(line, timing, axis, raster);
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

uint16_t ActiveImage::defaultWidth(const InputLine &line,
                                   const SourceTiming &timing, const Axis &axis)
{
    const float extent = timing.published() ? timing.activeExtent(axis)
                                            : axis.activeExtent();
    // No raster here: the DEFAULT width is a property of the line alone, and
    // the scale floor is applied by the callers that know the raster.
    return (uint16_t)clampWidth(lrintf(line.units() * extent), line, 0, axis);
}

ActiveImage::Placement ActiveImage::place(const InputLine &line,
                                          const SourceTiming &timing,
                                          const Axis &axis,
                                          const OutputRaster &raster) const
{
    uint16_t usable = line.capturable();
    long width, start;

    if (framing_.tunedOn(axis) && usable > 0) {
        width = clampWidth(lrintf(framing_.extentOn(axis) * (float)usable), line,
                           raster, axis);
        start = (long)line.firstCapture()
              + lrintf(framing_.originOn(axis) * (float)usable);
    } else {
        // Nothing has framed this axis yet, so the computed default stands in
        // until the first solve seeds it. clampToLine() is where that happens.
        width = clampWidth((long)defaultWidth(line, timing, axis), line,
                           raster, axis);
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

BlankingTiming ActiveImage::capture(const InputLine &line,
                                    const SourceTiming &timing,
                                    const Axis &axis, const OutputRaster &raster) const
{
    if (line.units() == 0)
        return BlankingTiming();

    Placement placed = place(line, timing, axis, raster);
    return BlankingTiming((uint16_t)placed.start,
                         (uint16_t)(placed.start + placed.width));
}

void ActiveImage::clampToLine(const InputLine &line, const SourceTiming &timing,
                              const Axis &axis, const OutputRaster &raster)
{
    if (line.units() == 0)
        return;

    uint16_t usable = line.capturable();
    if (usable == 0)
        return;

    // Seeds an axis nobody has framed yet from the default it just placed, and
    // brings a framed one back to what this line can realise. Both are the same
    // write, because the placement is the answer either way.
    Placement placed = place(line, timing, axis, raster);
    framing_.seedOn(axis,
                    (float)(placed.start - (long)line.firstCapture()) / (float)usable,
                    (float)placed.width / (float)usable);
}

long ActiveImage::clampWidth(long width, const InputLine &line,
                             const OutputRaster &raster, const Axis &axis)
{
    if (width > (long)line.capturable())
        width = line.capturable();

    // The part cannot minify, so a capture the room cannot hold is not shown
    // smaller -- its far end is simply not drawn.
    if (raster.solved()) {
        const long ceiling =
            (long)axis.maximumCapture(raster.total(), raster.activeStop());
        if (ceiling > 0 && width > ceiling)
            width = ceiling;
    }

    // Two floors, and the scale's is usually the higher. MinimumCapture stops
    // the control cropping to nothing; Axis::minimumCapture stops it cropping
    // past what the magnification can put back, which without it letterboxes
    // instead of stopping. Measured: a 1126 vertical raster floors at 282, and
    // 282 is exactly the last capture that filled the screen.
    long floor = (long)MinimumCapture;
    if (raster.solved()) {
        long scaleFloor = (long)axis.minimumCapture(raster.total());
        if (scaleFloor > floor)
            floor = scaleFloor;
    }


    return width < floor ? floor : width;
}

}  // namespace Tv5725
