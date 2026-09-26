#include "VideoPath.h"

#include "Tv5725Log.h"

#include <Arduino.h>

#include "Chip.h"

#include <math.h>
#include <stdio.h>

#include "Adc.h"
#include "BringUp.h"
#include "ColourSpace.h"
#include "CaptureWindow.h"
#include "Deinterlacer.h"
#include "HdBypass.h"
#include "InputFormatter.h"
#include "MemoryWindow.h"
#include "SamplingClock.h"
#include "OutputMode.h"
#include "ModeDetect.h"
#include "SyncProcessor.h"
#include "SyncMeasurement.h"
#include "VideoProcessor.h"

namespace Tv5725 {

// --- VideoPath ----------------------------------------------------------

VideoPath::VideoPath(DisplayClock &displayClock, SourceMeasurement &sampling,
                     FramingTable &framings, InputFormatter &inputFormatter)
    : displayClock_(displayClock), inputFormatter_(inputFormatter),
      usableHorizontal_(0), usableVertical_(0),
      reachHorizontal_(0), reachVertical_(0),
      firstHorizontal_(0), firstVertical_(0), activeStartLine_(0),
      timing_(0.0f),
      sampling_(sampling),
      scanSolved_(false), lineDoubled_(true),
      syncTypeApplied_(false),
      syncTypeInForce_(false), syncTypeChosenFor_(NoSelectionSeen), syncProbe_(0),
      framings_(framings),
      solvePending_(false), modePending_(false), modeOversample_(4),
      heldDivider_(0), fullFraming_(false), installedRateHz_(0),
      mode_(0),
      showing_(false),
      syncOut_(false), syncOutEver_(false),
      encoderLinePx_(0), encoderFrameLines_(0), encoderFieldRateHz_(0),
      encoderKnown_(false), encoderMoved_(false) {}

const PanAndZoom &VideoPath::framing() const { return framing_; }

const SourceKey &VideoPath::framedKey() const { return framedKey_; }

// THE BLANK FOLLOWS WHETHER THERE IS A PICTURE TO SHOW, and it lands on the
// DETECTION rather than on the arm: a mode change is seen about half a second
// before anything arms one, and until the blank lands the panel is showing the
// previous mode's geometry applied to a source that has left.
//
// Both directions are written, because the blank only ever lifted at a solve and
// a source that drops and returns unchanged arms nothing -- which leaves the
// panel dark for good.
//
// The sync pad is the fallback and not the mechanism. Where there is no solved
// aperture to close, or where video routes around the VDS entirely, closing one
// blanks nothing.
// ../../../../docs/investigations/the-transition-is-mostly-the-encoder.md
void VideoPath::showOutput(bool show)
{
    showing_ = show;

    // The power path takes the whole output down and only a preset load ever
    // wrote these two back, so a source that returns without one leaves a dark
    // panel with every geometry register correct. Written unconditionally,
    // because neither is what the encoder locks to -- the pad is, and it keeps
    // its own owner below for that reason.
    if (show) {
        Chip::OUT_SYNC_CNTRL::write(1);
        Chip::DAC_RGBS_PWDNZ::write(1);
    }

    if (!output_.usable() || mode_ == 0 || mode_->isBypass()) {
        driveSyncOut(show);
        return;
    }

    if (show)
        driveSyncOut(true);
    writeDisplayAperture();
}

bool VideoPath::encoderTimingMoved()
{
    const bool moved = encoderMoved_;
    encoderMoved_ = false;
    return moved;
}

void VideoPath::holdOutputSync(bool away) { driveSyncOut(!away); }

// Only the DROP is suppressed. It costs the sink a full re-acquisition, while
// driving a pad already driven costs one write -- and s0_49 has a second writer
// in the power path, so a cached drive cannot be trusted to still be in force.
void VideoPath::driveSyncOut(bool on)
{
    if (!on && syncOutEver_ && !syncOut_)
        return;

    const bool changed = !syncOutEver_ || syncOut_ != on;
    syncOut_ = on;
    syncOutEver_ = true;

    if (changed) {
        char line[48];
        snprintf(line, sizeof(line), "sync pad: %s", on ? "driven" : "away");
        tv5725Log(line);
    }

    if (on)
        SyncProcessor::enableOutput();
    else
        SyncProcessor::disableOutput();
}

// One line of active video where the aperture is closed, which is what the
// bench measured black. RD-5725-1.1 states no behaviour for a start at the
// stop, so the aperture is collapsed to the narrowest measured rather than to
// the narrowest expressible.
void VideoPath::writeDisplayAperture() const
{
    if (showing_) {
        GBS::VDS_DIS_HB_SP::write(output_.horizontal().display().stop());
        GBS::VDS_DIS_HB_ST::write(output_.horizontal().display().start());
        GBS::VDS_DIS_VB_SP::write(output_.vertical().display().stop());
        GBS::VDS_DIS_VB_ST::write(output_.vertical().display().start());
        return;
    }

    GBS::VDS_DIS_VB_SP::write(output_.vertical().display().stop());
    GBS::VDS_DIS_VB_ST::write((uint16_t)(output_.vertical().display().stop() + 1));
}

bool VideoPath::changing() const { return modePending_ || solvePending_; }

bool VideoPath::changingMode() const { return modePending_; }

uint16_t VideoPath::lineUnitsOn(const Axis &axis) const
{
    return axis.vertical() ? usableVertical_ : usableHorizontal_;
}

uint16_t VideoPath::firstUnitOn(const Axis &axis) const
{
    return axis.vertical() ? firstVertical_ : firstHorizontal_;
}

uint16_t VideoPath::reachOn(const Axis &axis) const
{
    return axis.vertical() ? reachVertical_ : reachHorizontal_;
}

uint16_t VideoPath::sourceActiveStartLine() const { return activeStartLine_; }

const SourceTiming &VideoPath::sourceTiming() const { return timing_; }

uint16_t VideoPath::originUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.originOn(axis) * (float)lineUnitsOn(axis));
}

