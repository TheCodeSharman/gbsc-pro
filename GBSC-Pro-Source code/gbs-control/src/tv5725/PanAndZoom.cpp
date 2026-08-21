#include "PanAndZoom.h"

#include "Scale.h"

#include <math.h>

namespace Tv5725 {

const float DefaultHActiveFraction = 0.76f;
const float DefaultVActiveFraction60Hz = 0.95f;
const float DefaultVActiveFraction50Hz = 0.82f;

const float OverCapture = 1.04f;

const uint16_t MinimumCapture = 16;

PanAndZoom::PanAndZoom() : horizontalZoom_(0), verticalZoom_(0), horizontalPan_(0), verticalPan_(0) {}

PanAndZoom::PanAndZoom(int16_t horizontalZoom, int16_t verticalZoom, int16_t horizontalPan, int16_t verticalPan)
    : horizontalZoom_(horizontalZoom), verticalZoom_(verticalZoom), horizontalPan_(horizontalPan), verticalPan_(verticalPan) {}

int16_t PanAndZoom::horizontalZoom() const { return horizontalZoom_; }

int16_t PanAndZoom::verticalZoom() const { return verticalZoom_; }

int16_t PanAndZoom::horizontalPan() const { return horizontalPan_; }

int16_t PanAndZoom::verticalPan() const { return verticalPan_; }

void PanAndZoom::setHorizontalZoom(int16_t units) { horizontalZoom_ = units; }

void PanAndZoom::setVerticalZoom(int16_t units) { verticalZoom_ = units; }

void PanAndZoom::setHorizontalPan(int16_t units) { horizontalPan_ = units; }

void PanAndZoom::setVerticalPan(int16_t units) { verticalPan_ = units; }

void PanAndZoom::zoomBy(int16_t dh, int16_t dv) { horizontalZoom_ += dh; verticalZoom_ += dv; }

void PanAndZoom::panBy(int16_t dx, int16_t dy) { horizontalPan_ += dx; verticalPan_ += dy; }

void PanAndZoom::reset() { horizontalZoom_ = verticalZoom_ = horizontalPan_ = verticalPan_ = 0; }

bool PanAndZoom::operator==(const PanAndZoom &other) const
{
    return horizontalZoom_ == other.horizontalZoom_ && verticalZoom_ == other.verticalZoom_
        && horizontalPan_ == other.horizontalPan_ && verticalPan_ == other.verticalPan_;
}

bool PanAndZoom::operator!=(const PanAndZoom &other) const
{
    return !(*this == other);
}

uint16_t PanAndZoom::defaultWidth(const InputLine &line, float fieldRateHz,
                               bool vertical)
{
    float fraction = DefaultHActiveFraction;
    if (vertical)
        fraction = fieldRateHz < 55.0f ? DefaultVActiveFraction50Hz
                                       : DefaultVActiveFraction60Hz;
    // No raster here: the DEFAULT width is a property of the line alone, and
    // the scale floor is applied by the callers that know the raster.
    return (uint16_t)clampWidth(
        lrintf(line.units() * fraction * OverCapture), line, 0,
        vertical ? AxisVertical : AxisHorizontal);
}

PanAndZoom::Placement PanAndZoom::place(const InputLine &line, float fieldRateHz,
                                       bool vertical, uint16_t rasterTotal) const
{
    int16_t zoomUnits = vertical ? verticalZoom_ : horizontalZoom_;
    int16_t pan = vertical ? verticalPan_ : horizontalPan_;

    long width = clampWidth(
        (long)defaultWidth(line, fieldRateHz, vertical) - zoomUnits, line,
        rasterTotal, vertical ? AxisVertical : AxisHorizontal);

    long start = (long)(line.units() - width) / 2 + pan;
    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - width)
        start = (long)line.lastCapture() - width;

    Placement placed = {width, start};
    return placed;
}

BlankingTiming PanAndZoom::capture(const InputLine &line, float fieldRateHz,
                        bool vertical, uint16_t rasterTotal) const
{
    if (line.units() == 0)
        return BlankingTiming();

    Placement placed = place(line, fieldRateHz, vertical, rasterTotal);
    return BlankingTiming((uint16_t)placed.start,
                         (uint16_t)(placed.start + placed.width));
}

void PanAndZoom::clampToLine(const InputLine &line, float fieldRateHz, bool vertical,
                          uint16_t rasterTotal)
{
    if (line.units() == 0)
        return;

    Placement placed = place(line, fieldRateHz, vertical, rasterTotal);

    int16_t &zoomUnits = vertical ? verticalZoom_ : horizontalZoom_;
    int16_t &pan = vertical ? verticalPan_ : horizontalPan_;

    zoomUnits = (int16_t)((long)defaultWidth(line, fieldRateHz, vertical)
                          - placed.width);
    pan = (int16_t)(placed.start - (long)(line.units() - placed.width) / 2);
}

long PanAndZoom::clampWidth(long width, const InputLine &line, uint16_t rasterTotal,
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
