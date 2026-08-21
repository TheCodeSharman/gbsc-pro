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
#include "CaptureWindow.h"
#include "InputLine.h"
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

// The capture window and the two rasters it has to fit, read from the chip in
// one place so nothing downstream reads a register to decide what to write.
class Capture {
public:
    Capture();

    uint16_t horizontalStop() const;
    uint16_t horizontalStart() const;
    uint16_t verticalStop() const;
    uint16_t verticalStart() const;
    uint16_t linePx() const;
    uint16_t frameLines() const;

    // The horizontal line knows what the hsync pulse takes off each end; the
    // vertical does not, because nothing has measured the vsync equivalent and
    // a guess there would crop picture rather than blanking.
    InputLine lineH() const;
    InputLine lineV() const;

    uint16_t captureH() const;
    uint16_t captureV() const;

    // In RGBHV bypass the VDS is out of the video path and there is nothing to
    // solve; both rasters read back as nearly zero.
    bool scaling() const;

    bool usable() const;

    // A 97/98 reading mid-preset-change is a measurement in progress, not a
    // mode: the smallest real ones the VDS scales are 262 and 312.
    static const uint16_t SourceVerticalTotalMin = 200;
    static const uint16_t SourceVerticalTotalMax = 1300;

    // Above this the source is a 50 Hz standard -- PAL-like ~312 lines, NTSC-like
    // ~262. Sanity-checks a measured field rate, which is transient during a
    // preset load where the line count is not.
    static const uint16_t PalVerticalTotalMin = 290;

    // IF_LINE_ST. Chosen, not derived -- nothing explains 64.
    static const uint16_t ProgressiveStart = 64;

    // Read the rasters and where each capture window rolls over. False when the
    // source has not settled far enough to derive a window from.
    //
    // The sampling is an ARGUMENT, not a read. PLLAD_LAT is what loads the
    // divider into the ADC PLL, so between a write and the latch the register
    // reports a value the chip is not using. Everything read here is a raster
    // or a live measurement; the divider is a choice, and belongs to whoever
    // made it.
    bool readRasters(const Sampling &sampling);

    void setWindows(const CaptureWindow &h, const CaptureWindow &v);

private:
    uint16_t horizontalStop_, horizontalStart_;      // IF_HB_SP2 / ST2, IF units
    uint16_t verticalStop_, verticalStart_;      // IF_VB_SP / ST, HALF-LINES
    uint16_t linePx_;         // output raster total, horizontal
    uint16_t frameLines_;     // output raster total, vertical
    uint16_t wrapH_, wrapV_;  // where each capture window rolls over
    uint16_t hlowLen_;        // hsync low, ADC samples
    uint16_t adcLine_;        // the whole line in the same samples
};

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

    // Which part of the source is grabbed, in INPUT UNITS.
    bool pan(int16_t dx, int16_t dy);

    // Zoom, in INPUT UNITS cropped off the default width. Positive crops in;
    // the picture stays as big as the raster allows and the scale follows.
    bool zoom(int16_t dh, int16_t dv);

private:
    // Only the 50/60 split is taken from it, so anything outside FrameSync's own
    // sanity window is treated as unmeasured rather than believed.
    static float sourceFieldRateOr50Hz();

    bool fail();

    bool readCapture(Capture &capture);

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    static void write(const RegisterSolution &solved,
                      const Capture &capture);

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

    // Where the front porch starts, from the raster this engine solved. Held
    // rather than re-derived because apply() reads the raster back off the chip
    // and the registers carry no porch. 0 until a raster is solved, which is what
    // a bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
};

}  // namespace Tv5725

#endif  // TV5725_GEOMETRY_H_
