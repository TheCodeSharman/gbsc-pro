#include "ActiveImage.h"

#include <math.h>

namespace Tv5725 {

ActiveImage::ActiveImage() {}

ActiveImage::ActiveImage(const PanAndZoom &framing) : framing_(framing) {}

const PanAndZoom &ActiveImage::framing() const { return framing_; }

void ActiveImage::setFraming(const PanAndZoom &framing) { framing_ = framing; }

void ActiveImage::panBy(int16_t dx, int16_t dy) { framing_.panBy(dx, dy); }

void ActiveImage::zoomBy(int16_t dh, int16_t dv) { framing_.zoomBy(dh, dv); }

int16_t ActiveImage::horizontalZoom() const { return framing_.horizontalZoom(); }

int16_t ActiveImage::verticalZoom() const { return framing_.verticalZoom(); }

int16_t ActiveImage::horizontalPan() const { return framing_.horizontalPan(); }

int16_t ActiveImage::verticalPan() const { return framing_.verticalPan(); }

bool ActiveImage::operator==(const ActiveImage &other) const
{
    return framing_ == other.framing_;
}

bool ActiveImage::operator!=(const ActiveImage &other) const
{
    return !(*this == other);
}

uint16_t ActiveImage::defaultWidth(const InputLine &line, float fieldRateHz,
                               const Axis &axis)
{
    float fraction = axis.activeFraction(fieldRateHz);
    // No raster here: the DEFAULT width is a property of the line alone, and
    // the scale floor is applied by the callers that know the raster.
    return (uint16_t)clampWidth(
        lrintf(line.units() * fraction * OverCapture), line, 0,
        axis);
}

ActiveImage::Placement ActiveImage::place(const InputLine &line, float fieldRateHz,
                                       const Axis &axis, uint16_t rasterTotal) const
{
    int16_t zoomUnits = framing_.zoomOn(axis);
    int16_t pan = framing_.panOn(axis);

    long width = clampWidth(
        (long)defaultWidth(line, fieldRateHz, axis) - zoomUnits, line,
        rasterTotal, axis);

    long start = (long)(line.units() - width) / 2 + pan;
    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - width)
        start = (long)line.lastCapture() - width;

    Placement placed = {width, start};
    return placed;
}

BlankingTiming ActiveImage::capture(const InputLine &line, float fieldRateHz,
                        const Axis &axis, uint16_t rasterTotal) const
{
    if (line.units() == 0)
        return BlankingTiming();

    Placement placed = place(line, fieldRateHz, axis, rasterTotal);
    return BlankingTiming((uint16_t)placed.start,
                         (uint16_t)(placed.start + placed.width));
}

void ActiveImage::clampToLine(const InputLine &line, float fieldRateHz, const Axis &axis,
                          uint16_t rasterTotal)
{
    if (line.units() == 0)
        return;

    Placement placed = place(line, fieldRateHz, axis, rasterTotal);

    int16_t zoomUnits = (int16_t)((long)defaultWidth(line, fieldRateHz, axis)
                                  - placed.width);
    int16_t pan = (int16_t)(placed.start - (long)(line.units() - placed.width) / 2);

    framing_.setZoomOn(axis, zoomUnits);
    framing_.setPanOn(axis, pan);
}

long ActiveImage::clampWidth(long width, const InputLine &line, uint16_t rasterTotal,
                         const Axis &axis)
{
    if (width > (long)line.capturable())
        return line.capturable();

    // Two floors, and the scale's is usually the higher. MinimumCapture stops
    // the control cropping to nothing; Axis::minimumCapture stops it cropping
    // past what the magnification can put back, which without it letterboxes
    // instead of stopping. Measured: a 1126 vertical raster floors at 282, and
    // 282 is exactly the last capture that filled the screen.
    long floor = (long)MinimumCapture;
    if (rasterTotal > 0) {
        long scaleFloor = (long)axis.minimumCapture(rasterTotal);
        if (scaleFloor > floor)
            floor = scaleFloor;
    }


    return width < floor ? floor : width;
}

}  // namespace Tv5725
