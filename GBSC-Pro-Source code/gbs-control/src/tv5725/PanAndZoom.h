#ifndef TV5725_PAN_AND_ZOOM_H_
#define TV5725_PAN_AND_ZOOM_H_

// The user's framing, per axis, as a proportion of the capturable region: where
// the window starts, and how much of that region it spans.
//
// Nothing here refers to a mode, so one pair means the same part of the picture
// whatever the source measures. That is what makes it both the live state and
// the stored state, with no portable form to convert to, and it is why the
// bound lives in the representation: a window outside what can be captured is
// not expressible. CaptureWindow knows the mode and turns a pair into the window
// on a given line. docs/framing-presets.md

#include <stdint.h>

#include "Axis.h"


namespace Tv5725 {

// A control that can crop the capture to nothing is one keypress from a dead
// picture with no way back.
extern const uint16_t MinimumCapture;

class PanAndZoom {
public:
    PanAndZoom();
    PanAndZoom(float horizontalOrigin, float horizontalExtent,
               float verticalOrigin, float verticalExtent);

    // An axis nobody has framed yet has no proportion of its own: CaptureWindow
    // gives it the computed default for the mode in force. The first press
    // seeds it from that default, and from then on the pair is the answer.
    bool tunedOn(const Axis &axis) const;
    void seedOn(const Axis &axis, float origin, float extent);

    // Shrink to this extent about the framing's own centre, where it is wider.
    // A framing nobody has tuned is left alone: its extent is a default rather
    // than a request, and narrowing it would record a choice the user made.
    void narrowTo(const Axis &axis, float extent);

    float originOn(const Axis &axis) const;
    float extentOn(const Axis &axis) const;

    // `usable` is the capturable region this mode offers, so one step is one
    // input unit exactly, whatever grid the proportion sits on.
    //
    // The two controls are orthogonal: the pan places the near edge and the
    // zoom moves the far one, so a framing is found in one pass of each. Zoom
    // in is POSITIVE: it crops, from the far edge alone.
    //
    // `narrowest` is where the zoom STOPS, in the same units as `usable`: the
    // capture below which the scale is already at its floor, so a tighter crop
    // is a smaller picture rather than a closer one. 0 asks for no stop, which
    // is what a caller with no output raster to measure it against has.
    // `reach` is the last unit of `usable` the window may occupy, which is not
    // the end of the line: the capture path excludes a head and the last two
    // units. Each control gives back at that bound in its OWN quantity -- a pan
    // stops moving and a zoom stops widening -- which is why the bound is here
    // and not in the placement, where nothing knows which control ran. 0 asks
    // for the whole line.
    void zoomBy(const Axis &axis, int16_t units, uint16_t usable,
                uint16_t reach = 0, uint16_t narrowest = 0);
    void panBy(const Axis &axis, int16_t units, uint16_t usable,
               uint16_t reach = 0);

    // A mode change has no framing worth keeping, only the previous mode's.
    void reset();

    bool operator==(const PanAndZoom &other) const;
    bool operator!=(const PanAndZoom &other) const;

private:
    // One axis's framing. Selected once and read off, so the arithmetic below
    // names what it is working on rather than choosing an axis at every term.
    struct AxisFraming {
        float origin, extent;
        bool tuned;
    };

    AxisFraming &on(const Axis &axis);
    const AxisFraming &on(const Axis &axis) const;
    static bool same(const AxisFraming &a, const AxisFraming &b);

    // Whole units of `usable`, so every value a control produces sits on that
    // mode's grid and the translation to units lands on an integer.
    static float moved(float value, int16_t units, uint16_t usable);

    // Whichever of the pair the control did not move gives way, so a pan never
    // resizes and a zoom never shifts. They take the framing rather than an
    // axis because the relationship they hold is between the origin and the
    // extent and has nothing to do with which axis those belong to. A seeded
    // pair has no control behind it, so its extent is bounded by the whole
    // region and the origin gives way.
    static void clampOrigin(AxisFraming &framing, float limit = 1.0f);
    static void clampExtent(AxisFraming &framing, float limit = 1.0f);
    static float limitFor(uint16_t reach, uint16_t usable);
    static void clampSeed(AxisFraming &framing);

    AxisFraming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_PAN_AND_ZOOM_H_
