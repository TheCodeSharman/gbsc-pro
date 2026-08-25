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
         uint16_t scaleMin, uint16_t captureGranularity,
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

    // write start = VDS_?B_SP + startConst + startPerMag x magnification.
    // Pipeline latency before the first write: ~25 input samples of run-up for
    // the 11-tap horizontal filter, ~1 line for the vertical line buffer.
    float startConst() const;
    float startPerMag() const;

    // Lowest VDS_?B_SP that does not corrupt the picture. Horizontally 8,
    // measured at ONE output hsync setting. Vertically 0 is an ASSUMPTION --
    // nobody has crept it.
    uint16_t windowStopMin() const;

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

    // The largest capture this raster can SHOW. VDS_?SCALE divides 1024 and
    // tops out at Scale::Max, so the least magnification the part can express
    // is barely over 1:1 and it cannot minify at all: a capture past this
    // produces a picture past the room, and the far end is cropped rather than
    // shrunk, with the clamped scale the only trace.
    uint16_t maximumCapture(uint16_t rasterTotal, uint16_t activeStop) const;

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

    // This axis's four output registers, from a capture in whatever units the
    // input formatter counted it in. The display window IS the picture at both
    // ends: nothing is given back to hide the pipeline's run-up, because at
    // every clock OutputMode::EngineCeilingHz allows there is none to hide.
    // docs/investigations/display-window-opens-early.md
    AxisSolution solve(uint16_t capture, Scale scale, uint16_t rasterTotal,
                       uint16_t activeStart = 0, uint16_t activeStop = 0) const;

private:
    // The earliest a picture may START, at this magnification: past the write
    // floor and past the back porch. fitToRaster and placePicture must agree on
    // it, so it lives in one place.
    float placementFloor(float offset, uint16_t activeStart) const;

    float startConst_, startPerMag_;
    uint16_t windowStopMin_, scaleMin_, captureGranularity_;
    float activeStart_, activeExtent_;
    bool vertical_;
};

// The two axes, defined once in Axis.cpp: `static const` in a header gives every
// translation unit its own silent copy.
extern const Axis AxisHorizontal;
extern const Axis AxisVertical;

}  // namespace Tv5725

#endif  // TV5725_AXIS_H_
