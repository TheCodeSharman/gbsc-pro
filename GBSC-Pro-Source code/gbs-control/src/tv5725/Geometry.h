#ifndef TV5725_GEOMETRY_H_
#define TV5725_GEOMETRY_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// See docs/firmware-geometry-engine.md.
//
// A header rather than a block in the .ino: arduino-cli inserts generated
// forward prototypes above any struct declared further down, so a function
// taking a struct parameter fails to compile at a line that looks fine.

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "InputLine.h"
#include "CaptureWindow.h"
#include "OutputRaster.h"
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "Sampling.h"

// Global scope, NOT namespace Tv5725, or the call in sourceFieldRateOr50Hz()
// resolves to a function that does not exist.
float getSourceFieldRate(boolean useSPBus);

namespace Tv5725 {

// Defined in OutputRaster.h, deliberately not included: only a pointer is held,
// and including it would pull the raster arithmetic into every user.
class OutputMode;

// The geometry engine: the user's framing, and every TV5725 register that is an
// output of it.
class Geometry {
public:
    Geometry();

    const PanAndZoom &framing() const;

    // Set outright, without solving: the web handler runs in a network-stack
    // callback rather than loop(), and solving measures the source field rate,
    // which spins.
    void requestFraming(const PanAndZoom &wanted);

    // Every register from the framing and the rasters. The capture window is
    // DERIVED; nothing is read back to decide it.
    bool apply();

    // The OUTPUT raster: both totals, both sync pulses, and the display clock
    // seed that affords them. `mode` comes from the user's preference, via
    // OutputRaster::modeForPreference(); NULL, or a frame height belonging to
    // no OutputMode, writes NOTHING and leaves the raster registers alone.
    //
    // **CALL IT BEFORE externalClockGenResetClock(), NOT AFTER**, which reads
    // PLL648_CONTROL_01 to program the Si5351. The order is raster, clock,
    // windows, then the rate steer LAST -- steering early corrects a new clock
    // against the old raster, which gives a 31 Hz frame and a black screen.
    // docs/investigations/preset-abandonment-audit.md
    bool solveRaster(const OutputMode *mode);

    // The deferred retry, against the mode the last call named, so the caller
    // need not hold it across the settle.
    bool solveRaster();

    // The output has gone into bypass: video routes around the VDS, so a
    // deferred solve is void and dropping it stops runSyncWatcher()'s retry
    // writing a scaled raster over the bypass setup.
    void enterBypass();

    // True when solveRaster() refused because the source was still settling,
    // rather than because the mode has no timings. The caller must retry the
    // WHOLE sequence -- raster, clock, windows -- which is why the retry lives
    // in the sketch beside externalClockGenResetClock().
    bool rasterPending() const;

    // True when solveSampling() has no measurable line rate and adopts the
    // divider already on the chip. Adopting is necessary -- an engine with no
    // divider defers every solve forever -- but it must be REMEMBERED as a
    // fallback, or a cold boot keeps whatever divider bypass left and only a
    // manual reset clears it. Retry the whole sequence: moving the divider moves
    // the capture window with it.
    bool samplingPending() const;

    // A mode change has no framing worth keeping, only the previous mode's.
    bool solveFromScratch();

    // Finish a solve the source was too unsettled to allow. Cheap when there is
    // nothing to do, so the sync watcher can call it every pass. Freeze stops it.
    bool solveIfPending();

    // Apply a requested framing, if one is waiting. The request is cleared before
    // the solve, so one that cannot be met is refused rather than retried on
    // every pass.
    bool applyRequested();

    // The ADC sampling divider this engine solves against: state, not a register
    // read. adopt() is for the paths that compute none -- a custom preset, and
    // bypass -- and writes all three registers, so it cannot go half-applied.
    void adoptSampling();

    // Compute the divider from the line rate and write all three registers.
    // `lineRateHz` is field rate x source lines; setOverSampleRatio() must have
    // settled rto->osr first, because the sample clock is the product of both.
    //
    // False when the rate is unmeasurable, falling back to adopt() or to the
    // previous choice. Refusing to compute from a measurement that did not
    // happen is the rule; refusing to hold a divider at all is not the same thing.
    bool solveSampling(uint32_t lineRateHz, uint8_t oversample);

    // Re-solve against the raster as it stands, for callers that have just
    // changed the output timing: `produced` is capture x 1024 / scale and has no
    // horizontalTotal term, so a window shifted by diffHTotal/2 lands where the
    // picture is not.
    bool recompute();

    // Which part of the source is grabbed, in OUTPUT PIXELS. The engine holds
    // the scale it solved, so a press is sized from that rather than from a
    // register read back off the chip.
    bool pan(int16_t dxPixels, int16_t dyPixels);

    // Zoom, in OUTPUT PIXELS. Positive crops in; the picture stays as big as
    // the raster allows and the scale follows.
    bool zoom(int16_t dhPixels, int16_t dvPixels);

private:
    // Only the 50/60 split is taken from it, so anything outside FrameSync's own
    // sanity window is treated as unmeasured rather than believed.
    static float sourceFieldRateOr50Hz();

    bool fail();

    // Output pixels -> input units. A press of nothing has to be skipped
    // outright: stepUnits() floors at one granule, so an axis the press did not
    // name would drift a unit per press.
    static int16_t unitsFor(int16_t pixels, const Scale &scale, const Axis &axis);

    bool readCapture(CaptureWindow &capture);

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    static void write(const RegisterSolution &solved,
                      const CaptureWindow &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    PanAndZoom framing_;
    Sampling sampling_;      // the divider this engine solves against
    bool rasterPending_;     // solveRaster() refused: the field rate was not settled
    bool samplingPending_;   // solveSampling() adopted a fallback divider
    bool solvePending_;      // a solve refused because the source was settling
    bool framingRequested_;  // the user asked; loop() drains it, freeze does not
    const OutputMode *rasterMode_;  // the mode the last solveRaster() was given
    Scale horizontalScale_, verticalScale_;  // what the last solve produced

    // Where the front porch starts, from the raster this engine solved. Held
    // rather than re-derived because apply() reads the raster back off the chip
    // and the registers carry no porch. 0 until a raster is solved, which is what
    // a bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
};

}  // namespace Tv5725

#endif  // TV5725_GEOMETRY_H_
