#ifndef TV5725_VIDEO_PATH_H_
#define TV5725_VIDEO_PATH_H_

// Where the geometry meets the TV5725's registers, and the only place they meet.
// docs/firmware-geometry-engine.md

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "DisplayClock.h"
#include "VideoSourceLine.h"
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
class InputFormatter;

/*
    VideoPath configures the TV5725 to show a capture window, framed as asked,
    in the chosen output mode. It solves the sampling clock, the output raster,
    the display clock and both windows from a measurement and a framing, and
    owns the route the video takes -- scaled, or handed to the panel.

    It measures nothing. Every characteristic of the source arrives from
    VideoSourceAcquisition, which owns the measuring.

    Four events reach it:
        - the sync type, which moves per source MODE change and not per input
        - the source's timings moved, so the capture is re-solved
        - the output mode moved, so the raster is re-solved
        - the framing moved, so the windows are re-solved
*/

class VideoPath {
public:
    VideoPath(DisplayClock &displayClock, SourceMeasurement &sampling,
              FramingTable &framings, InputFormatter &inputFormatter);

    const PanAndZoom &framing() const;

    // The source the framing held is against. Invalid until one has been
    // measured, so a caller storing a framing against it has to ask first.
    const SourceKey &framedKey() const;

    // The capturable region the last solve ran against, which is the
    // denominator the framing's proportions are taken against.
    uint16_t lineUnitsOn(const Axis &axis) const;

    // reachOn() is the last unit a window may stop on and firstUnitOn() the
    // earliest it may open.
    uint16_t firstUnitOn(const Axis &axis) const;
    uint16_t reachOn(const Axis &axis) const;

    // The source line active video starts on, for a path that plays the
    // source's raster out rather than scaling it. Zero where the measurement
    // matched no published raster, which is what an untuned source gets: its
    // own porches are already black, so blanking a guessed count costs picture.
    //
    // Established when the reading ARRIVES, not when the solve runs -- the
    // three values that identify a raster are all measured, and pass-through
    // never solves. docs/video-source-acquisition.md
    uint16_t sourceActiveStartLine() const;

    // The published raster the measurement matched, unpublished where none did.
    // Pass-through blanks both axes from it, so it is handed over whole rather
    // than as one derived number per axis.
    const SourceTiming &sourceTiming() const;

    // The framing on this axis in input units, against that region.
    // docs/scaler-geometry-model.md
    uint16_t originUnitsOn(const Axis &axis) const;
    uint16_t extentUnitsOn(const Axis &axis) const;

    // Hold the framing at the whole capturable region and ignore every press,
    // so a bench run can check one rule against any source and any output: at
    // 100% the capture takes the source's blanking on all four sides, and the
    // scaler is never asked to minify to show it. Releasing it gives the held
    // framing back.
    void forceFullFraming(bool on);
    bool fullFramingForced() const;

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

    // Configure the chip for this output mode: a resolution, ModeBypass to hand
    // the source to the panel, or 0 for a custom preset, which names no
    // resolution and leaves the raster standing on the chip.
    //
    // THE ARGUMENT CARRIES BOTH DIRECTIONS. ModeBypass is pass-through and a
    // resolution is not, so being told a resolution IS the leave -- there is no
    // second entry point to acquire steps this one lacks, which is how the leave
    // came to go without the bring-up, the block restart and the colour matrix.
    // docs/investigations/pass-through-holds-the-only-field-rate-instrument.md
    //
    // WHEN to say it is not decided here. Pass-through is a statement about the
    // source, so the layer that measures answers it.
    //
    // A resolution is not a source event -- the rate and the divider the last
    // solve measured still describe the source, so raster, clock and windows are
    // re-solved from what is held and nothing is measured. False where the
    // caller has to load a preset instead: a change already in flight, a mode
    // naming no resolution, or pass-through just left, which is solved by the
    // measurement that follows rather than from the rate it was entered on.
    bool setOutputMode(const OutputMode *mode);

    enum PollOutcome {
        PollIdle,
        PollSolved,        // a mode change completed, against solvedLines()
        PollUnmeasurable,
    };

    // The oversampling the last source event asked for. The reference sampling
    // clock is applied at it, by the layer that takes the measurement it exists
    // to make meaningful.

    // A solve was refused against what it was given, so it is worth trying again
    // once the source settles. False, so a caller can return it.
    bool deferSolve();

    // Whether a refused solve is waiting for one.
    bool solveDeferred() const;

    // Put the chip on the sync path the source carries, so that what the caller
    // reads next is the source rather than the last one's path. Measures
    // nothing the engine keeps, and runs once per mode change.
    void establishSyncType();
    void applySyncType(bool csync);

