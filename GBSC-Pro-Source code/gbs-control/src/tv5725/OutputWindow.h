#ifndef TV5725_OUTPUT_WINDOW_H_
#define TV5725_OUTPUT_WINDOW_H_

// How a capture is mapped onto the output raster to fill it, and every VDS
// register that says where the picture lands: both scales and both blanking
// pairs, on both axes.
//
// The pair to CaptureWindow. That one says which part of the source is taken;
// this one says where it goes. The raster both are fitted into is OutputTiming.
//
// It also answers what capture a raster will ACCEPT -- narrowestCapture() and
// widestCapture() -- because that bound is set by the write-start model this
// class holds, not by anything the capture side knows.
//
// RD-5725-1.1 separates the two blanking pairs and AxisSolution's names follow
// it. The MEMORY pair, VDS_?B_ST/SP, is "used to get data from memory" -- the
// window the playback stage fetches through. The DISPLAY pair,
// VDS_DIS_?B_ST/SP, is the "final display" blanking, "used to clean the output
// data in blanking", which is the aperture the encoder sees.

#include <stdint.h>

#include "Axis.h"
#include "AxisSolution.h"
#include "OutputTiming.h"
#include "PictureOrigin.h"
#include "RasterFit.h"
#include "Scale.h"

namespace Tv5725 {

class OutputWindow {
public:
    // Nothing solved: every axis reads unusable, which is what a path with no
    // geometry has.
    OutputWindow();

    OutputWindow(uint16_t horizontalCapture, uint16_t verticalCapture,
                 const OutputTiming &raster);

    // One axis: its two blanking windows and what the scale produced.
    const AxisSolution &on(const Axis &axis) const;

    Scale scaleOn(const Axis &axis) const;

    bool usable() const;

    // The smallest capture that can still fill the room this raster offers, at
    // this axis's full magnification -- where letterboxing STARTS. Below it the
    // crop cannot be compensated, so the picture shrinks on screen and the
    // solve re-centres what is left. Against the ROOM and not the raster total,
    // because the picture never fills the total: the porch it is placed behind
    // is a tenth of the line here, and charging it stops the zoom that far
    // short of the magnification the axis allows.
    static uint16_t narrowestCapture(const Axis &axis, const OutputTiming &raster);

    // The largest capture this raster can SHOW. VDS_?SCALE divides 1024 and
    // tops out at Scale::Max, so the least magnification the part can express
    // is barely over 1:1 and it cannot minify at all: a capture past this
    // produces a picture past the room, and the far end is cropped rather than
    // shrunk, with the clamped scale the only trace.
    static uint16_t widestCapture(const Axis &axis, const OutputTiming &raster);

    // The arithmetic below is public only because it is what the host tests
    // assert against, one step at a time. Nothing in the firmware calls it.

    // The scale that fits a capture to the room the raster offers.
    static RasterFit fitToRaster(const Axis &axis, uint16_t capture,
                                 uint16_t rasterTotal, uint16_t activeStart = 0,
                                 uint16_t activeStop = 0);

    // This axis's four output registers, from a capture in whatever units the
    // input formatter counted it in. The display window IS the picture at both
    // ends: nothing is given back to hide the pipeline's run-up, because at
    // every clock OutputMode::EngineCeilingHz allows there is none to hide.
    // docs/investigations/display-window-opens-early.md
    static AxisSolution solve(const Axis &axis, uint16_t capture, Scale scale,
                              uint16_t rasterTotal, uint16_t activeStart = 0,
                              uint16_t activeStop = 0);

    // The biggest picture this raster can hold, bounded at the NEAR end by the
    // write floor and at the FAR end by the front porch.
    static float maxDisplayWindow(const Axis &axis, uint16_t rasterTotal,
                                  uint16_t activeStart = 0, uint16_t activeStop = 0);

    static uint16_t minimumCapture(const Axis &axis, uint16_t rasterTotal,
                                   uint16_t activeStart = 0, uint16_t activeStop = 0);
    static uint16_t maximumCapture(const Axis &axis, uint16_t rasterTotal,
                                   uint16_t activeStart, uint16_t activeStop);

    static float originOffset(const Axis &axis, float magnification);

    // Whether the WRITE FLOOR decides where the picture starts, rather than the
    // raster's own back porch. The two regimes charge the write origin
    // differently and both blankingBeforePicture() and minimumCapture() turn on
    // it, so the comparison lives in one place.
    static bool writeFloorBinds(const Axis &axis, uint16_t activeStart);

    // What must stay blank BEFORE the picture, in output units: whichever of
    // the write floor and the raster's own back porch is larger.
    //
    // The FAR end owes nothing. Charging it the same reserve leaves a black bar
    // down the right of every picture that no zoom closes, because the scale is
    // refitted on every solve. activeStart 0 asks for the write floor alone.
    static float blankingBeforePicture(const Axis &axis, uint16_t activeStart);

    // One past the last pixel the picture may occupy: OutputTiming::activeStop,
    // the raster total less the minimum front porch. 0 asks for the raster's
    // own edge, which is what a bypass or a custom preset gets.
    static uint16_t farBound(uint16_t rasterTotal, uint16_t activeStop);

    // Centre the picture on the raster. A picture too big to centre starts at
    // the write floor and overscans off the far end.
    static PictureOrigin placePicture(const Axis &axis, float produced,
                                      uint16_t rasterTotal, float magnification,
                                      uint16_t activeStart = 0);

private:
    // Pipeline latency before the first write, per axis:
    // write start = VDS_?B_SP + constant + perMagnification x magnification.
    // ~25 input samples of run-up for the 11-tap horizontal filter, ~1 line for
    // the vertical line buffer. `floor` is the lowest VDS_?B_SP that does not
    // corrupt the picture -- horizontally 8, measured at ONE output hsync
    // setting; vertically 0 is an ASSUMPTION, nobody has crept it.
    // docs/scaler-geometry-model.md
    struct WriteStart { float constant, perMagnification; uint16_t floor; };
    static const WriteStart &writeStart(const Axis &axis);

    static float placementFloor(const Axis &axis, float offset,
                                uint16_t activeStart);

    static uint16_t totalOn(const Axis &axis, const OutputTiming &raster);
    static uint16_t activeStartOn(const Axis &axis, const OutputTiming &raster);
    static uint16_t activeStopOn(const Axis &axis, const OutputTiming &raster);

    AxisSolution horizontal_, vertical_;
    Scale horizontalScale_, verticalScale_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_WINDOW_H_
