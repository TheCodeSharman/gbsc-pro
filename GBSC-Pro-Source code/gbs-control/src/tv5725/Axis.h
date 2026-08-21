#ifndef TV5725_AXIS_H_
#define TV5725_AXIS_H_

// An axis, which knows its own write-start model and solves itself.
// See docs/scaler-geometry-model.md for the measurements behind the numbers.
#include <stdint.h>
#include "Scale.h"
#include "RasterFit.h"
#include "PictureOrigin.h"
#include "AxisSolution.h"

namespace Tv5725 {
class Axis {
public:
    Axis(float startConst, float startPerMag, uint16_t windowStopMin,
         uint16_t margin, uint16_t scaleMin, uint16_t captureGranularity,
         float activeFraction50Hz, float activeFraction60Hz, bool vertical);

    // Which axis this is. The one place that knows: callers pass the axis and
    // the arithmetic reads what it needs off it, rather than each call taking a
    // flag and re-deriving the same per-axis data from it.
    bool vertical() const;

    // How much of the line an untuned source is assumed to fill. Horizontal
    // barely moves across VESA, CEA and the NES; vertical splits hard on field
    // rate, because a 50 Hz source carries the same active height in a longer
    // frame. A starting point, not a derivation.
    //
    // This is what the horizontal/vertical flag used to select. It is data on
    // the axis now, so the arithmetic never asks which axis it is on.
    float activeFraction(float fieldRateHz) const;

    // write start = VDS_?B_SP + startConst + startPerMag x magnification.
    // Pipeline latency before the first write: ~25 input samples of run-up for
    // the 11-tap horizontal filter, ~1 line for the vertical line buffer.
    float startConst() const;
    float startPerMag() const;

    // Lowest VDS_?B_SP that does not corrupt the picture. Horizontally 8,
    // measured at ONE output hsync setting. Vertically 0 is an ASSUMPTION --
    // nobody has crept it.
    uint16_t windowStopMin() const;

    // Given back off the display window's far edge: the model's own worst
    // residual. A window short by 2 is invisible; long by 2 shows scratch.
    uint16_t margin() const;

    // How far this axis is willing to magnify, as a VDS_?SCALE floor. DERIVED as
    // Scale::Unity / max magnification -- 4.0x on both axes -- and it must stay
    // derived: a fixed horizontal 500 left only 73 units of zoom travel once
    // solveRaster() widened the raster, because minimumCapture() follows the
    // raster while PanAndZoom::defaultWidth() follows the input line alone.
    // Nothing in the part bounds it -- RD-5725-1.1 states no minimum and the
    // field is 10 bits -- so where interpolation starts to look bad is
    // perceptual, and the user's to find.
    uint16_t scaleMin() const;

    // The smallest change of capture POSITION this axis's hardware acts on.
    // Horizontally 2 IF units -- the low bit of IF_HB_SP2 does nothing, so a
    // one-unit move leaves the picture where it was. Vertically 1.
    // docs/scaler-geometry-model.md.
    uint16_t captureGranularity() const;

    // A move of `pixels` output pixels, in capture units, quantised to something
    // the hardware acts on: never less than one granule, always a whole number
    // of them.
    int16_t stepUnits(int16_t pixels, float magnification) const;

    // The smallest capture that can still fill `rasterTotal` at this axis's full
    // magnification. Below it cropping cannot be compensated, so the picture
    // shrinks on screen and the display window closes in around it.
    uint16_t minimumCapture(uint16_t rasterTotal) const;

    float originOffset(float magnification) const;

    // What must stay blank BEFORE the picture, in output units: whichever of the
    // write floor and the raster's own back porch is larger. Nothing can be
    // written before windowStopMin + startConst, which is physical.
    //
    // The FAR end owes nothing. Charging it the same reserve leaves a black bar
    // down the right of every picture that no zoom closes, because the scale is
    // refitted on every solve. activeStart 0 asks for the write floor alone.
    //
    // Float: startConst_ is 0.2 on the vertical axis and truncating it to 0 would
    // move every vertical solve.
    float blankingBeforePicture(uint16_t activeStart) const;

    // One past the last pixel the picture may occupy: OutputTimings::activeStop,
    // the raster total less the minimum front porch. 0 asks for the raster's own
    // edge, which is what a bypass or a custom preset gets -- there is no solved
    // raster to take a porch from.
    uint16_t farBound(uint16_t rasterTotal, uint16_t activeStop) const;

    // The biggest picture this raster can hold, bounded at the NEAR end by the
    // write floor and at the FAR end by the front porch.
    float maxDisplayWindow(uint16_t rasterTotal, uint16_t activeStart = 0,
                           uint16_t activeStop = 0) const;

    // The scale making the picture as big as this raster allows.
    //
    // The write offset grows with the magnification that depends on the size
    // being solved for, so it is solved rather than iterated:
    //
    //     produced = room - startPerMag x produced / capture
    //              = room x capture / (capture + startPerMag)
    //
    // A capture too small to fill the raster is bounded by the register at x4.
    RasterFit fitToRaster(uint16_t capture, uint16_t rasterTotal,
                    uint16_t activeStart = 0, uint16_t activeStop = 0) const;

    // Centre the picture on the raster. A picture too big to centre starts at
    // the write floor and overscans off the far end.
    PictureOrigin placePicture(float produced, uint16_t rasterTotal,
                           float magnification, uint16_t activeStart = 0) const;

    // This axis's four output registers. `capture` is IF units horizontally and
    // HALF-LINES vertically -- reading IF_VB as whole lines doubles the picture.
    AxisSolution solve(uint16_t capture, Scale scale, uint16_t rasterTotal,
                       uint16_t activeStart = 0, uint16_t activeStop = 0) const;

private:
    // The earliest a picture may START, at this magnification: past the write
    // floor and past the back porch. fitToRaster and placePicture must agree on
    // it, so it lives in one place.
    float placementFloor(float offset, uint16_t activeStart) const;

    float startConst_, startPerMag_;
    uint16_t windowStopMin_, margin_, scaleMin_, captureGranularity_;
    float activeFraction50Hz_, activeFraction60Hz_;
    bool vertical_;
};

// The two axes, defined once in Axis.cpp: `static const` in a header gives every
// translation unit its own silent copy.
extern const Axis AxisHorizontal;
extern const Axis AxisVertical;

}  // namespace Tv5725

#endif  // TV5725_AXIS_H_
