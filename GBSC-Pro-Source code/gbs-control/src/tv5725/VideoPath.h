#ifndef TV5725_VIDEO_PATH_H_
#define TV5725_VIDEO_PATH_H_

// Setting the chip up for a source: the orchestration of a video mode change,
// not the arithmetic alone. It is where the geometry meets the TV5725's
// registers and the only place they meet, and it is also what sequences the
// change itself -- what is re-solved, in what order, and what the video route
// becomes. docs/firmware-geometry-engine.md

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "../../gbs_types.h"
#include "BlankingTiming.h"
#include "DisplayClock.h"
#include "VideoSourceLine.h"
#include "CaptureWindow.h"
#include "OutputMode.h"
#include "OutputTiming.h"
#include "PanAndZoom.h"
#include "PictureOptions.h"
#include "Aspect.h"
#include "InputScale.h"
#include "ColourBalance.h"
#include "OutputWindow.h"
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

    **It is the orchestrator of a mode change, not a solver the sketch drives.**
    Reading it as the geometry alone is what leaves setup sequences in the
    sketch: deciding the order a change is applied in is this class's job, and a
    step that belongs to putting a source on screen belongs here whether or not
    it computes anything.

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

    // Phases of putting a source on screen. Each is a step of a mode change
    // rather than a solve, which is why they live here: deciding what a change
    // consists of, and in what order, is this class's.

    // Everything the PREVIOUS source left in a block that measures. Carried
    // over, a stale position or phase is read as this source's.
    void forgetPreviousSource();

    // The clock group's static half, after the ADC PLL has been restarted:
    // lock enable, both loop filters, which edge the input is clocked on, and
    // the decimator modes. The divider and the oversampling are not here --
    // those move with the source and are SourceMeasurement's.
    void applyClockGroup();

    // How the capture and playback stages ask for memory.
    void applyFrameBufferRequests();

    // Put everything already chosen in force: the video blocks and the ADC PLL
    // are restarted, the clock group and the memory requests written, the phase
    // adjusters re-taken, the group latched, and the clamp timed off the 27 MHz
    // reference at its default place.
    //
    // THE LATCH IS LAST, AND THE ORDER IS MEASURED. PLLAD_LAT loads MD, ND, KS,
    // CKOS and ICP together on a rising edge, so anything writing the group
    // after it leaves the part clocking at the old value with every register
    // reading back correct.
    void restartAndLatch();

    // Everything that has to be on the chip before the source is measured
    // through it: the colour path, the channel's output sync, the separator
    // level and both phase adjusters, and the previous source's positions
    // forgotten.
    //
    // THE SEPARATOR LEVEL IS NOT CHOSEN HERE. VideoSourceAcquisition searches
    // it against the source; whatever is held goes in force, because the
    // separator read and the first measurement are taken through it.
    void putSeparatorAndPhasesInForce();

    // The display scaler's picture filters, as the user chose them.
    //
    // The six-tap filter is forced ON, which overrides the preference the web
    // UI reports. ../../../../docs/known-issues.md
    void applyPictureFilters();

    // The two that follow the OUTPUT mode rather than the source, so an output
    // change re-applies these alone.
    void applyOutputPictureFilters();

    // The ADC's sense of what arrives on R, G and B, and what the chosen output
    // does to the chroma on the way out. Which space the source arrives in is
    // held by the class that selected the connector, so it is asked.
    void applyColourPath();

    // The user's picture filters, which a load must not put back to default --
    // so they live here beside the colour and the framing, and the preferences
    // pass adopts them rather than owning them.
    PictureOptions &pictureOptions();

    // The shape the picture is shown in. Defaulted from the raster the source
    // matched, replaced by whatever the user last chose for that source, and
    // stored beside the framing. An OUTPUT transform: setting it moves no part
    // of the framing, so a border flush to the screen stays flush to the shape.
    // docs/aspect-ratio.md
    Aspect aspect() const;

    // False where nothing moved, on the same terms as a framing press: the
    // shape asked for is the one already in force, or the solve refused.
    bool setAspect(Aspect shape);

    // Whether the last solve could show the shape it was given. False means an
    // axis filled instead, which no register distinguishes from having been
    // given no shape at all.
    bool shapeHonoured() const;

    // The user's picture colour, which a load must not put back to neutral --
    // so it lives beside the framing rather than being read off the chip.
    ColourBalance &colour();

    // Where the last solve put the picture, for a caller that wants to report
    // it rather than read a register back.
    const OutputWindow &image() const;

    // The source the framing held is against. Invalid until one has been
    // measured, so a caller storing a framing against it has to ask first.
    const SourceKey &framedKey() const;

    // The source a REPORT names. The framed key where a solve framed one, and
    // the arriving measurement where none did: pass-through spends the mode
    // change rather than solving, so a bypassed boot never adopts a key at all
    // and a screen reading framedKey() named 0 lines at 0 Hz.
    //
    // Held both ways, never measured here. The framed key is preferred where
    // there is one because it is quantised, so it does not move in its last
    // digit at a redraw cadence.
    SourceKey reportedKey() const;

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

    // One capture granule in OUTPUT PIXELS: the smallest move this axis has,
    // at the magnification the last solve landed on. A press is stated in
    // output pixels, so a caller wanting the finest step has to ask what that
    // costs -- it follows the output mode, the capture width and the divider.
    float granulePixels(const Axis &axis) const;

    // Hold the framing at the whole capturable region and ignore every press,
    // so a bench run can check one rule against any source and any output: at
    // 100% the capture takes the source's blanking on all four sides, and the
    // scaler is never asked to minify to show it. Releasing it gives the held
    // framing back.
    void forceFullFraming(bool on);
    bool fullFramingForced() const;

    // How far the input formatter's scaling-down block compresses the line,
    // which is the only minification the part has -- VDS_?SCALE divides 1024
    // and tops out at 1023, so the display scaler never produces a picture
    // narrower than its capture. HELD, so the next scan carries it and every
    // window placed in IF units follows. docs/scaling-down-path.md
    //
    // Nothing chooses it yet: the capture window, both scales and both output
    // windows have to come from ONE decision with it, and what the block does
    // to the count is measured rather than derived.
    // docs/investigations/the-input-formatter-can-scale-down.md
    bool setInputScale(InputScale wanted);
    InputScale inputScale() const;

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
    //
    // **THE ANSWER IS HELD AGAINST THE SELECTION IT WAS CHOSEN FOR**, so it
    // cannot outlive an input change: the two connectors carry different sync,
    // and a held answer reapplied to the other one leaves the sync processor
    // watching pins the selected source does not drive.
    void establishSyncType(uint8_t chosenFor);
    void applySyncType(bool csync);

    // The hsync pulse, taken by the layer that measures and handed over. THE
    // ENGINE READS NOTHING BACK: every window it solves, now and on every
    // framing press until the next reading arrives, comes off this.
    void sourceMeasured(const HsyncPulse &reading);

    // Establish the scan mode and the vertical window a measurement is taken
    // through, building the chip up first where the power path left it torn
    // down. Measures nothing itself; the only clock it installs is the
    // reference one that build-up needs, and every divider after it is sized
    // from a rate that was measured. Adc::BringUpDivider.
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

    // Solve every register from the measurement the caller has just taken.
    PollOutcome solveFromMeasurement();

    // Hide the picture, or show it again. Hidden behind the display aperture,
    // which the encoder never sees, from the moment the source stops being
    // acquired; shown by the acquisition layer once a transition has settled.
    //
    // A TRANSITION TAKES THE SYNC PAD AS WELL. The pad is what the HDMI encoder
    // locks to, and it places its window from what the line carries when the
    // pad returns -- so a mode change or an output change takes the pad away
    // before the first register the setup moves, and it comes back once, after
    // the last. docs/investigations/the-transition-is-mostly-the-encoder.md
    void showOutput(bool show);

    // Whether the output is on: the aperture open and the sync pad driven.
    bool outputShown() const;

    // Whether a transition has the sync pad away, which is what the acquisition
    // layer times its hold before presenting from.
    bool syncOutAway() const;

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

    // Put the path back on the chip for the sync type already in force, asking
    // no probe. False where no sync type has been applied yet.
    bool reapplySyncTypeInForce();

    // The line rate the measurement holds, for a caller deciding what an output
    // can carry. Held, not measured.
    uint32_t sourceLineRateHz() const;

    // Whether the source in force carries composite sync, as the engine decided
    // it. Held rather than read back: STATUS_SYNC_PROC_VSACT reports the sync
    // path already chosen, so it answers this question with its own input.
    // docs/sync-type-selection.md
    bool syncTypeIsCsync() const;

    bool reset();

    // True where the press MOVED the capture window. Neither zoom stop shows in
    // VDS_?SCALE -- zoom-in lands either side of the axis's floor on the mode's own
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

    // Whether the video goes through the scaler at all. Asked of the solved
    // output mode rather than of a bypass predicate: every path that changes
    // what the output is doing sets the mode, which is one owner, where
    // RgbhvOutput::isScaling() is a flag a boot leaves false on a unit that
    // goes on to scale.
    bool scalerCarriesVideo() const;

    // Whether the output in force is scaling RGBHV, which the sync processor's
    // preparation and its dynamic settings both ask. State rather than a chip
    // register: it lived in s1_2c, an address RD-5725-1.1 does not document.
    //
    // Not RgbhvOutput::isScaling(), which says what the SOURCE is entitled to
    // -- the bypass-refused path sets the two opposite.
    // ../../../../docs/investigations/the-rgbhv-question-is-two-questions.md
    bool scalingRgbhvInForce() const;
    void setScalingRgbhv(bool scaling);

    // Whether the line doubler is in the capture path. Decided here, because
    // what decides it is whether the doubled frame fits the raster -- and
    // written to three blocks, InputFormatter, VideoProcessor and Deinterlacer,
    // so no one of them can hold it. Everything counting IF units has to match:
    // the IF counts half-lines with the doubler in.
    bool lineDoubled() const;

    // A divider and an oversampling ratio chosen by hand rather than solved,
    // put in force the way a solve would put them -- the scan and the line
    // doubling that size the divider, the clamp that moves with it, and the
    // retime stop, all from the state this class already holds.
    //
    // The whole PLL group moves together or the ADC leaves lock, and the three
    // blocks that carry the scan have to agree with the counter, so there is
    // nothing here a caller can usefully do a subset of.
    void applyChosenSampling(uint16_t divider, uint8_t oversample);

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
    void announceSourceKey(const SourceKey &key, bool recalled);

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

    // The capture counter's origin, from the divider installed and the sync
    // width measured through it. Written at the install and again on every
    // solve, because the two arrive at different moments and either can move
    // without the other.
    void writeRetimeStop();

    // The divider this solve wants, held or recommended. Chooses; writes
    // nothing. The oversampling must have settled first: the sample clock is
    // the product of the divider and it.
    uint16_t chooseDivider() const;


    // Put the chip on a divider AND the scan sized for it, together. An IF unit
    // is two ADC samples on a doubled line and one on an undoubled one, so the
    // two are one setting: installed apart, the block counts a line that is not
    // arriving and the source's field rate -- timed off it -- comes back a
    // multiple of the truth. Nothing is written at all where the line does not
    // fit the counter.
    // ../../../../docs/investigations/the-field-rate-reads-exactly-double-after-a-sync-reset.md
    //
    // The oversampling goes in beside them because it is the third part of one
    // setting, and it is HELD: a caller that asks for a ratio by hand is giving
    // a command, and the next solve has to size its divider against the same
    // one or it undoes the request on the next measurement.
    void applySampling(uint16_t divider, bool doubled, uint8_t oversample);

    // The scan alone, sized for a divider that is going in beside it or is
    // already in force. False where the line does not fit the counter, having
    // written nothing.
    bool applyScan(uint16_t divider, bool doubled);

    // The clock alone, for a caller re-asserting the divider ALREADY IN FORCE:
    // the scan beside it already describes that line, and re-asserting one from
    // a stale decision is what left the block half doubled.
    void applySamplingClock(uint16_t divider);

    // **Before the divider is chosen, because it derives from this**: the
    // capture write limit doubles with the line doubler, so the two describe one
    // decision and the wrong order sizes the divider for the previous source.
    //
    // Applied against the clock in force, so the block is corrected while the
    // source is still unmeasurable: its own measurements only mean something
    // once its scan matches the source, and a scan derived after that gate is
    // never reached.
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
    void buildUpIfTornDown();
    void configureScalingPath();

    bool fail();

    // Name the step that refused. A boot that measures the source, writes the
    // divider and never writes the VDS is indistinguishable in a register dump
    // from one that never measured, and the three steps refuse for unrelated
    // reasons.
    bool refused(const char *step, const CaptureWindow &capture);

    // Output pixels -> input units, through the magnification this solve
    // landed on, so a press means the same on screen whatever the source
    // measures and whatever raster it is shown in.
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
    static uint16_t marginTaken(const CaptureWindow &capture, const Axis &axis);
    OutputWindow imageFor(const CaptureWindow &capture) const;

    // Ordered so the headroom never dips: the solver always takes the whole
    // memory window, so the only edge that can narrow it is VDS_?B_SP moving up.
    // docs/firmware-geometry-engine.md "Write ordering".
    void write(const OutputWindow &solved, const CaptureWindow &capture);

    // A press that cannot move the window must not move the state either, or the
    // control goes dead for as many presses as it was pushed past its limit.
    bool step(const PanAndZoom &wanted);

    DisplayClock &displayClock_;
    InputFormatter &inputFormatter_;
    PanAndZoom framing_;
    Aspect aspect_;
    InputScale inputScale_;
    ColourBalance colour_;
    PictureOptions picture_;
    // The capturable region the last solve ran against, per axis: the
    // denominator a press converts its units into a proportion with.
    uint16_t usableHorizontal_, usableVertical_;
    uint16_t reachHorizontal_, reachVertical_;
    uint16_t firstHorizontal_, firstVertical_;
    uint16_t activeStartLine_;

    // The divider this class installed, held rather than read back: the retime
    // stop is a function of it and of the source's sync width, and the sync
    // width arrives AFTER the install invalidates it.
    uint16_t installedDivider_;
    SourceTiming timing_;
    SourceMeasurement &sampling_;
    bool scanSolved_;
    bool lineDoubled_;
    // The path as it was last written, so a mode change that reuses the held
    // sync type pays neither the probe nor the settle behind it.
    bool syncTypeApplied_, syncTypeInForce_;
    // No selection carries this value, so the first pass always establishes.
    static const uint8_t NoSelectionSeen = 0xFF;
    uint8_t syncTypeChosenFor_;
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

    // The output raster in force, as OutputMode solved it: held WHOLE rather
    // than unpacked into the fields each caller wants, because the porch is not
    // a register and the next solve cannot read any of it back. Zero totals
    // mean there is no raster, which is what bypass looks like.
    OutputTiming raster_;

    // Where the last solve put the picture: both scales and both pairs of
    // blanking windows. Held rather than read back, so the blank is a state
    // this applies rather than a register value it saves, and usable() is what
    // says a solve has chosen one at all.
    OutputWindow output_;
    bool showing_;
    bool scalingRgbhv_;

    // The sync pad as this has it. Held, so the pad is written only when it
    // moves: every write of it that changes nothing still costs nothing, but a
    // write that drops it costs the sink a re-acquisition.
    bool syncOut_, syncOutEver_;
    void driveSyncOut(bool on);

    // The aperture closed and the sync pad away, ahead of a setup's first write.
    void takeOutputAway();

    // The timing the encoder is locked to, as the last solve left it: both
    // raster totals and the field rate they were solved for. The rate is
    // rounded because it is measured and dithers by tenths, and a tenth of a
    // hertz is not a timing the encoder can tell apart. A solve that moves it
    // takes the output away before it writes the raster.
    uint16_t encoderLinePx_, encoderFrameLines_;
    uint16_t encoderFieldRateHz_;
    bool encoderKnown_;

    // The aperture as the showing_ state has it: what the last solve chose, or
    // an aperture that admits nothing.
    void writeDisplayAperture() const;

};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PATH_H_