    // The hsync pulse, taken by the layer that measures and handed over. THE
    // ENGINE READS NOTHING BACK: every window it solves, now and on every
    // framing press until the next reading arrives, comes off this.
    void sourceMeasured(const HsyncPulse &reading);

    // Establish the scan mode and the vertical window a measurement is taken
    // through. Measures nothing itself, and installs no clock: the reset state
    // is already one that can be measured through, and every divider after it
    // is sized from a rate that was measured. Adc::BringUpDivider.
    //
    // Sync type, then scan mode, and both before the rate is measured. Each one
    // corrupts every measurement below it if left set for the previous source.
    // docs/video-source-acquisition.md
    void prepareToMeasure(uint16_t sourceLines);

    // Put the chip on the divider the source is to be left on, sized from the
    // line rate the caller has just measured. False where none can be chosen.
    //
    // Between the two halves of a measurement, because the duty is a ratio of
    // the pulse to the LINE and the line is the divider: a duty counted
    // through one and spent on a window sized in another's units is out by the
    // ratio between them. One install per mode change, where a reference clock
    // ahead of the measurement and the operating one after it cost two latches
    // and two settles.
    // A divider derived from a measurement that wanders may be suppressed by
    // the tolerance the two are compared on; one the output asked for may not.
    enum SamplingReason { SamplingFollowsMeasurement, SamplingFollowsOutput };
    bool installSampling(SamplingReason reason);

    // How far a re-derived divider may sit from the one in force before it is
    // written. This is QUANTISATION, not measurement: a field rate wobbling
    // either side of a divider step chose 2506 and 2508, which is 0.08%, and
    // every write re-latches the ADC PLL and restarts the settle.
    static const uint16_t DividerJitterPerMille = 2;

    // And the rate the divider in force was sized from, asked because the post
    // divider row and the VCO gain are a function of the divider TIMES the
    // rate -- so a divider that did not move can still want a different row.
    static const uint16_t InstalledRatePerMille = 50;

    // Solve every register from the measurement the caller has just taken.
    PollOutcome solveFromMeasurement();

    // Whether the last solve moved the timing the encoder is locked to. Cleared
    // by reading it, so one solve is answered once.
    //
    // The encoder samples the analog output and does not always notice the
    // timing under it moved: it carries on transmitting the mode it locked to
    // before, and the panel shows black with every scaler register correct.
    // Taking the output sync away is what makes it look again -- and it is the
    // only thing on this board that does, which is also why it is spent
    // nowhere else. ../../../../docs/investigations/encoder-stale-timing.md
    bool encoderTimingMoved();

    // Take the output sync away so the encoder re-acquires, and give it back.
    // The caller owns how long it stays away, because this class holds no clock.
    void holdOutputSync(bool away);

    // Hide the picture, or show it again. Blanked from the moment the source
    // stops being acquired until a solve has replaced the geometry.
    //
    // **IT MUST NOT REACH THE ENCODER.** The output sync pad is what the HDMI
    // encoder locks to, and taking it away costs a full sink re-acquisition --
    // seconds of dark panel, quantised, and none of it the engine's. The
    // display aperture blanks the picture where only the VDS can see it.
    // docs/investigations/the-transition-is-mostly-the-encoder.md
    void showOutput(bool show);

    // Whether a mode change is still working through: told the source moved and
    // not yet finished solving for it. What the sync output blanks against.
    bool changing() const;

    // Narrower than changing(): a deferred solve does not stop the caller looking
    // for a source event, but a mode change in flight does.
    bool changingMode() const;

    // Stop trusting the held sync type, so the next mode change measures it
    // again. For the one piece of evidence that says the path is wrong: a count
    // no source runs. The probe itself is not run here, because this is reached
    // from a pass that has just decided to re-measure everything anyway.
    void forgetSyncType();

    // Probe the sync type again and put the chip on the answer, for the
    // escalation a source that will not lock reaches. Returns whether the source
    // carries composite sync.
    //
    // IT WRITES THE PATH WHATEVER THE HELD VALUE SAYS, and nothing else
    // reconciles a register on the wrong path with a held value already right.
    // docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md
    bool reacquireSyncType();

    bool reset();

    // True where the press MOVED the capture window. Neither zoom stop shows in
    // VDS_?SCALE -- zoom-in lands either side of Scale::Min on the mode's own
    // rounding and zoom-out stops at the end of the line, far above it -- so
    // this is the only thing that can report a limit, and it reports the pan's
    // as well.
    bool pan(int16_t dxPixels, int16_t dyPixels);
    bool zoom(int16_t dhPixels, int16_t dvPixels);

