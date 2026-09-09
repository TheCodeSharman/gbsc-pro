#ifndef TV5725_VIDEO_PATH_H_
#define TV5725_VIDEO_PATH_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// docs/firmware-geometry-engine.md

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "DisplayClock.h"
#include "InputLine.h"
#include "CaptureWindow.h"
#include "OutputChoice.h"
#include "OutputMode.h"
#include "OutputTimings.h"
#include "PanAndZoom.h"
#include "VideoProcessorTimings.h"
#include "FramingTable.h"
#include "SourceKey.h"
#include "SourceMeasurement.h"

namespace Tv5725 {

class OutputMode;

class VideoPath {
public:
    VideoPath(DisplayClock &displayClock, SourceMeasurement &sampling,
              FramingTable &framings);

    const PanAndZoom &framing() const;

    // The source the framing held is against. Invalid until one has been
    // measured, so a caller storing a framing against it has to ask first.
    const SourceKey &framedKey() const;

    // The capturable region the last solve ran against, which is the
    // denominator the framing's proportions are taken against.
    uint16_t capturableOn(const Axis &axis) const;

    // The framing on this axis in input units, against that region.
    // docs/scaler-geometry-model.md
    uint16_t originUnitsOn(const Axis &axis) const;
    uint16_t extentUnitsOn(const Axis &axis) const;

    // Supplies the probe. Without one the engine leaves the sync path alone.
    //
    // **Per mode change, not per input.** A source can change its sync type
    // without the mux moving -- a RISC PC sets it from CMOS -- so a change is
    // the only signal there is that it may have moved.
    // docs/sync-type-selection.md
    void useSyncTypeProbe(bool (*hasOwnVsync)());

    // The source's timings moved, so the capture window and everything solved
    // from it are stale. The registers are not written until the source has
    // settled, and the choice does not become a resolution until the field rate
    // behind it has been measured.
    void inputTimingsChanged(uint8_t oversample);

    // The same event at the oversampling already held, which is what a source
    // event wants: the oversampling is an output of the last solve.
    void inputTimingsChanged();

    // The user picked a different output resolution. Not a source event: the
    // rate and the divider the last solve measured still describe the source, so
    // this re-solves raster, clock and windows from what is held and measures
    // nothing. False where a timings change is still in flight, which will
    // resolve the choice against its own measurement when it lands.
    bool outputModeChanged(const OutputChoice &choice);

    enum PollOutcome {
        PollIdle,
        PollResolved,      // a deferred solve; the source was not re-read
        PollSolved,        // a mode change completed, against solvedLines()
        PollUnmeasurable,
    };

    // Establish the sync path, the scan mode and a known sampling clock, so that
    // what the caller measures next means something. Measures nothing itself.
    // False when no mode change is outstanding.
    //
    // Sync type, then scan mode, then sampling clock, and all three before any
    // count is taken. Each one corrupts every measurement below it if left set
    // for the previous source. docs/input-acquisition.md
    bool prepareToMeasure();

    // Solve every register from the measurement the caller has just taken.
    PollOutcome solveFromMeasurement();

    // Retry a solve that was refused against a reading already taken. Needs no
    // fresh measurement.
    PollOutcome pollDeferred();

    // Whether a mode change is still working through: told the source moved and
    // not yet finished solving for it. What the sync output blanks against.
    bool changing() const;

    // Narrower than changing(): a deferred solve does not stop the caller looking
    // for a source event, but a mode change in flight does.
    bool changingMode() const;

    // Probe the sync type again and put the chip on the answer, for the
    // escalation a source that will not lock reaches. Returns whether the source
    // carries composite sync.
    //
    // IT WRITES THE PATH WHATEVER THE HELD VALUE SAYS, and nothing else
    // reconciles a register on the wrong path with a held value already right.
    // docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md
    bool reacquireSyncType();

    bool reset();

    // Notify the engine that the output has gone into bypass: video routes
    // around the VDS, so no solve is coming.
    //
    // EVERY PATH INTO BYPASS HAS TO SAY SO. Only a completed solve clears a mode
    // change, and bypass never solves -- so an armed one is retried once sync
    // stabilises and overwrites the setup bypass just chose.
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

    // What the output is doing, as one question. Null only before anything has
    // been solved; ModeBypass -- isBypass() -- while video routes around the VDS.
    const OutputMode *outputMode() const;

private:

    // The raster is the held one, never a read-back.
    bool solveWindows();

    // A solve that keeps the framing when the SOURCE is the one it was tuned
    // against, and drops it when it is not: the proportions are taken against
    // the capturable region, which the output raster does not touch.
    // docs/framing-presets.md
    bool solveForSource();

    // Order: raster, clock, windows, rate steer LAST. Steering early corrects a
    // new clock against the old raster -- 31 Hz frame, black screen. A choice
    // that names no resolution writes nothing and is refused rather than
    // deferred.
    bool solveRaster();

    // Take the output raster off the chip, for the one case that does not solve
    // one: a preset table's bytes, left standing whenever solveRaster() defers.
    // Named, because a silent read-back is inheritance unaccounted for.
    void adoptRaster();

    // The oversampling must have settled first: the sample clock is the product
    // of the divider and it.
    bool solveSampling(uint8_t oversample);

    // **Before solveSampling(), because the divider derives from it**: the
    // capture write limit doubles with the line doubler, so the two describe one
    // decision and the wrong order sizes the divider for the previous source.
    void solveScanMode();

    bool fail();

    void establishSyncType();

    // Output pixels -> input units. A press of nothing has to be skipped
    // outright: stepUnits() floors at one granule, so an axis the press did not
    // name would drift a unit per press.
    static int16_t unitsFor(int16_t pixels, const Scale &scale, const Axis &axis);

    bool measureSourceTimings(CaptureWindow &capture);
    bool calculateInputFormatterRegisters(CaptureWindow &capture);
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
    SourceMeasurement &sampling_;
    bool scanModeApplied_;
    bool syncTypeProbed_;
    bool (*syncProbe_)();
    SourceKey framedKey_;
    FramingTable &framings_;
    bool solvePending_;
    bool modePending_;
    uint8_t modeOversample_;
    OutputChoice choice_;
    const OutputMode *rasterMode_;

    // The output raster in force, held rather than read back off VDS_?SYNC_RST.
    // Zero means there is none, which is what bypass looks like.
    uint16_t rasterLinePx_, rasterFrameLines_;
    Scale horizontalScale_, verticalScale_;

    // Where the front porch starts, from the raster this engine solved. The
    // registers carry no porch, so there is nothing to read back. 0 until a
    // raster is solved, which is what bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PATH_H_