uint16_t VideoPath::extentUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.extentOn(axis) * (float)lineUnitsOn(axis));
}

// Solve every register from what is held. A caller that has only moved the
// framing wants solveWindows(); this is for one that has just been handed a
// fresh reading, or whose previous solve was refused against the one it had.
bool VideoPath::resolve()
{
    if (!installSampling(SamplingFollowsMeasurement))
        return fail();
    return solveWindows();
}

bool VideoPath::deferSolve() { return fail(); }

bool VideoPath::solveDeferred() const { return solvePending_; }

bool VideoPath::solveWindows()
{
    CaptureWindow capture;
    if (!sizeCaptureWindow(capture))
        return refused("capture window", capture);
    if (!calculateInputFormatterRegisters(capture))
        return refused("input formatter", capture);

    OutputWindow solved = imageFor(capture);
    if (!solved.usable()) {
        fail();
        return refused("output raster", capture);
    }

    write(solved, capture);
    solvePending_ = false;

    // The geometry is ready, so the blank has nothing left to hide. Every solve
    // writes its aperture through the blank state, which means one that
    // completes while the picture is hidden writes an aperture admitting
    // nothing -- and a mode change is the only change with a completion step of
    // its own to lift it. Without this a deferred solve leaves the panel black
    // with the source acquired and every other register correct.
    if (!modePending_)
        showOutput(true);
    return true;
}

const OutputMode *VideoPath::outputMode() const { return mode_; }

bool VideoPath::solveRaster()
{
    // The choice is an input, not a read-back. Deriving the mode from
    // VDS_VSYNC_RST would leave the preset table -- the thing this replaces --
    // its only writer. docs/chip-initialisation.md.
    const OutputMode *mode = mode_;
    if (mode == 0) {
        // Not a failure, and NOT a fall back to 1080p: the choice names no
        // resolution, and a raster nobody has swept keeps what it had.
        return false;
    }
    if (mode->isBypass()) {
        // The output is the source's own timing, so there is no raster to
        // compute and solving one would write zeros that every other register
        // agrees with.
        return false;
    }

    // The KEY's rate, which is a whole hertz and only moves when the source's
    // identity does. The reading behind it wanders -- one unchanged 800x600
    // source settles at 60.38 Hz after one mode change and 60.72 after the
    // next -- and a raster generated from that moves with it, for a source that
    // never moved. The frame time lock closes on frame time continuously, so
    // the raster only has to be in the right ballpark.
    // docs/firmware-geometry-engine.md
    // EngineCeilingHz, not the higher WorkingCeilingHz the part is measured to
    // run at: a wider raster costs zoom travel. See the constant.
    OutputTiming raster = mode->solve(framedKey_.rateHz(),
                                        OutputMode::EngineCeilingHz);
    if (!raster.usable()) {
        // Refused, not deferred: the frame height and the rate are settled, so
        // waiting produces the same answer at the cost of a measurement a pass.
        return false;
    }

    // Totals before sync positions, per
    // docs/investigations/preset-abandonment-audit.md. Both hold total-1.
    GBS::VDS_HSYNC_RST::write(raster.horizontalTotal - 1);
    GBS::VDS_VSYNC_RST::write(raster.verticalTotal - 1);
    raster_ = raster;

    // One quantity in three registers, so all three are written here.
    // VDS_VSYN_SIZE1 and _2 are the vertical totals the frame-rate selector
    // picks between, and VDS_FR_SELECT never alternates, so both are the frame.
    // doPostPresetLoadSteps() wrote them once, in a function the deferred solve
    // never re-enters, leaving them sized for the total the preset loaded with.
    GBS::VDS_VSYN_SIZE1::write(raster.verticalTotal + 1);
    GBS::VDS_VSYN_SIZE2::write(raster.verticalTotal + 1);

    GBS::VDS_HS_ST::write(raster.hsyncStart);
    GBS::VDS_HS_SP::write(raster.hsyncStop);
    GBS::VDS_VS_ST::write(raster.vsyncStart);
    GBS::VDS_VS_SP::write(raster.vsyncStop);

    // The seed, last of the raster group and first of the clock's. It names the
    // frequency to run at, not the byte to write: select() puts the part on
    // PCLKIN when a generator can serve that frequency, and on the seed's own
    // internal divider when none can.
    displayClock_.hold(raster.divider);
    displayClock_.select();

    // Solving a raster means the scaler is what the output carries, so the
    // routing bypass moved is claimed back here rather than by a whole-chip
    // bring-up running on the way past.
    Chip::routeToScaler();

    // What the encoder now has to lock to. Both totals and the rate, because the
    // output frame time follows the source: one raster at two field rates is two
    // pixel clocks and two HDMI modes.
    const uint16_t fieldRateHz = (uint16_t)(sampling_.fieldRateHz() + 0.5f);
    // The FIRST solve is not a move: the bring-up has the sync pad away already,
    // so the encoder has no timing to be stale on and a re-look buys nothing.
    if (encoderKnown_
        && (raster.horizontalTotal != encoderLinePx_
            || raster.verticalTotal != encoderFrameLines_
            || fieldRateHz != encoderFieldRateHz_))
        encoderMoved_ = true;
    encoderLinePx_ = raster.horizontalTotal;
    encoderFrameLines_ = raster.verticalTotal;
    encoderFieldRateHz_ = fieldRateHz;
    encoderKnown_ = true;

    return true;
}

