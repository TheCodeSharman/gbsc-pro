#ifndef TV5725_PAN_AND_ZOOM_H_
#define TV5725_PAN_AND_ZOOM_H_

// The user's framing, per axis, as a proportion of the capturable region: where
// the window starts, and how much of that region it spans.
//
// Nothing here refers to a mode, so one pair means the same part of the picture
// whatever the source measures. That is what makes it both the live state and
// the stored state, with no portable form to convert to, and it is why the
// bound lives in the representation: a window outside what can be captured is
// not expressible. ActiveImage knows the mode and turns a pair into the window
// on a given line. docs/framing-presets.md

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

class PanAndZoom {
public:
    PanAndZoom();
    PanAndZoom(float horizontalOrigin, float horizontalExtent,
               float verticalOrigin, float verticalExtent);

    // An axis nobody has framed yet has no proportion of its own: ActiveImage
    // gives it the computed default for the mode in force. The first press
    // seeds it from that default, and from then on the pair is the answer.
    bool tunedOn(const Axis &axis) const;
    void seedOn(const Axis &axis, float origin, float extent);

    float originOn(const Axis &axis) const;
    float extentOn(const Axis &axis) const;

    // `usable` is the capturable region this mode offers, so one step is one
    // input unit exactly and a step with its inverse returns the identical
    // proportion rather than one that happens to land on the same unit.
    //
    // Zoom in is POSITIVE: it crops. The origin moves by half of what the
    // extent loses, so the window keeps its centre.
    void zoomBy(const Axis &axis, int16_t units, uint16_t usable);
    void panBy(const Axis &axis, int16_t units, uint16_t usable);

    // A mode change has no framing worth keeping, only the previous mode's.
    void reset();

    bool operator==(const PanAndZoom &other) const;
    bool operator!=(const PanAndZoom &other) const;

private:
    // Whole units of `usable`, so every value a control produces sits on that
    // mode's grid and the translation to units lands on an integer.
    static float moved(float value, int16_t units, uint16_t usable);
    void clampOn(const Axis &axis);

    float horizontalOrigin_, horizontalExtent_;
    float verticalOrigin_, verticalExtent_;
    bool horizontalTuned_, verticalTuned_;
};

}  // namespace Tv5725

#endif  // TV5725_PAN_AND_ZOOM_H_
