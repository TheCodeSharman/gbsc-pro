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

    // Move the framing by whole input units on `line`. An axis nobody has
    // framed yet is seeded from the default first, so a press always lands on
    // this mode's grid -- which is what makes one press one unit, and a press
    // with its inverse return the same framing.
    void panBy(const VideoSourceLine &line, const SourceTiming &timing,
               const Axis &axis, int16_t units);
    void zoomBy(const VideoSourceLine &line, const SourceTiming &timing,
                const Axis &axis, int16_t units);

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

    // Bring the framing back to what the line can actually realise. capture()
    // clamps the WINDOW, and a framing left beyond anything reachable kills the
    // control in that direction -- see VideoPath::readCapture().
    void clampToLine(const VideoSourceLine &line, const SourceTiming &timing,
                     const Axis &axis);

private:
    // The width and start this lands on, before either becomes a register.
    // capture() and clampToLine() both take it from here, so they cannot
    // disagree: one unit apart is a dead zone one press wide.
    struct Placement { long width, start; };
    Placement place(const VideoSourceLine &line, const SourceTiming &timing,
                    const Axis &axis) const;

    PanAndZoom framing_;
};

}  // namespace Tv5725

#endif  // TV5725_ACTIVE_IMAGE_H_