bool VideoPath::scalerCarriesVideo() const
{
    const OutputMode *mode = outputMode();
    return mode != NULL && !mode->isBypass();
}

void VideoPath::adoptRaster()
{
    // Read back, which is what adopting means: bypass and a custom preset
    // leave a raster on the chip that this engine did not solve, so the totals
    // are all of it there is. The porch is not a register and stays zero.
    raster_ = OutputTiming();
    raster_.horizontalTotal = GBS::VDS_HSYNC_RST::read() + 1;
    raster_.verticalTotal = GBS::VDS_VSYNC_RST::read() + 1;
    displayClock_.adopt();
}

void VideoPath::inputTimingsChanged()
{
    inputTimingsChanged(modeOversample_);
}

void VideoPath::inputTimingsChanged(uint8_t oversample)
{
    showOutput(false);

    modePending_ = true;
    modeOversample_ = oversample;
    scanSolved_ = false;

    // The line count is about to move, so the steadiness run so far means
    // nothing.
    sampling_.modeChanged();

    // The sampling clock, BEFORE anything tries to measure. A load leaves the
    // ADC PLL on the bring-up's crossover row, and the sync processor counts in
    // ADC clocks -- so every measurement is garbage until this runs, the
    // steadiness gate never passes, and the pass that would have fixed the
    // clock never arrives.
    //
    // The clock alone: the divider is the one already in force, so the scan
    // beside it already describes that line, and re-asserting it from the
    // OUTGOING source's decision is what left the block half doubled.
    applySamplingClock(Adc::dividerInForce());
}

bool VideoPath::passedThrough() const
{
    return mode_ != 0 && mode_->isBypass();
}

bool VideoPath::setOutputMode(const OutputMode *mode)
{
    if (mode != 0 && mode->isBypass()) {
        configurePassThrough();
        return true;
    }

    const OutputMode *const previous = mode_;
    const bool leaving = passedThrough();

    // Held before solveLineDoubling(), which judges the doubler against it.
    // **THE LINE DOUBLER IS A PROPERTY OF THE OUTPUT AS MUCH AS OF THE SOURCE**:
    // what decides it is whether the doubled frame fits the raster, so a shorter
    // raster strands a doubling that fitted the taller one.
    mode_ = mode;

    if (leaving) {
        // Configured here, not solved. The rate held is the one pass-through was
        // entered on, so what solves this output is the measurement that
        // follows -- the caller's next one, or the preset load the false sends
        // it to.
        configureScalingPath();
        return false;
    }

    if (modePending_)
        return false;

    const bool wasDoubled = lineDoubled_;
    solveLineDoubling(sampling_.sourceLines());

    // The divider is bounded by the capture the RASTER can show, so the output
    // moves it as much as the doubling does -- 480p affords 1876 ADC samples of
    // this source's line and 576p 1952, both undoubled. Only where one of the
    // two moved: the divider is re-DERIVED from the rate already held, never
    // re-measured, and writing it re-latches the ADC PLL.
    if ((mode != previous || lineDoubled_ != wasDoubled)
        && !installSampling(SamplingFollowsOutput))
        return false;

    if (!solveRaster())
        return false;

    // raster -> clock -> windows, the same order poll() runs and for the same
    // reason: the clock reads the seed the raster just chose.
    displayClock_.reset();
    return solveWindows();
}

void VideoPath::sourceMeasured(const HsyncPulse &reading)
{
    reading_ = reading;
    timing_ = SourceTiming::matching(arrivingKey());
    activeStartLine_ = timing_.activeStartLine(sampling_.sourceLines() + 1);
}

