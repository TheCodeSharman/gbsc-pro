#ifndef TV5725_GEOMETRY_H_
#define TV5725_GEOMETRY_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// docs/firmware-geometry-engine.md

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "DisplayClock.h"
#include "InputLine.h"
#include "CaptureWindow.h"
#include "OutputMode.h"
#include "RasterSolution.h"
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "SourceMeasurement.h"

namespace Tv5725 {

class OutputMode;

class Geometry {
public:
    explicit Geometry(DisplayClock &displayClock);

    const PanAndZoom &framing() const;

    // Notify the engine that the source has changed mode. The registers are
    // not written until the source has settled. 
    void modeChanged(const OutputMode *mode, bool customPreset,
                     uint8_t oversample);

    // Called by the sketch main loop - allows the engine to determine when the
    // source has settled and apply any pending mode changes. True on the pass
    // that completes a mode change.
    bool poll();

    void reset();

    // Notify the engine that the output has gone into bypass: so video routes around the VDS.
    void enterBypass();

    bool pan(int16_t dxPixels, int16_t dyPixels);
    bool zoom(int16_t dhPixels, int16_t dvPixels);

    // Re-solve every register from the framing held and the source as it reads
    // now. Measures, so it must run from loop().
    bool resolve();

private:

    // Every window and both scales, from the framing and the source as the last
    // measurement found it. The raster is the held one, never a read-back.
    bool solveWindows();

    // A mode change has no framing worth keeping, only the previous mode's.
    bool solveFromScratch();

    // Order: raster, clock, windows, rate steer LAST. Steering early corrects
    // a new clock against the old raster -- 31 Hz frame, black screen.
    // A null mode writes nothing and is refused rather than deferred.
    bool solveRaster(const OutputMode *mode);

    // The deferred retry, against the mode the last call named.
    bool solveRaster();

    // Take the output raster off the chip, for the one case that does not solve
    // one: a preset table's bytes, replayed before solveRaster() runs and left
    // standing whenever it defers. Named, like adoptSampling(), because a
    // silent read-back is the same inheritance unaccounted for.
    void adoptRaster();

    // For the paths that compute no divider: a custom preset, and bypass.
    void adoptSampling();

    // The oversampling must have settled first: the sample clock is the product
    // of the divider and it.
    bool solveSampling(uint8_t oversample);

    // Only the 50/60 split is taken from it.
    float sourceFieldRateOr50Hz() const;

    bool fail();

    // The divider, the IF line counter and the retime stop, to their owners.
    void writeSampling();

    // Output pixels -> input units. A press of nothing has to be skipped
    // outright: stepUnits() floors at one granule, so an axis the press did not
    // name would drift a unit per press.
    static int16_t unitsFor(int16_t pixels, const Scale &scale, const Axis &axis);

    // The source, as the sync processor counts it: the vertical and horizontal
    // timings, and the maximum capture they allow.
    bool measureSourceTimings(CaptureWindow &capture);

    // The user's chosen portion of that maximum, as the IF window on each axis.
    bool calculateInputFormatterRegisters(CaptureWindow &capture);

    // The scales and output windows that map that capture to the full screen.
    RegisterSolution calculateOutputRaster(const CaptureWindow &capture) const;

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    static void write(const RegisterSolution &solved,
                      const CaptureWindow &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    DisplayClock &displayClock_;
    PanAndZoom framing_;
    SourceMeasurement sampling_;      // the divider this engine solves against
    bool rasterPending_;     // solveRaster() refused: the field rate was not settled
    bool samplingPending_;   // solveSampling() adopted a fallback divider
    bool solvePending_;
    bool modePending_;
    bool modeIsCustomPreset_;
    uint8_t modeOversample_;      // a solve refused because the source was settling
    const OutputMode *rasterMode_;  // the mode the last solveRaster() was given

    // The output raster in force, held rather than read back off VDS_?SYNC_RST.
    // Zero means there is none, which is what bypass looks like.
    uint16_t rasterLinePx_, rasterFrameLines_;
    Scale horizontalScale_, verticalScale_;  // what the last solve produced

    // Where the front porch starts, from the raster this engine solved. The
    // registers carry no porch, so there is nothing to read back. 0 until a
    // raster is solved, which is what bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
};

}  // namespace Tv5725

#endif  // TV5725_GEOMETRY_H_
