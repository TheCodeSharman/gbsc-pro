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
         uint16_t margin, uint16_t scaleMin, uint16_t captureGranularity);

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

    // What must stay blank at BOTH ends, in output units: whichever of the write
    // floor and the raster's own back porch is larger. The write floor is
    // physical -- nothing can be written before windowStopMin + startConst. The
    // back porch (OutputRaster.h) is reserved at the FAR end too, which
    // guarantees a front porch rather than assuming the encoder tolerates none.
    // activeStart 0 asks for the write floor alone.
    //
    // Float, matching geometry_math.blanking_each_end: startConst_ is 0.2 on the
    // vertical axis and truncating it to 0 would move every vertical solve.
    float blankingEachEnd(uint16_t activeStart) const;

    // The biggest picture this raster can hold. Bounded at BOTH ends, because the
    // picture is centred and equal margins are the only kind of overscan a user
    // can reason about.
    float maxDisplayWindow(uint16_t rasterTotal, uint16_t activeStart = 0) const;

    // The scale making the picture as big as this raster allows.
    //
    // The write offset grows with the magnification that depends on the size
    // being solved for, so it is solved rather than iterated:
    //
    //     produced = room - 2 x startPerMag x produced / capture
    //              = room x capture / (capture + 2 x startPerMag)
    //
    // A capture too small to fill the raster is bounded by the register at x4.
    RasterFit fitToRaster(uint16_t capture, uint16_t rasterTotal,
                    uint16_t activeStart = 0) const;

    // Centre the picture on the raster. A picture too big to centre starts at
    // the write floor and overscans off the far end.
    PictureOrigin placePicture(float produced, uint16_t rasterTotal,
                           float magnification, uint16_t activeStart = 0) const;

    // This axis's four output registers. `capture` is IF units horizontally and
    // HALF-LINES vertically -- reading IF_VB as whole lines doubles the picture.
    AxisSolution solve(uint16_t capture, Scale scale,
                       uint16_t rasterTotal, uint16_t activeStart = 0) const;

private:
    // The earliest a picture may START, at this magnification: past the write
    // floor and past the back porch. fitToRaster and placePicture must agree on
    // it, so it lives in one place.
    float placementFloor(float offset, uint16_t activeStart) const;

    float startConst_, startPerMag_;
    uint16_t windowStopMin_, margin_, scaleMin_, captureGranularity_;
};

// The two axes, defined once in Axis.cpp: `static const` in a header gives every
// translation unit its own silent copy.
extern const Axis AxisHorizontal;
extern const Axis AxisVertical;

}  // namespace Tv5725

#endif  // TV5725_AXIS_H_