void VideoPath::prepareToMeasure(uint16_t sourceLines)
{
    solveLineDoubling(sourceLines);

    // Pass-through solves nothing, so there is no window here to strand and
    // nothing that would rewrite one taken off it.
    // ../../../../docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    if (passedThrough())
        return;

    // Unconditional, because a window is not only stranded by a mode change.
    // Nothing else writes these two until a solve succeeds, which is the thing
    // they are stopping.
    inputFormatter_.writeReferenceVerticalBlank();
}

bool VideoPath::installSampling(SamplingReason reason)
{
    // Pass-through's divider is HdBypass's, sized from the rate held when the
    // channel was entered, and HD_HSYNC_RST is sized for the same number -- so
    // the duty is already counted against a known line and installing anything
    // here would move the sampling out from under a raster no solve will redo.
    if (passedThrough())
        return true;

    const uint16_t divider = chooseDivider();
    if (divider == 0)
        return false;

    // ASKED OF THE RATE, NOT OF THE DIVIDER. The divider is an actuator inside
    // the loop that measures its own input, so re-deriving it per measurement
    // limit-cycles either side of the value it wants and no tolerance on the
    // divider converges. Holding it against the rate it was sized from costs
    // nothing, because a settled source carries no scatter to forgive.
    //
    // A divider that was CHOSEN rather than measured compares exactly: the
    // output moves the bound the divider is sized against, and a held one is a
    // command that a rate which did not move must not swallow.
    // ../../../docs/investigations/the-divider-is-an-actuator-in-its-own-sensor.md
    const uint32_t rate = sampling_.lineRateHz();
    const uint16_t inForce = Adc::dividerInForce();
    const bool derivedFromMeasurement =
        reason == SamplingFollowsMeasurement && heldDivider_ == 0;

    // AND OF THE SCAN, because the divider is sized against it: the scan on the
    // chip is the one the last install put there, and the selection path leaves
    // the reference clock's rather than this source's -- so a rate that agrees
    // must not suppress the install that carries the scan in.
    const bool alreadyInForce =
        inputFormatter_.scanIsDoubled() == lineDoubled_
        && (derivedFromMeasurement
                ? VideoSignal::ratesAgree(rate, installedRateHz_,
                                          SourceIdentityPerMille)
                : divider == inForce);
    if (inForce != 0 && alreadyInForce)
        return true;

    installedRateHz_ = rate;

    char line[80];
    snprintf(line, sizeof(line), "sampling: rate %lu doubled %u -> divider %u",
             (unsigned long)rate, (unsigned)lineDoubled_, (unsigned)divider);
    tv5725Log(line);

    applySampling(divider, lineDoubled_);
    return true;
}

VideoPath::PollOutcome VideoPath::solveFromMeasurement()
{
    if (!modePending_)
        return PollIdle;

    if (!installSampling(SamplingFollowsMeasurement))
        return PollIdle;

    // Before the raster, which is generated from the key rather than from the
    // reading behind it. docs/firmware-geometry-engine.md
    adoptSourceKey();

    // Whatever raster is on the chip, taken before the solve that replaces it,
    // so a solve that still refuses leaves the windows sized for something.
    adoptRaster();
    if (!solveRaster()) {
        // solveRaster() never defers: both its refusals are final, so a retry
        // would pay for a field rate measurement to reach the same answer.
        modePending_ = false;
        showOutput(true);
        return PollIdle;
    }

    // raster -> clock -> windows. The clock reads the seed the raster just
    // chose, and every window is sized against the raster it lands on.
    displayClock_.reset();
    solveWindows();

    modePending_ = false;
    showOutput(true);
    return PollSolved;
}

bool VideoPath::reset()
{
    // The entry goes with the framing. "Back to default" has to mean the table
    // stops answering for this source, or the solve that follows restores
    // exactly what was just discarded and the control does nothing.
    framings_.forget(framedKey_);

    // A framing change like any other. The source has not moved and no load has
    // disturbed the ADC, so the divider, the raster and the clock all re-derive
    // to what they already hold -- and re-arming would freeze capture for
    // seconds to reach them.
    framing_.reset();
    return solveWindows();
}

void VideoPath::configurePassThrough()
{
    // The measurement is NOT discarded. Bypass does not measure, so what is
    // held is the rate from the mode that preceded it -- which is the fact a
    // caller asking whether the display can show this source wants, and the
    // only place it exists once the standard byte is gone.

    // No raster is solved here, so the register is the only source of the seed
    // the encoder is already running on.
    displayClock_.adopt();

    modePending_ = false;

    // The resolution the user asked for is NOT touched here, because it is not
    // held here: pass-through is a different fact about the same output, and
    // stored in one field the second destroyed the first and left the way back
    // with nothing to return to.
    mode_ = &ModeBypass;

    // AFTER the mode, which is what says there is no display aperture in the
    // path: video routes around the VDS here, so the blank can only be the sync
    // pad and closing an aperture would blank nothing.
    showOutput(true);

    raster_ = OutputTiming();

    // Bypass has no solved raster, so it has no porch either -- and a porch left
    // from the last scaled mode would size the next one's picture.
}

