#include "PanAndZoom.h"

#include <math.h>

namespace Tv5725 {

const float DefaultHActiveFraction = 0.76f;
const float DefaultVActiveFraction60Hz = 0.95f;
const float DefaultVActiveFraction50Hz = 0.82f;

const float OverCapture = 1.04f;

const uint16_t MinimumCapture = 16;

PanAndZoom::PanAndZoom() : zoomH_(0), zoomV_(0), panH_(0), panV_(0) {}

PanAndZoom::PanAndZoom(int16_t zoomH, int16_t zoomV, int16_t panH, int16_t panV)
    : zoomH_(zoomH), zoomV_(zoomV), panH_(panH), panV_(panV) {}

int16_t PanAndZoom::zoomH() const { return zoomH_; }

int16_t PanAndZoom::zoomV() const { return zoomV_; }

int16_t PanAndZoom::panH() const { return panH_; }

int16_t PanAndZoom::panV() const { return panV_; }

void PanAndZoom::setZoomH(int16_t units) { zoomH_ = units; }

void PanAndZoom::setZoomV(int16_t units) { zoomV_ = units; }

void PanAndZoom::setPanH(int16_t units) { panH_ = units; }

void PanAndZoom::setPanV(int16_t units) { panV_ = units; }

void PanAndZoom::zoomBy(int16_t dh, int16_t dv) { zoomH_ += dh; zoomV_ += dv; }

void PanAndZoom::panBy(int16_t dx, int16_t dy) { panH_ += dx; panV_ += dy; }

void PanAndZoom::reset() { zoomH_ = zoomV_ = panH_ = panV_ = 0; }

bool PanAndZoom::operator==(const PanAndZoom &other) const
{
    return zoomH_ == other.zoomH_ && zoomV_ == other.zoomV_
        && panH_ == other.panH_ && panV_ == other.panV_;
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
    return (uint16_t)clampWidth(
        lrintf(line.units() * fraction * OverCapture), line);
}

CaptureWindow PanAndZoom::capture(const InputLine &line, float fieldRateHz,
                        bool vertical) const
{
    if (line.units() == 0)
        return CaptureWindow();

    int16_t zoomUnits = vertical ? zoomV_ : zoomH_;
    int16_t pan = vertical ? panV_ : panH_;

    long width = clampWidth(
        (long)defaultWidth(line, fieldRateHz, vertical) - zoomUnits, line);

    long start = (long)(line.units() - width) / 2 + pan;
    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - width)
        start = (long)line.lastCapture() - width;
    return CaptureWindow((uint16_t)start, (uint16_t)(start + width));
}

void PanAndZoom::clampToLine(const InputLine &line, float fieldRateHz, bool vertical)
{
    if (line.units() == 0)
        return;

    // Deliberately the same arithmetic as capture(), in the same order: this
    // has to agree with it exactly, or the framing is clamped to a window the
    // solver does not produce and the dead zone comes back one unit wide.
    int16_t &zoomUnits = vertical ? zoomV_ : zoomH_;
    int16_t &pan = vertical ? panV_ : panH_;

    long full = defaultWidth(line, fieldRateHz, vertical);
    long width = clampWidth(full - zoomUnits, line);
    zoomUnits = (int16_t)(full - width);

    long centre = (long)(line.units() - width) / 2;
    long start = centre + pan;
    if (start < (long)line.firstCapture())
        start = line.firstCapture();
    if (start > (long)line.lastCapture() - width)
        start = (long)line.lastCapture() - width;
    pan = (int16_t)(start - centre);
}

long PanAndZoom::clampWidth(long width, const InputLine &line)
{
    if (width > (long)line.capturable())
        return line.capturable();
    if (width < (long)MinimumCapture)
        return MinimumCapture;
    return width;
}

}  // namespace Tv5725
