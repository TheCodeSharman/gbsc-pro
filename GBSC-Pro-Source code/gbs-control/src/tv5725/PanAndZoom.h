#ifndef TV5725_PAN_AND_ZOOM_H_
#define TV5725_PAN_AND_ZOOM_H_

// The four integers of user intent. What they MEAN on a line is ActiveImage's;
// this is only what the user asked for.

#include <stdint.h>

#include "Axis.h"


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

    // The pair for one axis. The caller has an Axis in hand already, so it asks
    // with that rather than choosing between two getters.
    int16_t zoomOn(const Axis &axis) const;
    int16_t panOn(const Axis &axis) const;
    void setZoomOn(const Axis &axis, int16_t units);
    void setPanOn(const Axis &axis, int16_t units);

    // Zoom in is POSITIVE: it crops, and the scale follows.
    void zoomBy(int16_t dh, int16_t dv);
    void panBy(int16_t dx, int16_t dy);

    // A mode change has no framing worth keeping, only the previous mode's.
    void reset();

    bool operator==(const PanAndZoom &other) const;
    bool operator!=(const PanAndZoom &other) const;

private:
    int16_t horizontalZoom_;   // input units cropped off the default width; negative widens
    int16_t verticalZoom_;
    int16_t horizontalPan_;    // input units from the centre of the line
    int16_t verticalPan_;
};

}  // namespace Tv5725

#endif  // TV5725_PAN_AND_ZOOM_H_