void VideoPath::configureScalingPath()
{
    // Route first, so the bring-up sees the path it is configuring.
    Chip::routeToScaler();
    if (BringUp::armed())
        BringUp::init(inputFormatter_);

    // Configured, then restarted. Chip::init() leaves the VDS and the input
    // formatter held -- only this releases them, and only on the scaling
    // branch, which the route above is what selects.
    Chip::resetVideoBlocks();

    // The decimator's matrix, which pass-through takes out because the HD bypass
    // channel converts for itself. Which one the source wants is held by the
    // class that selected the connector, so it is asked rather than handed in.
    if (Adc::inputIsComponent())
        ColourSpace::applyYuv();
    else
        ColourSpace::applyRgb();
}

SourceKey VideoPath::arrivingKey() const
{
    return SourceKey(sampling_.sourceLines(), sampling_.fieldRateHz(),
                     reading_.syncDuty(), sampling_.hsyncPolarity(),
                     sampling_.vsyncPolarity());
}

void VideoPath::adoptSourceKey()
{
    const SourceKey arriving = arrivingKey();
    if (arriving == framedKey_)
        return;

    // Leaving one source for another. Nothing is stored here: the table has
    // followed every press already, so what this source was tuned to is in it.
    if (!framings_.find(arriving, &framing_))
        framing_.reset();
    framedKey_ = arriving;
}



void VideoPath::forceFullFraming(bool on) { fullFraming_ = on; }

bool VideoPath::fullFramingForced() const { return fullFraming_; }

void VideoPath::useSyncTypeProbe(bool (*hasOwnVsync)()) { syncProbe_ = hasOwnVsync; }

void VideoPath::forgetSyncType()
{
    SyncMeasurement::forget();
    syncTypeApplied_ = false;
}

bool VideoPath::reacquireSyncType()
{
    SyncMeasurement::forget();
    syncTypeApplied_ = false;
    establishSyncType(syncTypeChosenFor_);
    return SyncMeasurement::isCsync();
}

bool VideoPath::reapplySyncTypeInForce()
{
    if (!syncTypeApplied_)
        return false;

    const bool csync = syncTypeInForce_;
    syncTypeApplied_ = false;
    applySyncType(csync);
    return true;
}

// OPTIMISTIC, AND THE LADDER PAYS FOR BEING WRONG. A source changes its sync
// type far more rarely than it changes mode, and measuring costs the probe's
// settle and window -- a composite source has no V to arrive, so it spends the
// whole window every time, measured at 1.00 s of a 2.0 s transition. So a mode
// change reuses what is held, and a held value that turns out wrong reaches the
// escalation ladder: the wrong path counts 97..137 on a 311-line source, which
// arms a re-probe.
// ../../../../docs/investigations/own-vsync-probe-window.md
void VideoPath::establishSyncType(uint8_t chosenFor)
{
    if (chosenFor != syncTypeChosenFor_) {
        syncTypeChosenFor_ = chosenFor;
        SyncMeasurement::forget();
        syncTypeApplied_ = false;
    }

    if (syncProbe_ == 0)
        return;

    // ONE RECORD OF WHETHER THE SYNC TYPE IS KNOWN, and it is
    // SyncMeasurement's. A second flag here cannot see the sketch's own
    // forget(), so a reset that says the answer is unknown left this reading a
    // held value the reset had already replaced with a guess -- measured on the
    // bench as the right arrangement applied on an input change and undone five
    // seconds later when detection dropped to low power.
    applySyncType(SyncMeasurement::syncType(syncProbe_));
}

// The settle is what makes this worth skipping: applying the path the chip is
// already on costs half a second and changes nothing.
void VideoPath::applySyncType(bool csync)
{
    if (syncTypeApplied_ && csync == syncTypeInForce_)
        return;
    syncTypeApplied_ = true;
    syncTypeInForce_ = csync;

    // THE ARRANGEMENT IS INVISIBLE OTHERWISE. Every register it writes is one
    // several other paths also write, so which owner last had it cannot be read
    // off a dump -- and a source measured through the wrong one reports a clean
    // count for the source on the other connector.
    char line[56];
    snprintf(line, sizeof(line), "sync arrangement: %s for input %u",
             csync ? "composite or SOG" : "separate", (unsigned)syncTypeChosenFor_);
    tv5725Log(line);

    SyncProcessor::applyForSyncType(csync);
    ModeDetect::applySyncType(csync ? ModeDetect::Csync : ModeDetect::SeparateSync);
    delay(SyncProcessor::PathSettleMs);
}

