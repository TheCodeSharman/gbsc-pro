#ifndef TV5725_CAPTURE_WINDOW_H_
#define TV5725_CAPTURE_WINDOW_H_

// The rectangle grabbed out of the source's line and frame, and the thing pan
// and zoom move. It calculates the Input Formatter registers that capture the
// aperture the PanAndZoom asks for.
//
// **NO HARDWARE FACILITY CAN MEASURE WHERE PICTURE SITS IN THE RASTER.** A
// border is black ACTIVE video, electrically identical to back porch, so
// sync-domain measurement finds the raster and never the picture inside it. The
// framing is an assertion, not a measurement: the user places it by panning and
// zooming, and it starts from what an untuned source is assumed to fill.
//
// PanAndZoom is the four integers of user intent; this turns them into a window
// on a line it holds, so the bounds and the placement cannot disagree.

#include <stdint.h>

#include "Axis.h"
#include "VideoSourceLine.h"
#include "SourceTiming.h"
#include "PanAndZoom.h"
#include "BlankingTiming.h"

namespace Tv5725 {

class CaptureWindow {
public:
    CaptureWindow();

    // The line, the frame and the published raster a framing is placed against.
    // InputFormatter states the two counters, because it is the block that
    // writes the line counter; `timing` is the published raster the measurement
    // matched, resolved before it gets here. docs/firmware-geometry-engine.md
    CaptureWindow(const VideoSourceLine &line, const VideoSourceLine &frame,
                  const SourceTiming &timing);

    // IF_LINE_ST. Chosen, not derived -- nothing explains 64.
    static const uint16_t ProgressiveStart = 64;

    // Clamped on the way in, so the framing kept is one these bounds can
    // realise, and the windows are derived from the same placement -- one unit
    // apart is a dead zone one press wide. A press big enough to overshoot
    // still moves the window a unit or two, so it is accepted and the overshoot
    // kept; every smaller press back then produces an identical window and is
    // reverted, leaving the control dead in that direction. Only the hold ramp
    // presses that far -- measured pv -51 against a limit of -46, ph -144
    // against -134.
    void setFraming(const PanAndZoom &wanted);
    const PanAndZoom &framing() const;

    // The register pairs: IF_HB_ST2/SP2 and IF_VB_ST/SP. Each carries its
    // axis's captureMargin at both ends of the picture, floored at the
    // counter's origin and clamped to the last unit before it wraps.
    const BlankingTiming &horizontal() const;
    const BlankingTiming &vertical() const;

    // The horizontal line knows what the hsync pulse takes off its head; the
    // vertical does not, because nothing has measured the vsync equivalent and
    // a guess there would crop picture rather than blanking.
    const VideoSourceLine &horizontalLine() const;
    const VideoSourceLine &verticalLine() const;

    // The span the framing is a proportion of: the WHOLE line, which is the
    // same part of the source in either scan mode. What the capture path can
    // actually reach inside it is VideoSourceLine's own business.
    uint16_t lineUnitsOn(const Axis &axis) const;

    // The first and last units of the line a capture window may occupy. What
    // lies outside them is the capture path's own exclusion, not the framing's.
    uint16_t firstUnitOn(const Axis &axis) const;
    uint16_t reachOn(const Axis &axis) const;

    bool usable() const;

    // How much of the line an untuned source is assumed to fill. COMPUTED from
    // the line alone -- nothing is read from the chip. The fraction is of the
    // WHOLE line, because that is what the VESA and CEA modes it came from
    // measure; what the line can actually hold then bounds it. A source running
    // a published raster is not assumed at all: the standard states its active
    // window and it is taken exactly, with no over-capture to add.
    static uint16_t defaultWidth(const VideoSourceLine &line, const SourceTiming &timing,
                                 const Axis &axis);

private:
    // The width and start an axis lands on, before either becomes a register.
    // captureOn() and clampFramingTo() both take it from here, so they cannot
    // disagree. `clamped` says a bound moved it off what the framing asked for,
    // which is the only reason to overwrite a framing the user set.
    struct Placement { long width, start; bool clamped; };

    const VideoSourceLine &lineOn(const Axis &axis) const;
    Placement place(const Axis &axis) const;
    BlankingTiming captureOn(const Axis &axis) const;

    // Seeds an axis nobody has framed yet, and brings a framed one back where
    // the line cannot realise it -- a framing left beyond anything reachable
    // kills the control in that direction, see VideoPath::readCapture().
    //
    // A framing the line CAN realise is left exactly as the user set it. Writing
    // the placement back unconditionally re-quantises the proportion onto
    // whatever grid this line happens to offer, and the capture grid halves
    // when the line doubler goes off, so a trip through a short output used to
    // cost a unit that the fine grid could never express again.
    // docs/investigations/a-coarse-capture-grid-must-not-rewrite-the-framing.md
    void clampFramingTo(const Axis &axis);

    // A width wider than the line can hold wraps; one narrower than
    // MinimumCapture is a dead picture with no press back. Both bounds are the
    // LINE's, which is what keeps a framing meaning the same part of the source
    // whatever the output is doing.
    static long clampWidth(long width, const VideoSourceLine &line);

    VideoSourceLine horizontalLine_, verticalLine_;
    SourceTiming timing_;
    PanAndZoom framing_;
    BlankingTiming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_CAPTURE_WINDOW_H_
