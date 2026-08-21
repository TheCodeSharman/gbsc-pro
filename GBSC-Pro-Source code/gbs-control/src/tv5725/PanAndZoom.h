#ifndef TV5725_PAN_AND_ZOOM_H_
#define TV5725_PAN_AND_ZOOM_H_

// The user's framing: the only state the geometry keeps, and the thing every
// register is an output of.

#include <stdint.h>

#include "Axis.h"
#include "InputLine.h"
#include "BlankingTiming.h"

namespace Tv5725 {

// Active area as a fraction of the total. Horizontal barely moves across VESA,
// CEA and the NES; vertical splits hard on field rate, because a 50 Hz source
// carries the same active height in a longer frame. A starting point for an
// untuned source, not a derivation.
extern const float DefaultHActiveFraction;
extern const float DefaultVActiveFraction60Hz;
extern const float DefaultVActiveFraction50Hz;

// Err toward capturing blanking rather than cropping picture: black edges are
// visible and adjustable, a cropped edge looks like a tuning fault.
extern const float OverCapture;

// A control that can crop the capture to nothing is one keypress from a dead
// picture with no way back.
extern const uint16_t MinimumCapture;

// The user's framing, per axis, and the only state the geometry keeps. The
// registers are an output of it, never an input to it.
//
// Every field is an integer count of INPUT UNITS, so out and back returns the
// same window exactly rather than relying on rounding to cancel, and so one unit
// is reachable -- which the proportional form this replaced could not express.
class PanAndZoom {
public:
    PanAndZoom();
    PanAndZoom(int16_t horizontalZoom, int16_t verticalZoom, int16_t horizontalPan, int16_t verticalPan);

    int16_t horizontalZoom() const;
    int16_t verticalZoom() const;
    int16_t horizontalPan() const;
    int16_t verticalPan() const;

    void setHorizontalZoom(int16_t units);
    void setVerticalZoom(int16_t units);
    void setHorizontalPan(int16_t units);
    void setVerticalPan(int16_t units);

    // Zoom in is POSITIVE: it crops, and the scale follows.
    void zoomBy(int16_t dh, int16_t dv);
    void panBy(int16_t dx, int16_t dy);

    // A mode change has no framing worth keeping, only the previous mode's.
    void reset();

    bool operator==(const PanAndZoom &other) const;
    bool operator!=(const PanAndZoom &other) const;

    // How much of the line an untuned source is assumed to fill. COMPUTED from
    // the line alone -- nothing is read from the chip. The fraction is of the
    // WHOLE line, because that is what the VESA and CEA modes it came from
    // measure; what the line can actually hold then bounds it.
    static uint16_t defaultWidth(const InputLine &line, float fieldRateHz,
                                 bool vertical);

    // The capture window this framing means, on `line`. Derived from the
    // framing and the line alone -- nothing is read back. At rest this IS the
    // default window, so there is no second definition of it.
    BlankingTiming capture(const InputLine &line, float fieldRateHz, bool vertical,
                   uint16_t rasterTotal) const;

    // Bring this framing back to what the line can actually realise. capture()
    // clamps the WINDOW, and a framing left beyond anything reachable kills the
    // control in that direction -- see Geometry::readCapture().
    void clampToLine(const InputLine &line, float fieldRateHz, bool vertical,
                     uint16_t rasterTotal);

    // A width wider than the line can hold wraps; one narrower than the minimum
    // is a dead picture. The floor is whichever is larger: what the control must
    // not crop past, and what the SCALE can still magnify to fill the raster --
    // without the second, zooming past the magnification ceiling keeps cropping
    // and the picture letterboxes instead of the control stopping.
    static long clampWidth(long width, const InputLine &line, uint16_t rasterTotal,
                           const Axis &axis);

private:
    // The width and start this framing lands on, before either becomes a
    // register. capture() and clampToLine() both take it from here, so they
    // cannot disagree: one unit apart is a dead zone one press wide.
    struct Placement { long width, start; };
    Placement place(const InputLine &line, float fieldRateHz, bool vertical,
                    uint16_t rasterTotal) const;

    int16_t horizontalZoom_;   // input units cropped off the default width; negative widens
    int16_t verticalZoom_;
    int16_t horizontalPan_;    // input units from the centre of the line
    int16_t verticalPan_;
};

}  // namespace Tv5725

#endif  // TV5725_PAN_AND_ZOOM_H_