void VideoPath::solveLineDoubling(uint16_t lines)
{
    if (!VideoSignal::countIsSource(lines))
        return;

    // Against the mode ASKED FOR rather than the raster last solved: this runs
    // before solveRaster(), so the held raster is the previous output's, and a
    // mode change would decide the scan mode from the resolution it is leaving.
    // The porch is not known this early either, so the bound is the raster's own
    // edge and a doubling that only just fits is caught by the capture clamp.
    const uint16_t showable =
        mode_ && !mode_->isBypass()
            ? OutputWindow::maximumCapture(AxisVertical, mode_->frameLines(), 0, 0)
            : 0;
    const bool doubled = InputFormatter::shouldDoubleLine(lines, showable);
    if (scanSolved_ && doubled == lineDoubled_)
        return;

    // An IF unit is two ADC samples on a doubled line and one on an undoubled
    // one, so the doubling sizes the divider as much as the rate does, and the
    // clock in force is no longer the one this rate installed.
    installedRateHz_ = 0;
    lineDoubled_ = doubled;
    scanSolved_ = true;

    // SIZED FOR THE CLOCK ALREADY IN FORCE, because that is the line the block
    // is counting. A scan the clock in force cannot be represented in is
    // refused whole and carried by the install that moves the divider -- which
    // is the reference clock's case, 2506 samples being a line no progressive
    // counter can hold.
    applyScan(Adc::dividerInForce(), doubled);
}

bool VideoPath::lineDoubled() const { return lineDoubled_; }

bool VideoPath::applyScan(uint16_t divider, bool doubled)
{
    if (divider == 0)
        return false;

    // The line counter is a count of IF units and the decimation is what an IF
    // unit IS, so the scan and the line it is sized for are one setting: written
    // apart, the block counts several lines per line and the source's field
    // rate -- timed off that block's vertical -- comes back a multiple of the
    // truth.
    // ../../../../docs/investigations/the-field-rate-reads-exactly-double-after-a-sync-reset.md
    const bool component = Adc::inputIsComponent();
    if (!inputFormatter_.applyScan(divider, doubled, component))
        return false;

    VideoProcessor::applyLineDoubling(doubled, component);
    Deinterlacer::applyLineDoubling(doubled);
    return true;
}

void VideoPath::applySampling(uint16_t divider, bool doubled)
{
    if (!applyScan(divider, doubled))
        return;

    applySamplingClock(divider);
}

void VideoPath::applySamplingClock(uint16_t divider)
{
    if (divider == 0)
        return;

    Adc::applySampleRate(divider, sampling_.lineRateHz(), modeOversample_);
    SyncProcessor::writeRetimeStop(SyncProcessor::retimeStopFor(divider));

    // The clamp is a fraction of the LINE, so it moves with the divider. Left
    // where a previous one put it, the stop reaches past the back porch and the
    // black level is taken off picture -- measured, a stop 18 samples late
    // costs 20 grey levels. Placed from the divider just applied rather than
    // measured: STATUS_SYNC_PROC_HTOTAL only echoes it back.
    SyncProcessor::placeClampFor(divider, syncTypeInForce_, Adc::inputIsComponent());

    // Every install re-latches the PLL, so nothing counted in ADC samples is
    // the source's for the next few passes -- whichever route wrote it.
    sampling_.samplingClockLatched();
}

// Taking a hold and releasing one are both commands, so neither may be skipped
// by installSampling()'s rate test.
void VideoPath::holdDivider(uint16_t divider)
{
    heldDivider_ = divider;
    installedRateHz_ = 0;
}

uint16_t VideoPath::heldDivider() const { return heldDivider_; }

void VideoPath::restartSamplingClock()
{
    applySamplingClock(Adc::dividerInForce());
    Adc::restartPll();
    SyncProcessor::forgetPositions();
}

uint16_t VideoPath::chooseDivider() const
{
    return heldDivider_ != 0
               ? heldDivider_
               : SamplingClock::recommendedDivider(sampling_.lineRateHz(),
                                                   modeOversample_, lineDoubled_,
                                                   dividerCeilingForOutput());
}

uint16_t VideoPath::dividerCeilingForOutput() const
{
    if (mode_ == 0 || mode_->isBypass())
        return 0;

    // The raster this solve is about to write, computed rather than read: it
    // depends on the output choice and the key's rate, and on nothing the
    // sampling clock decides. Running it here costs one solve and keeps the
    // write order raster -> clock -> windows intact.
    const SourceKey arriving = arrivingKey();
    OutputTiming raster = mode_->solve(arriving.rateHz(), OutputMode::EngineCeilingHz);
    if (!raster.usable())
        return 0;

    const uint16_t showable =
        OutputWindow::maximumCapture(AxisHorizontal, raster.horizontalTotal, 0,
                                     raster.activeStop);
    if (showable == 0)
        return 0;

    // Back from IF units to ADC samples: the line the divider may ask for is
    // the capture the raster can show. The head the capture never reaches is
    // the hsync pulse, which is a fraction of the very line being sized, so it
    // is not added here. docs/known-issues.md
    const uint32_t units = (uint32_t)showable;
    const uint32_t divider = units * (lineDoubled_ ? 2u : 1u);
    return divider > Adc::DividerMax ? (uint16_t)Adc::DividerMax : (uint16_t)divider;
}