    // A whole framing at once, for whoever restores one a user stored: the
    // proportions become the live framing and every register is re-solved from
    // them. The engine calculates -- a stored framing is never replayed as
    // registers. docs/framing-presets.md
    bool applyFraming(const PanAndZoom &framing);

    // Re-solve every register from the framing held and the reading last handed
    // over. MEASURES NOTHING: a caller wanting the source as it reads now
    // re-reads it and hands the reading in first.
    bool resolve();

    // Solve around this divider rather than the one SamplingClock recommends,
    // so two sampling densities can be compared as engine-solved states. 0
    // releases it. Held state, like every other input the engine calculates
    // from -- a divider written straight to the registers instead leaves the
    // capture window, both scales and the fetch describing the previous solve.
    // docs/investigations/a-hand-set-divider-cannot-be-judged-against-a-solved-window.md
    void holdDivider(uint16_t divider);
    uint16_t heldDivider() const;

    // Put the divider in force back on the chip and restart the PLL under it,
    // solving nothing. For a source that measures as absent while the sync
    // processor is simply not counting: it counts in ADC clocks, so an unlocked
    // ADC PLL is indistinguishable from no source, and every sync-processor
    // register is already correct.
    // ../../../docs/investigations/the-ladder-never-restarts-the-adc-pll.md
    void restartSamplingClock();

    // What the output is doing, as one question. Null only before anything has
    // been solved; ModeBypass -- isBypass() -- while video routes around the VDS.
    const OutputMode *outputMode() const;

    // Whether the line doubler is in the capture path. Decided here, because
    // what decides it is whether the doubled frame fits the raster -- and
    // written to three blocks, InputFormatter, VideoProcessor and Deinterlacer,
    // so no one of them can hold it. Everything counting IF units has to match:
    // the IF counts half-lines with the doubler in.
    bool lineDoubled() const;

private:

    // The raster is the held one, never a read-back.
    bool solveWindows();

    // A solve that keeps the framing when the SOURCE is the one it was tuned
    // against, and drops it when it is not: the proportions are taken against
    // the capturable region, which the output raster does not touch.
    // docs/framing-presets.md
    // Which source is in force, held so the timings can be generated from it
    // rather than from the reading. Runs BEFORE the raster is solved.
    // What the last measurement says the source is. One spelling, so the
    // framing table and the standards lookup cannot disagree about it.
    SourceKey arrivingKey() const;

    void adoptSourceKey();

    // Order: raster, clock, windows, rate steer LAST. Steering early corrects a
    // new clock against the old raster -- 31 Hz frame, black screen. A choice
    // that names no resolution writes nothing and is refused rather than
    // deferred.
    bool solveRaster();

    // Take the output raster off the chip, for the one case that does not solve
    // one: a preset table's bytes, left standing whenever solveRaster() defers.
    // Named, because a silent read-back is inheritance unaccounted for.
    void adoptRaster();

    // The largest divider whose captured line the output raster can show, or 0
    // where there is no raster to bound it with.
    // ../../../../docs/investigations/the-capture-may-not-outgrow-the-raster.md
    uint16_t dividerCeilingForOutput() const;

    // The divider this solve wants, held or recommended. Chooses; writes
    // nothing. The oversampling must have settled first: the sample clock is
    // the product of the divider and it.
    uint16_t chooseDivider() const;


    // Put the chip on a divider, in all three of the registers that carry it.
    // The divider goes first because Adc latches it,
    // and the latch loads KS, CKOS and ICP with it -- so anything setting those
    // must already have run. A measurement that solved nothing writes nothing.
    void applySampling(uint16_t divider);

    // **Before the divider is chosen, because it derives from this**: the
    // capture write limit doubles with the line doubler, so the two describe one
    // decision and the wrong order sizes the divider for the previous source.
    void solveLineDoubling(uint16_t lines);

    // Whether video routes around the VDS. The mode in force says it, so there
    // is nothing to hold separately.
    bool passedThrough() const;

    // EVERY PATH INTO PASS-THROUGH HAS TO REACH THIS. Only a completed solve
    // clears a mode change, and pass-through never solves -- so an armed one is
    // retried once sync stabilises and overwrites what the route switch chose.
    void configurePassThrough();

    // Bring the chip back up on the scaling path. Pass-through configured it
    // away from that setup and left the memory blocks, both FIFOs and the VDS in
    // reset, and nothing else on this path claims any of it back.
    void configureScalingPath();

    bool fail();

    // Name the step that refused. A boot that measures the source, writes the
    // divider and never writes the VDS is indistinguishable in a register dump
    // from one that never measured, and the three steps refuse for unrelated
    // reasons.
    bool refused(const char *step, const CaptureWindow &capture);

