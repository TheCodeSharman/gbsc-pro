#ifndef TV5725_GEOMETRY_H_
#define TV5725_GEOMETRY_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// docs/firmware-geometry-engine.md

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "InputLine.h"
#include "CaptureWindow.h"
#include "OutputRaster.h"
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "SourceMeasurement.h"

// Global scope, NOT namespace Tv5725, or the call in sourceFieldRateOr50Hz()
// resolves to a function that does not exist.
float getSourceFieldRate(boolean useSPBus);

namespace Tv5725 {

class OutputMode;

class Geometry {
public:
    Geometry();

    const PanAndZoom &framing() const;

    // Set outright, without solving: the web handler runs in a network-stack
    // callback rather than loop(), and solving measures the source field rate,
    // which spins.
    void requestFraming(const PanAndZoom &wanted);

    bool apply();

    // Order: raster, clock, windows, rate steer LAST. Steering early corrects
    // a new clock against the old raster -- 31 Hz frame, black screen.
    // A null mode writes nothing.
    bool solveRaster(const OutputMode *mode);

    // The deferred retry, against the mode the last call named, so the caller
    // need not hold it across the settle.
    bool solveRaster();

    // Take the output raster off the chip, for the one case that does not solve
    // one: a preset table's bytes, replayed before solveRaster() runs and left
    // standing whenever it defers. Named, like adoptSampling(), because a
    // silent read-back is the same inheritance unaccounted for.
    void adoptRaster();

    // The output has gone into bypass: video routes around the VDS, so a
    // deferred solve is void and dropping it stops runSyncWatcher()'s retry
    // writing a scaled raster over the bypass setup.
    void enterBypass();

    // Settling, not unsupported. Retry the WHOLE sequence.
    bool rasterPending() const;

    // Adopted a divider rather than computed one. Remembered, or a cold boot
    // keeps whatever bypass left. Moving the divider moves the capture with it.
    bool samplingPending() const;

    // A mode change has no framing worth keeping, only the previous mode's.
    bool solveFromScratch();

    // Cheap when there is nothing to do; the sync watcher calls it every pass.
    bool solveIfPending();

    // Cleared before the solve, so an impossible framing is refused, not retried.
    bool applyRequested();

    // For the paths that compute no divider: a custom preset, and bypass.
    void adoptSampling();

    // setOverSampleRatio() must have settled rto->osr first: the sample clock
    // is the product of the divider and the oversampling.
    bool solveSampling(uint8_t oversample);

    // `produced` is capture x 1024 / scale with no horizontalTotal term, so a
    // window shifted by diffHTotal/2 lands where the picture is not.
    bool recompute();

    // OUTPUT PIXELS, sized from the scale the last solve produced.
    bool pan(int16_t dxPixels, int16_t dyPixels);

    // OUTPUT PIXELS. Positive crops in; the scale follows.
    bool zoom(int16_t dhPixels, int16_t dvPixels);

private:
    // Only the 50/60 split is taken from it.
    static float sourceFieldRateOr50Hz();

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

    PanAndZoom framing_;
    SourceMeasurement sampling_;      // the divider this engine solves against
    bool rasterPending_;     // solveRaster() refused: the field rate was not settled
    bool samplingPending_;   // solveSampling() adopted a fallback divider
    bool solvePending_;      // a solve refused because the source was settling
    bool framingRequested_;  // the user asked; loop() drains it, freeze does not
    const OutputMode *rasterMode_;  // the mode the last solveRaster() was given

    // The output raster in force, held rather than read back off VDS_?SYNC_RST.
    // Zero means there is none, which is what bypass looks like.
    uint16_t rasterLinePx_, rasterFrameLines_;
    Scale horizontalScale_, verticalScale_;  // what the last solve produced

    // Where the front porch starts, from the raster this engine solved. Held
    // rather than re-derived because apply() reads the raster back off the chip
    // and the registers carry no porch. 0 until a raster is solved, which is what
    // a bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
};

}  // namespace Tv5725

#endif  // TV5725_GEOMETRY_H_