int16_t VideoPath::unitsFor(int16_t pixels, const Scale &scale, const Axis &axis)
{
    return pixels == 0 ? 0 : axis.stepUnits(pixels, scale.magnification());
}

bool VideoPath::pan(int16_t dxPixels, int16_t dyPixels)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(AxisHorizontal, unitsFor(dxPixels, output_.horizontal().scale(), AxisHorizontal),
                 usableHorizontal_, reachHorizontal_);
    wanted.panBy(AxisVertical, unitsFor(dyPixels, output_.vertical().scale(), AxisVertical),
                 usableVertical_, reachVertical_);
    return step(wanted);
}

uint16_t VideoPath::narrowestCaptureOn(const Axis &axis) const
{
    return OutputWindow::narrowestCapture(axis, raster_);
}

uint16_t VideoPath::widestCaptureOn(const Axis &axis) const
{
    return OutputWindow::widestCapture(axis, raster_);
}

void VideoPath::narrowToRaster(PanAndZoom &framing, const CaptureWindow &capture,
                               const Axis &axis) const
{
    // The bound is a capture WIDTH, so it is compared against what the line can
    // realise; the proportion it becomes is of the whole line, which is what the
    // framing is anchored to.
    const uint16_t whole = capture.lineUnitsOn(axis);
    const uint16_t first = capture.firstUnitOn(axis);
    const uint16_t last = capture.reachOn(axis);
    const uint16_t reachable = last > first ? (uint16_t)(last - first) : 0;

    const uint16_t most = widestCaptureOn(axis);
    if (whole == 0 || most == 0 || most >= reachable)
        return;

    framing.narrowTo(axis, (float)most / (float)whole);
}

bool VideoPath::rasterSolved() const
{
    return raster_.horizontalTotal >= 64 && raster_.verticalTotal >= 64;
}

bool VideoPath::zoom(int16_t dhPixels, int16_t dvPixels)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(AxisHorizontal, unitsFor(dhPixels, output_.horizontal().scale(), AxisHorizontal),
                  usableHorizontal_, reachHorizontal_,
                  narrowestCaptureOn(AxisHorizontal));
    wanted.zoomBy(AxisVertical, unitsFor(dvPixels, output_.vertical().scale(), AxisVertical),
                  usableVertical_, reachVertical_,
                  narrowestCaptureOn(AxisVertical));
    return step(wanted);
}

bool VideoPath::applyFraming(const PanAndZoom &framing)
{
    return step(framing);
}

bool VideoPath::refused(const char *step, const CaptureWindow &capture)
{
    char line[96];
    snprintf(line, sizeof(line),
             "solve refused: %s (capture %dx%d, raster %ux%u, lines %u)",
             step, (int)capture.horizontal().width(), (int)capture.vertical().width(),
             (unsigned)raster_.horizontalTotal, (unsigned)raster_.verticalTotal,
             (unsigned)sampling_.sourceLines());
    tv5725Log(line);
    return false;
}

bool VideoPath::fail()
{
    solvePending_ = true;
    return false;
}

bool VideoPath::sizeCaptureWindow(CaptureWindow &capture)
{
    // Bypass is not a failure to retry: there is nothing to solve.
    if (!rasterSolved()) {
        solvePending_ = false;
        return false;
    }

    if (inputFormatter_.lineUnits() < 64)
        return fail();

    // **A MEASUREMENT IN RANGE IS NOT A MEASUREMENT THAT SETTLED**, and the
    // vertical axis is the one it fools: the line comes from the held divider,
    // while this is entirely the source's line count. Sampled through a preset
    // load the count passes 506, 251, 269, 259 and 511 -- all inside the bounds
    // a range check applies, and a solve that lands on one sizes the vertical
    // window for a frame the source is not sending.
    //
    // VideoSignal is the one owner of the bounds, on both the count and the
    // rate.
    const uint16_t sourceLines = sampling_.sourceLines();
    if (!VideoSignal::isVideo(sourceLines, sampling_.fieldRateHz()))
        return fail();

    capture = CaptureWindow(inputFormatter_.capturableLine(reading_,
                                                          SyncMeasurement::isCsync()),
                            inputFormatter_.capturableFrame(sourceLines),
                            timing_);
    return true;
}

bool VideoPath::calculateInputFormatterRegisters(CaptureWindow &capture)
{
    // Forced, the whole capturable region every solve, so no stored framing and
    // no source change can put the bench rule back where it was.
    PanAndZoom wanted = fullFraming_ ? PanAndZoom(0.0f, 1.0f, 0.0f, 1.0f) : framing_;
    narrowToRaster(wanted, capture, AxisHorizontal);
    narrowToRaster(wanted, capture, AxisVertical);
    capture.setFraming(wanted);
    framing_ = capture.framing();
    usableHorizontal_ = capture.lineUnitsOn(AxisHorizontal);
    usableVertical_ = capture.lineUnitsOn(AxisVertical);
    reachHorizontal_ = capture.reachOn(AxisHorizontal);
    reachVertical_ = capture.reachOn(AxisVertical);
    firstHorizontal_ = capture.firstUnitOn(AxisHorizontal);
    firstVertical_ = capture.firstUnitOn(AxisVertical);
    return capture.usable() ? true : fail();
}

