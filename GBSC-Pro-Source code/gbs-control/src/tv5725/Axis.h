#ifndef GEOMETRY_AXIS_H_
#define GEOMETRY_AXIS_H_

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
         uint16_t margin);

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

    float originOffset(float magnification) const;

    // The biggest picture this raster can hold. Bounded by the write floor at
    // BOTH ends, because the picture is centred and equal margins are the only
    // kind of overscan a user can reason about.
    float maxDisplayWindow(uint16_t rasterTotal) const;

    // The scale making the picture as big as this raster allows.
    //
    // The write offset grows with the magnification that depends on the size
    // being solved for, so it is solved rather than iterated:
    //
    //     produced = room - 2 x startPerMag x produced / capture
    //              = room x capture / (capture + 2 x startPerMag)
    //
    // A capture too small to fill the raster is bounded by the register at x4.
    RasterFit fitToRaster(uint16_t capture, uint16_t rasterTotal) const;

    // Centre the picture on the raster. A picture too big to centre starts at
    // the write floor and overscans off the far end.
    PictureOrigin placePicture(float produced, uint16_t rasterTotal,
                           float magnification) const;

    // This axis's four output registers. `capture` is IF units horizontally and
    // HALF-LINES vertically -- reading IF_VB as whole lines doubles the picture.
    AxisSolution solve(uint16_t capture, Scale scale,
                       uint16_t rasterTotal) const;

private:
    float startConst_, startPerMag_;
    uint16_t windowStopMin_, margin_;
};

// The two axes, defined once in Axis.cpp: `static const` in a header gives every
// translation unit its own silent copy.
extern const Axis AxisHorizontal;
extern const Axis AxisVertical;

}  // namespace Tv5725

#endif  // GEOMETRY_AXIS_H_
