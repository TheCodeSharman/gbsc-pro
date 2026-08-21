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
#include "InputLine.h"
#include "PanAndZoom.h"

namespace Tv5725 {

class ActiveImage {
public:
    ActiveImage();
    explicit ActiveImage(const PanAndZoom &framing);

    const PanAndZoom &framing() const;
    void setFraming(const PanAndZoom &framing);

    // Placed by panning and zooming: the user has no other way to say where the
    // picture is, because nothing can measure it.
    void panBy(int16_t dx, int16_t dy);
    void zoomBy(int16_t dh, int16_t dv);

    int16_t horizontalZoom() const;
    int16_t verticalZoom() const;
    int16_t horizontalPan() const;
    int16_t verticalPan() const;

    bool operator==(const ActiveImage &other) const;
    bool operator!=(const ActiveImage &other) const;

    // How much of the line an untuned source is assumed to fill. COMPUTED from
    // the line alone -- nothing is read from the chip. The fraction is of the
    // WHOLE line, because that is what the VESA and CEA modes it came from
    // measure; what the line can actually hold then bounds it.
    static uint16_t defaultWidth(const InputLine &line, float fieldRateHz,
                                 const Axis &axis);

    // A width wider than the line can hold wraps; one narrower than the minimum
    // is a dead picture. The floor is whichever is larger: what the control must
    // not crop past, and what the SCALE can still magnify to fill the raster --
    // without the second, zooming past the magnification ceiling keeps cropping
    // and the picture letterboxes instead of the control stopping.
    static long clampWidth(long width, const InputLine &line, uint16_t rasterTotal,
                           const Axis &axis);

    // Where this lands on `line`. Derived from the framing and the line alone --
    // nothing is read back. At rest this IS the default window, so there is no
    // second definition of it.
    BlankingTiming capture(const InputLine &line, float fieldRateHz, const Axis &axis,
                           uint16_t rasterTotal) const;

    // Bring the framing back to what the line can actually realise. capture()
    // clamps the WINDOW, and a framing left beyond anything reachable kills the
    // control in that direction -- see Geometry::readCapture().
    void clampToLine(const InputLine &line, float fieldRateHz, const Axis &axis,
                     uint16_t rasterTotal);

private:
    // The width and start this lands on, before either becomes a register.
    // capture() and clampToLine() both take it from here, so they cannot
    // disagree: one unit apart is a dead zone one press wide.
    struct Placement { long width, start; };
    Placement place(const InputLine &line, float fieldRateHz, const Axis &axis,
                    uint16_t rasterTotal) const;

    PanAndZoom framing_;
};

}  // namespace Tv5725

#endif  // TV5725_ACTIVE_IMAGE_H_
