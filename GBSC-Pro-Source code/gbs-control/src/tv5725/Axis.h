#ifndef TV5725_AXIS_H_
#define TV5725_AXIS_H_

// Which axis this is, and what the CAPTURE path does on it: the grid a window
// may move on, the margin the path drops at each end, and where an untuned
// source is assumed to put its video.
//
// It is the token the whole engine is parameterised by: every ...On(axis) call
// passes one of the two instances below rather than a flag each caller
// re-derives the same per-axis data from.
//
// Where the picture LANDS is not here. The write-start model, the placement
// and the scale fitting belong to OutputWindow, which owns the registers they
// produce. See docs/scaler-geometry-model.md for the measurements.
#include <stdint.h>

namespace Tv5725 {
class Axis {
public:
    Axis(uint16_t captureGranularity, uint16_t captureMargin,
         float activeStart, float activeExtent, bool vertical);

    // Which axis this is. The one place that knows: callers pass the axis and
    // the arithmetic reads what it needs off it, rather than each call taking a
    // flag and re-deriving the same per-axis data from it.
    bool vertical() const;

    // Where active video starts and how far it runs on a source running no
    // raster the standards state, as a fraction of the whole line or frame.
    // The ENVELOPE of what real sources put on a line, so nothing is cropped
    // and what is captured beyond the picture is black -- which is visible and
    // one press away, where a cropped edge looks like a fault.
    // docs/investigations/vesa-modes-are-clipped-by-default.md
    float activeStart() const;
    float activeExtent() const;

    // How far beyond the picture the capture window opens at EACH END, so that
    // the units the write only partly fills have a spare one under them. The
    // aperture then spans the whole write and the picture reaches the raster at
    // every edge -- the margin is what PAYS for that, rather than something the
    // aperture gives back.
    //
    // Vertically 2, both measured. A window opened on the picture loses the
    // source's first and last lines: at 800x600@60 the top frame reaches the
    // panel from IF_VB_SP 19 and not 20, and the bottom from IF_VB_ST 621 and
    // not 620. The second unit is measured at 1024x768@60, where the card's
    // green frame does not reach the panel from the engine's own window and
    // does from one unit earlier; opening early costs a line of the source's
    // blanking and nothing else. The doubled case is NOT measured.
    // Horizontally 1, for the partly written unit at each end. It used to be 0
    // and the aperture was inset instead, which showed as a black column down
    // the left of a full-screen picture and a black row across the bottom.
    uint16_t captureMargin() const;

    // The smallest change of capture POSITION this axis's hardware acts on.
    // Horizontally 2 IF units -- the low bit of IF_HB_SP2 does nothing, so a
    // one-unit move leaves the picture where it was. Vertically 1.
    // docs/scaler-geometry-model.md.
    uint16_t captureGranularity() const;

    // A move of `pixels` output pixels, in capture units: the nearest whole
    // number of granules to what was asked, which below half a granule is none
    // of them. A press is stated in output pixels and answered in them.
    int16_t stepUnits(int16_t pixels, float magnification) const;

private:
    uint16_t captureGranularity_, captureMargin_;
    float activeStart_, activeExtent_;
    bool vertical_;
};

extern const Axis AxisHorizontal;
extern const Axis AxisVertical;

}  // namespace Tv5725

#endif  // TV5725_AXIS_H_
