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
    // How far into the line the capture path keeps writing video. Past it the
    // writes are Y=U=V=0, which decodes to green and destroys the picture
    // there, so this bounds usable capture width rather than hiding it.
    // docs/capture-limits.md
    static const uint16_t WriteLimitUnits = 1125;

    // How far after the sync edge the line is counted from video reaches the
    // input formatter. Measured on four undoubled modes as 69..76 units, with
    // the offset it explains running -16 to +76.
    // docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
    static const uint16_t CaptureLagUnits = 72;

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
    // is counted from whichever edge the chip triggered on and carries video a
    // lag behind it, so the two are the same position only on a line nothing
    // displaced.
    uint16_t videoAt(float lineFraction) const;

    // The widest capture this line can hold.
    uint16_t capturable() const;

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
    // the guard would throw away video.
    //
    // `lagUnits` is CaptureLagUnits where the capture path places the picture,
    // and zero where something else does -- on a doubled line IF_HBIN_SP is the
    // FIFO's own reset and puts the picture where it wants it.
    //
    // The tail is bounded by WriteLimitUnits, which is measured rather than
    // derived from anything this class is handed. docs/capture-limits.md
    static VideoSourceLine measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine,
                                    uint16_t lagUnits, bool syncAtHead);

private:
    VideoSourceLine(uint16_t units, uint16_t syncUnits, uint16_t lagUnits, bool syncAtHead);

    uint16_t units_;
    uint16_t syncUnits_;
    uint16_t lagUnits_;
    bool syncAtHead_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_SOURCE_LINE_H_
