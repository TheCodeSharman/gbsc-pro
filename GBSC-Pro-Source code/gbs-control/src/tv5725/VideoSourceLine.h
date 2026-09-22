#ifndef TV5725_VIDEO_SOURCE_LINE_H_
#define TV5725_VIDEO_SOURCE_LINE_H_

// The video source's line, as a capture window is placed on it: how long it is,
// and how much of it the framing may not have.

#include <stdint.h>

#include "HsyncPulse.h"

namespace Tv5725 {

// The line the framing is applied to, together with the part of it the capture
// window may not take. What a unit is belongs to whoever counted them --
// CaptureWindow decides it per axis, from the scan mode.
// PanAndZoom::capture() and PanAndZoom::clampToLine() clamp against this and
// must agree exactly: one unit of disagreement is a dead zone where every press
// back produces an identical window. docs/firmware-geometry-engine.md
class VideoSourceLine {
public:
    // Blanking the capture path writes past the hsync pulse where the line
    // doubler is in circuit, which a window opened at the pulse's end captures
    // as the saturated green Y=U=V=0 decodes to. It bounds where a window may
    // OPEN and displaces nothing: the video behind it is where IF_HBIN_SP's own
    // line reset put it, so videoAt() does not carry it.
    //
    // Crept at PLLAD_MD 2200 on a 1101-unit line whose pulse is 78.1 units: a
    // window 17.9 units past the pulse takes the green, 19.9 is clean, and the
    // margin here is on top of that. The cost is the source's own blanking,
    // which a full framing reaches into anyway, so the picture does not shrink.
    //
    // **It is not a time and not a fraction of the line.** 17 was measured at
    // PLLAD_MD 2506, where the same time would be 15 units here and the same
    // fraction 15 as well -- both under what the bench needs. Two dividers, and
    // what they agree on is a count of UNITS.
    // docs/investigations/tail-green.md
    static const uint16_t DoubledHeadBlankingUnits = 22;

    // The earliest unit a capture window may open on, whatever the sync
    // arrangement leaves free. docs/known-issues.md
    static const uint16_t FirstCapturableUnit = 1;

    // The whole line is available. The exclusion is the HSYNC pulse and there
    // is no vertical equivalent.
    explicit VideoSourceLine(uint16_t units);

    // The frame: no sync interval to exclude and no head blanking.
    static VideoSourceLine frame(uint16_t units);

    VideoSourceLine(uint16_t units, uint16_t syncUnits);

    // Where the window rolls over.
    uint16_t units() const;

    // How much of the line the hsync pulse takes. Whether that is at the head
    // is the `syncAtHead` measured() was given.
    uint16_t syncUnits() const;

    uint16_t firstCapture() const;
    uint16_t lastCapture() const;

    // Where a position a video standard states as a fraction of ITS line lands
    // in this one. The standard counts from the hsync leading edge; this line
    // is counted from whichever edge the chip triggered on, so the two differ
    // by the sync interval on a line whose origin is the trailing edge.
    uint16_t videoAt(float lineFraction) const;

    // The inverse: which fraction of the source's line the video at this
    // position in the counter came from.
    float fractionAt(uint16_t position) const;

    // The span the framing is a proportion of: everything between the ends.
    uint16_t capturable() const;

    // The widest window that may actually be taken, which is the whole
    // capturable span: nothing in the capture path bounds it.
    // docs/investigations/the-tail-green-is-the-vds-line-filter.md
    uint16_t maxCaptureWidth() const;

    // Where the input formatter's PROGRESSIVE line window stops, given where
    // IF_LINE_ST starts it. That window is the line double timing -- it belongs
    // to deinterlacing, not to the picture -- and it has to span exactly one
    // line, so the stop follows the line length and may roll past it. The line
    // length moves with PLLAD_MD, so a constant stop sizes the window for
    // whichever line it was picked against.
    uint16_t progressiveStop(uint16_t start) const;

    // The line the source sends, from the pulse measured off it. The pulse is
    // that fraction of `units`, and nothing here is a constant for one source:
    // a 0.121 duty source excludes nearly twice what a 0.071 one does.
    //
    // NOTHING IS SUBSTITUTED FOR AN IMPLAUSIBLE DUTY. An HsyncPulse has already
    // been judged a pulse where it was taken, so there is no reading here to
    // refuse -- and a fallback placed the window from a guess that happened to
    // suit the bench source to one unit, which made every other mode wrong and
    // invisible. HsyncPulse.h
    //
    // The pulse's own polarity says which end of the line the origin is on. A
    // positive-going pulse puts it on the leading edge, so the pulse is at the
    // head and no window may start inside it; an inverted one puts it on the
    // trailing edge, where the sync interval is already behind the origin and a
    // guard there would throw away video.
    //
    // `lineDoubled` carries DoubledHeadBlankingUnits, which is a bound on where
    // a window may OPEN rather than a displacement of the video behind it.
    static VideoSourceLine forDuty(uint16_t units, const HsyncPulse &pulse,
                                   bool lineDoubled);

private:
    VideoSourceLine(uint16_t units, uint16_t syncUnits,
                    uint16_t headBlankingUnits, bool syncAtHead);

    uint16_t units_;
    uint16_t syncUnits_;
    uint16_t headBlankingUnits_;
    bool syncAtHead_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_SOURCE_LINE_H_
