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
#include "OutputRaster.h"
#include "PanAndZoom.h"
#include "RegisterSolution.h"
#include "SourceMeasurement.h"

namespace Tv5725 {

class OutputMode;

class Geometry {
public:
    explicit Geometry(DisplayClock &displayClock);

    const PanAndZoom &framing() const;

    // Set outright, without solving: the web handler runs in a network-stack
    // callback rather than loop(), and solving measures the source field rate,
    // which spins.
    void requestFraming(const PanAndZoom &wanted);

    // The horizontal total was retimed outside the engine, by the htotal search
    // on a board with no clock generator to steer instead. The engine holds the
    // raster rather than reading it back, so it has to be told: every window and
    // both scales are fitted to the total this names.
    bool rasterWidthChanged(uint16_t horizontalTotal);

    // The source is about to change mode. Everything the solve will need that
    // cannot be re-derived once it has rides along: videoStandardInput is
    // rewritten by PresetLoad::videoStandardInputAfterLoad(), so the mode the
    // choice was made from is gone by the time the raster is solved.
    //
    // Nothing is solved here. The measurements a solve needs -- field rate,
    // line count, hsync width -- lag the mode change by seconds, and read early
    // they do not fail, they return plausible garbage: 54.47, 55.12 and 66.79 Hz
    // on a source that runs 50.08, for rasters of 1761, 1740 and 1436 against
    // the 1918 due.
    void modeChanged(const OutputMode *mode, bool customPreset,
                     uint8_t oversample);

    // Drive whatever is outstanding: a pending mode change once the source has
    // settled into it, and any framing the user asked for. Cheap when there is
    // nothing to do and cheap while the source is still moving, so it can be
    // called on every pass.
    //
    // True on the pass that COMPLETES a mode change, which is when the display
    // clock has moved and the first frame is worth showing.
    bool poll();

    // Back to the default framing, with everything the engine owns re-solved
    // from the source as it reads now. The way out of a framing the user has
    // zoomed into a corner, and the only one short of a reboot.
    void reset();

    // The output has gone into bypass: video routes around the VDS, so an
    // outstanding solve is void, and dropping it stops a later poll writing a
    // scaled raster over the bypass setup.
    void enterBypass();

    // OUTPUT PIXELS, sized from the scale the last solve produced.
    bool pan(int16_t dxPixels, int16_t dyPixels);

    // OUTPUT PIXELS. Positive crops in; the scale follows.
    bool zoom(int16_t dhPixels, int16_t dvPixels);

private:
    // Measure the source, then solve. The entry points that are not a poll pass
    // have no measurement of their own.
    bool apply();

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
    bool framingRequested_;  // the user asked; loop() drains it, freeze does not
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
