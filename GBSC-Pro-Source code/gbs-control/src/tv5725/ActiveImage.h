#ifndef TV5725_ACTIVE_IMAGE_H_
#define TV5725_ACTIVE_IMAGE_H_

// The part of the source's line that holds picture, as against blanking and
// border, and where it lands inside the bounds the capture can reach.
//
// **NO HARDWARE FACILITY CAN MEASURE THIS.** A border is black ACTIVE video,
// electrically identical to back porch, so sync-domain measurement finds the
// raster and never the picture inside it. It is an assertion, not a
// measurement: the user places it by panning and zooming, and it starts from
// what an untuned source is assumed to fill.
//
// PanAndZoom is the four integers of user intent; this is what they mean on a
// line. The bounds belong to CaptureWindow, so staying inside them is a
// conversation between the two rather than either clamping alone.

#include <stdint.h>

#include "Axis.h"
#include "BlankingTiming.h"
#include "VideoSourceLine.h"
#include "PanAndZoom.h"
#include "SourceTiming.h"

namespace Tv5725 {

class ActiveImage {
public:
    ActiveImage();
    explicit ActiveImage(const PanAndZoom &framing);

    const PanAndZoom &framing() const;
    void setFraming(const PanAndZoom &framing);

    bool operator==(const ActiveImage &other) const;
    bool operator!=(const ActiveImage &other) const;

    // How much of the line an untuned source is assumed to fill. COMPUTED from
    // the line alone -- nothing is read from the chip. The fraction is of the
    // WHOLE line, because that is what the VESA and CEA modes it came from
    // measure; what the line can actually hold then bounds it. A source running
    // a published raster is not assumed at all: the standard states its active
    // window and it is taken exactly, with no over-capture to add.
    static uint16_t defaultWidth(const VideoSourceLine &line, const SourceTiming &timing,
                                 const Axis &axis);

    // A width wider than the line can hold wraps; one narrower than
    // MinimumCapture is a dead picture with no press back. Both bounds are the
    // LINE's, which is what keeps a framing meaning the same part of the source
    // whatever the output is doing.
    static long clampWidth(long width, const VideoSourceLine &line);

    // Where this lands on `line`. Derived from the framing and the line alone --
    // nothing is read back. At rest this IS the default window, so there is no
    // second definition of it.
    BlankingTiming capture(const VideoSourceLine &line, const SourceTiming &timing,
                           const Axis &axis) const;

    // Give up extent, keeping the centre the user placed. The bound passed in
    // is the OUTPUT raster's, which this class knows nothing about: the line's
    // own bounds are clampToLine()'s.
    void narrowTo(const Axis &axis, float extent);

    // Seed an axis nobody has framed yet, and bring a framed one back where
    // this line cannot realise it -- a framing left beyond anything reachable
    // kills the control in that direction, see VideoPath::readCapture().
    //
    // A framing the line CAN realise is left exactly as the user set it. Writing
    // the placement back unconditionally re-quantises the proportion onto
    // whatever grid this line happens to offer, and the capture grid halves
    // when the line doubler goes off, so a trip through a short output used to
    // cost a unit that the fine grid could never express again.
    // docs/investigations/a-coarse-capture-grid-must-not-rewrite-the-framing.md
    void clampToLine(const VideoSourceLine &line, const SourceTiming &timing,
                     const Axis &axis);

private:
    // The width and start this lands on, before either becomes a register.
    // capture() and clampToLine() both take it from here, so they cannot
    // disagree: one unit apart is a dead zone one press wide. `clamped` says a
    // bound moved it off what the framing asked for, which is the only reason
    // to overwrite a framing the user set.
    struct Placement { long width, start; bool clamped; };
    Placement place(const VideoSourceLine &line, const SourceTiming &timing,
                    const Axis &axis) const;

    PanAndZoom framing_;
};

}  // namespace Tv5725

#endif  // TV5725_ACTIVE_IMAGE_H_