    // Output pixels -> input units. A press of nothing has to be skipped
    // outright: stepUnits() floors at one granule, so an axis the press did not
    // name would drift a unit per press.
    static int16_t unitsFor(int16_t pixels, const Scale &scale, const Axis &axis);

    // Where zoom-in stops on this axis: the capture below which the scale is
    // already at its floor, so a tighter crop shrinks the picture rather than
    // filling the screen with less of the source.
    uint16_t narrowestCaptureOn(const Axis &axis) const;

    // Where zoom-out stops on this axis: the capture above which the picture is
    // wider than the room and the part cannot minify, so every further unit is
    // a unit of picture with nowhere to go.
    // ../../../../docs/investigations/the-capture-may-not-outgrow-the-raster.md
    uint16_t widestCaptureOn(const Axis &axis) const;

    // Bring a framing back inside what the raster can show, before the capture
    // window is asked to realise it. On the solve path rather than in zoom()
    // alone, because an output mode change moves the raster under a framing
    // nobody pressed.
    void narrowToRaster(PanAndZoom &framing, const CaptureWindow &capture,
                        const Axis &axis) const;

    // Whether there is an output raster to solve a capture against. Bypass
    // leaves none, and there is nothing to solve there.
    bool rasterSolved() const;

    bool sizeCaptureWindow(CaptureWindow &capture);
    bool calculateInputFormatterRegisters(CaptureWindow &capture);
    VideoProcessorTimings calculateOutputRaster(const CaptureWindow &capture) const;

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    void write(const VideoProcessorTimings &solved,
               const CaptureWindow &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    DisplayClock &displayClock_;
    InputFormatter &inputFormatter_;
    PanAndZoom framing_;
    // The capturable region the last solve ran against, per axis: the
    // denominator a press converts its units into a proportion with.
    uint16_t usableHorizontal_, usableVertical_;
    uint16_t reachHorizontal_, reachVertical_;
    uint16_t firstHorizontal_, firstVertical_;
    uint16_t activeStartLine_;
    SourceTiming timing_;
    SourceMeasurement &sampling_;
    bool scanModeApplied_;
    bool lineDoubled_;
    bool syncTypeProbed_;
    // The path as it was last written, so a mode change that reuses the held
    // sync type pays neither the probe nor the settle behind it.
    bool syncTypeApplied_, syncTypeInForce_;
    bool (*syncProbe_)();
    SourceKey framedKey_;
    FramingTable &framings_;
    bool solvePending_;
    bool modePending_;
    uint8_t modeOversample_;
    uint16_t heldDivider_;
    bool fullFraming_;

    // The line rate the divider in force was sized from, so a pass asking for
    // the clock it already installed does not re-latch the PLL.
    uint32_t installedRateHz_;
    // The mode in force: the last one this was told to configure the chip for.
    // ModeBypass while video routes around the VDS, and 0 before anything has
    // been solved or where the choice names no resolution.
    const OutputMode *mode_;

    // The last pulse handed over. What every solve runs off, so a framing press
    // costs no read of the chip.
    HsyncPulse reading_;

    // The output raster in force, held rather than read back off VDS_?SYNC_RST.
    // Zero means there is none, which is what bypass looks like.
    uint16_t rasterLinePx_, rasterFrameLines_;
    Scale horizontalScale_, verticalScale_;

    // The display aperture the last solve chose, and whether a solve has chosen
    // one. Held rather than read back, so the blank is a state this applies
    // rather than a register value it saves.
    DisplayWindow display_;
    bool displaySolved_;
    bool showing_;

    // The sync pad as this has it. Held, so the pad is written only when it
    // moves: every write of it that changes nothing still costs nothing, but a
    // write that drops it costs the sink a re-acquisition.
    bool syncOut_, syncOutEver_;
    void driveSyncOut(bool on);

    // The timing the encoder is locked to, as the last solve left it: both
    // raster totals and the field rate they were solved for. The rate is
    // rounded because it is measured and dithers by tenths, and a tenth of a
    // hertz is not a timing the encoder can tell apart.
    uint16_t encoderLinePx_, encoderFrameLines_;
    uint16_t encoderFieldRateHz_;
    bool encoderKnown_, encoderMoved_;

    // The aperture as the showing_ state has it: what the last solve chose, or
    // an aperture that admits nothing.
    void writeDisplayAperture() const;

    // Where the front porch starts, from the raster this engine solved. The
    // registers carry no porch, so there is nothing to read back. 0 until a
    // raster is solved, which is what bypass and a custom preset both stay on.
    uint16_t activeStop_, activeLinesStop_;
    uint16_t activeStart_, activeLinesStart_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PATH_H_