OutputWindow VideoPath::imageFor(const CaptureWindow &capture) const
{
    // The window the hardware plays out, which is the register pair: scaling the
    // picture alone runs the far end past the aperture and the source's last
    // line is blanked.
    return OutputWindow(capture.horizontal().width(), capture.vertical().width(),
                        raster_);
}

void VideoPath::write(const OutputWindow &solved, const CaptureWindow &capture)
{
    // 1. Far edges OUTWARD only, which can only add headroom. The memory window
    // hugs the picture, so it moves in as well as out; narrowing it here would
    // leave the old, wider display window showing unwritten memory at the far
    // edge for the length of a write. Inward moves wait for step 5b.
    if (solved.horizontal().memory().start() > GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.horizontal().memory().start());
    if (solved.vertical().memory().start() > GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.vertical().memory().start());

    // 2. Near edges down, if down is where they are going.
    if (solved.horizontal().memory().stop() < GBS::VDS_HB_SP::read())
        GBS::VDS_HB_SP::write(solved.horizontal().memory().stop());
    if (solved.vertical().memory().stop() < GBS::VDS_VB_SP::read())
        GBS::VDS_VB_SP::write(solved.vertical().memory().stop());

    // 3. The picture. BYPS cleared because an explicit scale was computed.
    //
    // The line double's progressive window spans one whole line, so it is
    // recomputed on every solve. Its start is written rather than read, or a
    // clobbered preset byte would propagate into the stop.
    GBS::IF_LINE_ST::write(capture.progressiveWindow().stop());
    GBS::IF_LINE_SP::write(capture.progressiveWindow().start());
    GBS::IF_HB_SP2::write(capture.horizontal().stop());
    GBS::IF_HB_ST2::write(capture.horizontal().start());
    GBS::IF_VB_SP::write(capture.vertical().stop());
    GBS::IF_VB_ST::write(capture.vertical().start());
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HSCALE::write(solved.horizontal().scale().reg());
    GBS::VDS_VSCALE::write(solved.vertical().scale().reg());

    // 4. Near edges up, now that the picture they bound is the new one.
    GBS::VDS_HB_SP::write(solved.horizontal().memory().stop());
    GBS::VDS_VB_SP::write(solved.vertical().memory().stop());

    // 5. The aperture, which must hug the picture. Through the blank state, so
    // a solve taken while the picture is hidden does not put it back on screen.
    output_ = solved;
    GBS::VDS_DIS_HB_SP::write(output_.horizontal().display().stop());
    GBS::VDS_DIS_HB_ST::write(output_.horizontal().display().start());
    writeDisplayAperture();

    // 5b. Far edges INWARD, now that the aperture they bound has closed. The
    // mirror of step 2: a window edge may only cross the display window's in
    // the direction that keeps the picture covered.
    if (solved.horizontal().memory().start() < GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.horizontal().memory().start());
    if (solved.vertical().memory().start() < GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.vertical().memory().start());

    // 6. The playback burst, only if it is not already right. Rewriting
    // PB_FETCH_NUM reprograms the playback FIFO while the picture is being read
    // out of it, which flickers even when the value written is identical.
    // docs/investigations/horizontal-scale-corruption.md
    uint16_t fetch = MemoryWindow::fetchFor(capture.horizontal().width());
    uint16_t offset = MemoryWindow::strideFor(capture.lineUnitsOn(AxisHorizontal));
    if (GBS::PB_FETCH_NUM::read() != fetch)
        GBS::PB_FETCH_NUM::write(fetch);
    if (GBS::PB_CAP_OFFSET::read() != offset)
        GBS::PB_CAP_OFFSET::write(offset);
}

bool VideoPath::step(const PanAndZoom &wanted)
{
    if (fullFraming_)
        return false;

    PanAndZoom before = framing_;
    framing_ = wanted;

    uint16_t horizontalStop = GBS::IF_HB_SP2::read();
    uint16_t horizontalStart = GBS::IF_HB_ST2::read();
    uint16_t verticalStop = GBS::IF_VB_SP::read();
    uint16_t verticalStart = GBS::IF_VB_ST::read();

    if (!solveWindows()) {
        framing_ = before;
        return false;
    }
    if (GBS::IF_HB_SP2::read() == horizontalStop && GBS::IF_HB_ST2::read() == horizontalStart
        && GBS::IF_VB_SP::read() == verticalStop && GBS::IF_VB_ST::read() == verticalStart) {
        framing_ = before;
        return false;
    }
    // A press that moved a window is what makes this framing worth a place in
    // the table, and it goes in NOW rather than when the source is left: a unit
    // turned off where it is used would otherwise lose every tuning. Only the
    // flash write is debounced. One that moved nothing stores nothing, which is
    // also what keeps sixteen places from filling with computed defaults.
    framings_.remember(framedKey_, framing_);
    return true;
}

}  // namespace Tv5725
