#ifndef TV5725_VIDEO_SOURCE_LINE_H_
#define TV5725_VIDEO_SOURCE_LINE_H_

// The video source's line, as a capture window is placed on it: how long it is,
// and how much of it the framing may not have.

#include <stdint.h>

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

    // How late the capture path delivers video, as a FRACTION OF THE LINE,
    // counted from the sync edge the line is counted from. It TRANSLATES a
    // window rather than narrowing it: both ends move, because the video
    // behind them does. Zero on a doubled line, where IF_HBIN_SP is the FIFO's
    // own reset and places the picture itself.
    //
    // A fraction and not a count of samples, which takes two dividers to tell
    // apart: 72 samples at PLLAD_MD 1124 on 800x600@60 and 118 at 1880 on the
    // bench source at 480p are 0.0641 and 0.0628 of their lines, agreeing to
    // 2%, where as a count they disagree by 64%. Nor is it a time -- the same
    // two are 1.69 us and 4.02 us.
    // docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
    static const float CaptureLagFraction;

    // The whole line is available. Vertical uses this: the exclusion is the
    // HSYNC pulse and there is no vertical equivalent.
    explicit VideoSourceLine(uint16_t units);

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

    // lastCapture() said in the source's own units, which is what a control
    // bound has to be in: a framing names a position in the video and the lag
    // is what turns that into a position in the counter. Bounding the control
    // by the counter's own last unit instead leaves the tail of the line
    // unreachable by exactly the lag, which is a dead zone one press wide.
    uint16_t lastReachable() const;

    // How far the video sits behind the counter's origin on this line, which is
    // what turns a position in the source into a position in the counter.
    uint16_t videoLag() const;

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

    // The line as the chip measures it. `hlowLen` is STATUS_SYNC_PROC_HLOW_LEN,
    // the hsync low duration in ADC samples, and `adcLine` is PLLAD_MD, the
    // whole line in the same samples -- so their ratio is the hsync duty and
    // the pulse is that fraction of `units`. Nothing here is a constant for one
    // source: a 0.121 duty source excludes nearly twice what a 0.071 one does.
    //
    // `syncAtHead` is STATUS_SYNC_PROC_HSPOL. A positive-going pulse puts the
    // line's origin on its leading edge, so the pulse is at the head and no
    // window may start inside it; an inverted one puts the origin on the
    // trailing edge, where the sync interval is already behind the origin and
    // a guard there would throw away video.
    //
    // `lineDoubled` carries DoubledHeadBlankingUnits, which is a bound on where
    // a window may OPEN rather than a displacement of the video behind it.
    static VideoSourceLine measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine,
                                    bool syncAtHead);

    // The same line from the duty directly, which is the form a reading taken
    // against one divider carries across the solve that replaces it.
    static VideoSourceLine forDuty(uint16_t units, float duty, bool lineDoubled,
                                   bool syncAtHead);

private:
    VideoSourceLine(uint16_t units, uint16_t syncUnits,
                    uint16_t headBlankingUnits, bool syncAtHead);

    uint16_t units_;
    uint16_t syncUnits_;
    uint16_t lagUnits_;
    uint16_t headBlankingUnits_;
    bool syncAtHead_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_SOURCE_LINE_H_
