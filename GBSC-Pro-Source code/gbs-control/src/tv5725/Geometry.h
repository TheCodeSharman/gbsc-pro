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
#include "OutputTimings.h"
#include "PanAndZoom.h"
#include "VideoProcessorTimings.h"
#include "FramingTable.h"
#include "SourceKey.h"
#include "SourceMeasurement.h"

namespace Tv5725 {

class OutputMode;

class Geometry {
public:
    explicit Geometry(DisplayClock &displayClock);

    const PanAndZoom &framing() const;

    // What has been tuned, against the sources it was tuned for. The engine
    // puts a framing in when the user leaves the source; whoever owns the file
    // reads it back at boot and writes it out. docs/framing-presets.md
    const FramingTable &framings() const;
    bool rememberFraming(const SourceKey &key, const PanAndZoom &framing);

    // The source the framing held is against. Invalid until one has been
    // measured, so a caller storing a framing against it has to ask first.
    const SourceKey &framedKey() const;

    // Moves whenever the table does. A pad press must not write flash, so the
    // sketch debounces -- and comparing this against what it last wrote is how
    // it knows a write is owed at all rather than paying for one every tick.
    uint16_t framingRevision() const;

    // The capturable region the last solve ran against, which is the
    // denominator the framing's proportions are taken against. A caller holding
    // a proportion needs it to say what that is in input units.
    uint16_t capturableOn(const Axis &axis) const;

    // The framing on this axis in input units, against that region. The
    // proportion is the state; this is the same window in the units the
    // console, the bench instruments and docs/scaler-geometry-model.md speak,
    // and the engine is the only thing holding the denominator to convert with.
    uint16_t originUnitsOn(const Axis &axis) const;
    uint16_t extentUnitsOn(const Axis &axis) const;

    // Notify the engine that the source has changed mode. The registers are
    // not written until the source has settled.
    void modeChanged(const OutputMode *mode,
                     uint8_t oversample);

    // Called by the sketch main loop - allows the engine to determine when the
    // source has settled and apply any pending mode changes. True on the pass
    // that completes a mode change.
    bool poll();

    // Whether a mode change is still working through: told the source moved and
    // not yet finished solving for it. What the sync output blanks against.
    bool changing() const;

    // The source disturbed, as the chip latched it. Arms a re-measure, which
    // the line count alone cannot: a source returning at the same count and a
    // different field rate moves nothing sourceMoved() can see.
    void sourceInterrupted();

    bool reset();

    // Notify the engine that the output has gone into bypass: so video routes around the VDS.
    void enterBypass();

    bool pan(int16_t dxPixels, int16_t dyPixels);
    bool zoom(int16_t dhPixels, int16_t dvPixels);

    // A whole framing at once, for whoever restores one a user stored: the
    // proportions become the live framing and every register is re-solved from
    // them. The engine calculates -- a stored framing is never replayed as
    // registers. docs/framing-presets.md
    bool applyFraming(const PanAndZoom &framing);

    // Re-solve every register from the framing held and the source as it reads
    // now. Measures, so it must run from loop().
    bool resolve();

    // The source field rate the last solve ran at. Held, not measured here, so
    // a network callback may ask.
    float sourceFieldRateHz() const;

    // Whether the source runs a 15 kHz line, and the held rate that says so.
    bool sourceLowLineRate() const;
    uint32_t sourceLineRateHz() const;

private:

    // Every window and both scales, from the framing and the source as the last
    // measurement found it. The raster is the held one, never a read-back.
    bool solveWindows();

    // A solve that keeps the framing when the SOURCE is the one it was tuned
    // against, and drops it when it is not. applyPresets() calls modeChanged()
    // for a source mode change and for the user picking a different output
    // resolution, and only the first of those invalidates a framing: the
    // proportions are taken against the capturable region, which the output
    // raster does not touch. docs/framing-presets.md
    bool solveForSource();

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

    // The oversampling must have settled first: the sample clock is the product
    // of the divider and it.
    bool solveSampling(uint8_t oversample);

    // Which scan mode the measured source line count calls for, held and
    // written. Both the capture width and the frame the vertical window sits on
    // are computed from the line doubling it applies.
    //
    // **Before solveSampling(), because the divider derives from it**: the
    // capture write limit doubles with the line doubler, so the two describe
    // one decision and the wrong order sizes the divider for the previous
    // source.
    // Whether the source has settled on a line count the last solve did not run
    // against. The engine's own measurement, so a mode change needs nobody to
    // announce it.
    bool sourceMoved();

    void solveScanMode();

    bool fail();

    // The known sampling state a measurement is taken from, before it is taken.
    // Disturbing, so it belongs where the source has just changed and the
    // picture is moving anyway -- never on a schedule.
    void holdReferenceSampling();

    // The divider, the IF line counter and the retime stop, to their owners.
    void writeSampling();

    // A divider the source cannot lock to, moved off. Always false: correcting
    // a divider is not a solve, and the mode change has not landed.

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
    VideoProcessorTimings calculateOutputRaster(const CaptureWindow &capture) const;

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    static void write(const VideoProcessorTimings &solved,
                      const CaptureWindow &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    DisplayClock &displayClock_;
    PanAndZoom framing_;
    // The capturable region the last solve ran against, per axis: the
    // denominator a press converts its units into a proportion with.
    uint16_t usableHorizontal_, usableVertical_;
    SourceMeasurement sampling_;      // the divider this engine solves against
    bool samplingPending_;   // solveSampling() adopted a fallback divider
    bool sourceInterrupted_; // the chip latched a disturbance, and nothing has re-measured
    uint32_t referenceRateHz_; // the estimate the reference sample rate was sized from
    bool scanModeApplied_;   // the registers have been written for this mode change
    uint16_t solvedLines_;   // the source line count the last solve ran against
    SourceKey framedKey_;    // the source the framing held was tuned against
    FramingTable framings_;
    uint16_t framingRevision_;
    uint16_t idleLines_;     // the count seen while no mode change is outstanding
    uint8_t idleRun_;        // how many polls it has held it
    bool solvePending_;
    bool modePending_;
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
