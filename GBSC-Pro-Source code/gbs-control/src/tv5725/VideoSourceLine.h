#ifndef TV5725_VIDEO_SOURCE_LINE_H_
#define TV5725_VIDEO_SOURCE_LINE_H_

// The counter a capture window is placed in: how long it is, how much of it the
// sync interval takes, and which end of it that interval sits on. Turning those
// into a window is CaptureWindow's. docs/firmware-geometry-engine.md

#include <stdint.h>

#include "HsyncPulse.h"

namespace Tv5725 {

class VideoSourceLine {
public:
    // Blanking the capture path writes past the hsync pulse where the line
    // doubler is in circuit, which a window opened at the pulse's end captures
    // as the saturated green Y=U=V=0 decodes to. It bounds where a window may
    // OPEN and displaces nothing: the video behind it is where IF_HBIN_SP's own
    // line reset put it.
    //
    // **It is not a time and not a fraction of the line.** 17 was measured at
    // PLLAD_MD 2506, where the same time would be 15 units here and the same
    // fraction 15 as well -- both under what the bench needs. Two dividers, and
    // what they agree on is a count of UNITS.
    // docs/investigations/tail-green.md
    static const uint16_t DoubledHeadBlankingUnits = 22;

    // The whole line is available. The exclusion is the HSYNC pulse and there
    // is no vertical equivalent.
    explicit VideoSourceLine(uint16_t units);

    VideoSourceLine(uint16_t units, uint16_t syncUnits);

    // The frame, for a counter that zeroes on the vertical sync pulse's
    // TRAILING edge: video starts at the counter's own origin, so there is no
    // interval to exclude and no head blanking.
    static VideoSourceLine frame(uint16_t units);

    // The frame for a counter that zeroes on the pulse's LEADING edge instead,
    // where the pulse is leading blanking and the video sits that far behind
    // the origin. Which edge a sync arrangement zeroes on is not derivable here
    // and is the caller's.
    // ../../../../docs/investigations/the-vertical-origin-follows-the-sync-type.md
    static VideoSourceLine frame(uint16_t units, uint16_t vsyncUnits);

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
    static VideoSourceLine forDuty(uint16_t units, const HsyncPulse &pulse,
                                   bool lineDoubled);

    // Where the counter rolls over.
    uint16_t units() const;

    // How much of the counter the sync interval takes, and whether it sits at
    // the head rather than already behind the origin.
    uint16_t syncUnits() const;
    bool syncAtHead() const;

    // What the capture path writes over beyond the sync interval, at the head.
    uint16_t headBlankingUnits() const;

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
