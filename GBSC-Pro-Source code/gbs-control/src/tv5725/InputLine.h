#ifndef GEOMETRY_LINE_H_
#define GEOMETRY_LINE_H_

// The source line a capture window is placed on: how long it is, and how much
// of it the framing may not have.

#include <stdint.h>

namespace Tv5725 {

// The line the framing is applied to -- IF units horizontally, HALF-LINES
// vertically -- together with the part of it the capture window may not take.
// PanAndZoom::capture() and PanAndZoom::clampToLine() clamp against this and
// must agree exactly: one unit of disagreement is a dead zone where every press
// back produces an identical window. docs/firmware-geometry-engine.md
class InputLine {
public:
    // The whole line is available. Vertical uses this: the exclusion is the
    // HSYNC pulse and there is no vertical equivalent.
    explicit InputLine(uint16_t units);

    InputLine(uint16_t units, uint16_t syncUnits);

    // Where the window rolls over: IF_HSYNC_RST + 1, or 2 x (VTOTAL + 1).
    uint16_t units() const;

    // How much of the line the hsync pulse takes at the HEAD.
    uint16_t syncUnits() const;

    uint16_t firstCapture() const;
    uint16_t lastCapture() const;

    // The widest capture this line can hold.
    uint16_t capturable() const;

    // The line as the chip measures it. `hlowLen` is STATUS_SYNC_PROC_HLOW_LEN,
    // the hsync low duration in ADC samples, and `adcLine` is PLLAD_MD, the
    // whole line in the same samples -- so their ratio is the hsync duty and
    // the pulse is that fraction of `units`. Nothing here is a constant for one
    // source: a 0.121 duty source excludes nearly twice what a 0.071 one does.
    //
    // The pulse is at the HEAD, because SP_RT_HS_ST reads 0 and the input
    // formatter counts from the sync's leading edge.
    //
    // The TAIL IS DELIBERATELY UNBOUNDED. There is green there too and where it
    // comes from is an open question: no register holds its position, and a
    // black border is electrically identical to back porch -- so a guard there
    // would be a number nobody can derive. docs/scaler-geometry-model.md.
    static InputLine measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine);

private:
    uint16_t units_;
    uint16_t syncUnits_;
};

}  // namespace Tv5725

#endif  // GEOMETRY_LINE_H_
