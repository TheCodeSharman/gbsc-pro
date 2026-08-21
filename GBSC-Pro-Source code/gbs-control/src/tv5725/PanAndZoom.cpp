#include "PanAndZoom.h"

#include "Scale.h"


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






int16_t PanAndZoom::zoomOn(const Axis &axis) const
{
    return axis.vertical() ? verticalZoom_ : horizontalZoom_;
}

int16_t PanAndZoom::panOn(const Axis &axis) const
{
    return axis.vertical() ? verticalPan_ : horizontalPan_;
}

void PanAndZoom::setZoomOn(const Axis &axis, int16_t units)
{
    if (axis.vertical())
        verticalZoom_ = units;
    else
        horizontalZoom_ = units;
}

void PanAndZoom::setPanOn(const Axis &axis, int16_t units)
{
    if (axis.vertical())
        verticalPan_ = units;
    else
        horizontalPan_ = units;
}

}  // namespace Tv5725
