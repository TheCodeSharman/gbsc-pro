#include "VideoPath.h"

#include <Arduino.h>

#include "Chip.h"

#include <math.h>
#include <stdio.h>

#include "Adc.h"
#include "CaptureWindow.h"
#include "FrameBuffer.h"
#include "InputFormatter.h"
#include "Memory.h"
#include "MemoryMap.h"
#include "OutputMode.h"
#include "ModeDetect.h"
#include "SyncProcessor.h"
#include "SyncType.h"

namespace Tv5725 {

// --- VideoPath ----------------------------------------------------------

VideoPath::VideoPath(DisplayClock &displayClock, SourceMeasurement &sampling)
    : displayClock_(displayClock),
      usableHorizontal_(0), usableVertical_(0),
      sampling_(sampling), samplingPending_(false), sourceInterrupted_(false), referenceRateHz_(0),
      framingRevision_(0),
      scanModeApplied_(false), syncTypeProbed_(false), syncProbe_(0),
      mayRun_(0),
      solvedLines_(0), solvedLineRateHz_(0),
      idleLines_(0), idleRun_(0), unusableCountArmed_(false),
      sourceState_(SourceAbsent),
      candidateRateHz_(0), rateRun_(0),
      solvePending_(false), modePending_(false), modeOversample_(4),
      choice_(), rasterMode_(0),
      rasterLinePx_(0), rasterFrameLines_(0), activeStop_(0),
      activeLinesStop_(0) {}

const PanAndZoom &VideoPath::framing() const { return framing_; }

const FramingTable &VideoPath::framings() const { return framings_; }

bool VideoPath::rememberFraming(const SourceKey &key, const PanAndZoom &framing)
{
    if (!framings_.remember(key, framing))
        return false;
    ++framingRevision_;
    return true;
}

const SourceKey &VideoPath::framedKey() const { return framedKey_; }

uint16_t VideoPath::framingRevision() const { return framingRevision_; }

bool VideoPath::changing() const { return modePending_ || solvePending_; }

SourceState VideoPath::sourceState() const { return sourceState_; }

bool VideoPath::sourceIsPresent() const
{
    return sourceState_ == SourceAcquired && !changing();
}

uint16_t VideoPath::capturableOn(const Axis &axis) const
{
    return axis.vertical() ? usableVertical_ : usableHorizontal_;
}

uint16_t VideoPath::originUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.originOn(axis) * (float)capturableOn(axis));
}

uint16_t VideoPath::extentUnitsOn(const Axis &axis) const
{
    return (uint16_t)lrintf(framing_.extentOn(axis) * (float)capturableOn(axis));
}

// Measure the source, then solve from it. For the two callers that need the
// source read again: the deferred retry, whose previous solve was refused
// against the measurement it already had, and the re-derive command, whose whole
// contract is the source as it reads now. A caller that has only moved the
// framing wants solveWindows(), which costs no vsync sample.
bool VideoPath::resolve()
{
    // The same reference the poll pass takes, and for the same reason: a window
    // solved for a taller mode strands the block the rate is timed off, and a
    // count taken through the previous mode's divider is not the source's.
    // Neither caller here reaches the one in poll().
    holdReferenceSampling();

    if (!sampling_.measureLineRate())
        return fail();
    return solveWindows();
}

bool VideoPath::solveWindows()
{
    CaptureWindow capture;
    if (!measureSourceTimings(capture))
        return false;
    if (!calculateInputFormatterRegisters(capture))
        return false;

    VideoProcessorTimings solved = calculateOutputRaster(capture);
    if (!solved.usable())
        return fail();

    write(solved, capture);
    horizontalScale_ = solved.horizontalScale();
    verticalScale_ = solved.verticalScale();
    solvePending_ = false;
    return true;
}

const OutputMode *VideoPath::outputMode() const { return rasterMode_; }

bool VideoPath::solveRaster()
{
    // The choice is an input, not a read-back. Deriving the mode from
    // VDS_VSYNC_RST would leave the preset table -- the thing this replaces --
    // its only writer. docs/chip-initialisation.md.
    const OutputMode *mode = choice_.resolve();
    rasterMode_ = mode;
    if (mode == 0) {
        // Not a failure, and NOT a fall back to 1080p: the choice names no
        // resolution, and a raster nobody has swept keeps what it had.
        return false;
    }
    if (mode->isBypass()) {
        // Named rather than left to usable(): the output is the source's own
        // timing, so there is no raster to compute and solving one would write
        // zeros that every other register agrees with.
        return false;
    }

    // The pass's own measurement. A raster solved at the wrong rate is out by
    // the ratio of the rates, and lineRateFrom() is what refuses one: it bounds
    // the line count, picks the nominal rate from it, and requires the measured
    // rate to agree within 2%. Reaching here means that passed, so re-reading
    // both to re-apply it would only add a second answer to disagree with.
    // docs/firmware-geometry-engine.md
    // EngineCeilingHz, not the higher WorkingCeilingHz the part is measured to
    // run at: a wider raster costs zoom travel. See the constant.
    OutputTimings raster = mode->solve(sampling_.fieldRateHz(),
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
    rasterLinePx_ = raster.horizontalTotal;
    rasterFrameLines_ = raster.verticalTotal;

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

    // The porch is not a register, so the next solve cannot read it back.
    activeStop_ = raster.activeStop;
    activeLinesStop_ = raster.activeLinesStop;

    return true;
}

void VideoPath::adoptRaster()
{
    rasterLinePx_ = GBS::VDS_HSYNC_RST::read() + 1;
    rasterFrameLines_ = GBS::VDS_VSYNC_RST::read() + 1;
    displayClock_.adopt();
}

void VideoPath::inputTimingsChanged(uint8_t oversample)
{
    // The windows land seconds from now, once the source has settled into the
    // mode; until then the previous mode's geometry is what the new source
    // would be shown through.
    FrameBuffer::freezeCapture();

    modePending_ = true;
    modeOversample_ = oversample;
    scanModeApplied_ = false;
    syncTypeProbed_ = false;

    // Off the held choice, not off an argument. solveRaster() derives it again
    // when the solve runs; this keeps outputMode() answering consistently until
    // then.
    rasterMode_ = choice_.resolve();

    // The line count is about to move, so the steadiness run so far means
    // nothing.
    sampling_.resetSteadiness();

    // The sampling clock, BEFORE anything tries to measure. A load leaves the
    // ADC PLL on the bring-up's crossover row, and the sync processor counts in
    // ADC clocks -- so every measurement is garbage until this runs, the
    // steadiness gate never passes, and the pass that would have fixed the
    // clock never arrives.
    writeSampling();
}

bool VideoPath::outputModeChanged(const OutputChoice &choice)
{
    choice_ = choice;
    if (modePending_)
        return false;

    // The frame height the scan mode is judged against, before it is judged.
    // **THE LINE DOUBLER IS A PROPERTY OF THE OUTPUT AS MUCH AS OF THE SOURCE**:
    // what decides it is whether the doubled frame fits the raster, so a shorter
    // raster strands a doubling that fitted the taller one.
    rasterMode_ = choice.resolve();

    const bool wasDoubled = sampling_.lineDoubled();
    solveScanMode();

    // Only where the doubling moved. The divider derives from it and from the
    // line rate already held -- so it is re-DERIVED, never re-measured -- and
    // writing it re-latches the ADC PLL, which is a relock nothing asked for.
    if (sampling_.lineDoubled() != wasDoubled
        && !solveSampling(modeOversample_))
        return false;

    if (!solveRaster())
        return false;

    // raster -> clock -> windows, the same order poll() runs and for the same
    // reason: the clock reads the seed the raster just chose.
    displayClock_.reset();
    return solveWindows();
}

bool VideoPath::poll(bool detectionDue)
{
    if (mayRun_ != 0 && !mayRun_())
        return false;

    if (!modePending_) {
        if (detectionDue && sourceMoved())
            inputTimingsChanged(modeOversample_);
        return modePending_ ? false : (solvePending_ ? resolve() : false);
    }

    // **BEFORE EVERYTHING, INCLUDING THE SCAN MODE.** Every measurement below
    // is counted through the sync path, so a path still set for the source
    // before this one leaves the gates shut and nothing downstream can open
    // them: a separate-sync source on the csync path counts 97 lines for ever.
    establishSyncType();

    // **BEFORE THE GATES BELOW, AND THIS IS THE POINT OF IT.** The input
    // formatter's own measurements are only meaningful once its scan mode
    // matches the source, so a scan mode left wrong makes the gates fail and a
    // scan mode derived after them is never reached. The sync processor counts
    // the source directly and is indifferent to the scan mode, which is what
    // makes the line count usable here and nothing else.
    solveScanMode();

    // **BEFORE THE LINE COUNT, because the line count is a measurement too.**
    // The sync processor counts in ADC clocks, so on the previous mode's
    // divider the PLL sits outside its lock range and the count that comes back
    // is not the source's -- and that count is what the gate below is reading.
    // Applying the reference afterwards puts the fix on the far side of the
    // gate its absence holds shut.
    // docs/investigations/field-rate-measured-downstream.md
    holdReferenceSampling();

    // The cheap gate. Everything below this line measures, and the field rate
    // costs up to 250 ms a vsync pulse. The reference above is what opens it:
    // a count taken through the previous mode's divider is not the source's.
    if (!sampling_.sampleSteady()) {
        // The count settled on the serrations, so the pair in force is not
        // covering them. Margin over the default rather than a search for the
        // lowest pair that works: which pairs measure a source is not
        // reproducible between runs.
        // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
        if (sampling_.countWasSerrations())
            SyncProcessor::widenCoast();
        return noSourceToSolve();
    }

    // THE measurement of the source for this pass. Everything below derives
    // from it -- the divider, the raster, both windows -- so nothing can end up
    // solved against a rate something else was not.
    if (!sampling_.measureLineRate()) {
        // Deferred, not settled for. The reference above is already a divider
        // the capture window can be measured in, so there is nothing to inherit
        // and the flag is only a note to re-solve.
        samplingPending_ = true;
        return noSourceToSolve();
    }

    // A rate is worth sizing a raster from once it has REPEATED. The cross-check
    // inside measureLineRate() bounds the rate against the line count, which
    // catches a settling source off by tens of percent and passes one off by
    // tenths -- and the raster is out by whatever fraction the rate is, for
    // good, because nothing re-solves it.
    if (!sampling_.rateSettled())
        return false;

    if (!solveSampling(modeOversample_))
        return false;

    // Whatever raster is on the chip, taken before the solve that replaces it,
    // so a solve that still refuses leaves the windows sized for something.
    adoptRaster();
    if (!solveRaster()) {
        // solveRaster() never defers: both its refusals are final, so a retry
        // would pay for a field rate measurement to reach the same answer.
        modePending_ = false;
        FrameBuffer::releaseCapture();
        return false;
    }

    // raster -> clock -> windows. The clock reads the seed the raster just
    // chose, and every window is sized against the raster it lands on.
    displayClock_.reset();
    solveForSource();

    // What this solve ran against, so a source that later differs from it arms
    // the engine without anyone having to say so.
    solvedLines_ = sampling_.sourceLines();
    solvedLineRateHz_ = sampling_.lineRateHz();
    holdSolvedSource();
    modePending_ = false;
    FrameBuffer::releaseCapture();
    return true;
}


bool VideoPath::reset()
{
    // The entry goes with the framing. "Back to default" has to mean the table
    // stops answering for this source, or the solve that follows restores
    // exactly what was just discarded and the control does nothing.
    if (framings_.forget(framedKey_))
        ++framingRevision_;

    // A framing change like any other. The source has not moved and no load has
    // disturbed the ADC, so the divider, the raster and the clock all re-derive
    // to what they already hold -- and re-arming would freeze capture for
    // seconds to reach them.
    framing_.reset();
    return solveWindows();
}

void VideoPath::sourceInterrupted()
{
    sourceInterrupted_ = true;
}

void VideoPath::enterBypass()
{
    // The measurement is NOT discarded. Bypass does not measure, so what is
    // held is the rate from the mode that preceded it -- which is the fact a
    // caller asking whether the display can show this source wants, and the
    // only place it exists once the standard byte is gone.

    // No raster is solved here, so the register is the only source of the seed
    // the encoder is already running on.
    displayClock_.adopt();

    modePending_ = false;
    FrameBuffer::releaseCapture();

    choice_ = OutputChoice(OutputBypass);
    rasterMode_ = &ModeBypass;
    rasterLinePx_ = 0;
    rasterFrameLines_ = 0;
    samplingPending_ = false;
    solvedLines_ = 0;

    // Bypass has no solved raster, so it has no porch either -- and a porch left
    // from the last scaled mode would size the next one's picture.
    activeStop_ = 0;
    activeLinesStop_ = 0;
}

bool VideoPath::solveForSource()
{
    const SourceKey arriving(sampling_.sourceLines(), sampling_.fieldRateHz());
    if (arriving == framedKey_)
        return solveWindows();

    // Leaving one source for another. Nothing is stored here: the table has
    // followed every press already, so what this source was tuned to is in it.
    if (!framings_.find(arriving, &framing_))
        framing_.reset();
    framedKey_ = arriving;
    return solveWindows();
}



void VideoPath::useSyncTypeProbe(bool (*hasOwnVsync)()) { syncProbe_ = hasOwnVsync; }

void VideoPath::useRunGate(bool (*mayRun)()) { mayRun_ = mayRun; }

bool VideoPath::reacquireSyncType()
{
    syncTypeProbed_ = false;
    establishSyncType();
    return SyncType::isCsync();
}

void VideoPath::establishSyncType()
{
    if (syncTypeProbed_ || syncProbe_ == 0)
        return;
    syncTypeProbed_ = true;

    const bool csync = SyncType::probe(syncProbe_);
    SyncProcessor::applyForSyncType(csync);
    ModeDetect::applySyncType(csync ? ModeDetect::Csync : ModeDetect::SeparateSync);
    delay(SyncProcessor::PathSettleMs);
}

void VideoPath::holdReferenceSampling()
{
    const uint16_t reference = SourceMeasurement::referenceDivider(sampling_.lineDoubled());
    const uint32_t estimate = sampling_.estimatedLineRateHz();

    // Unconditional, ahead of the return below. The reference divider is a
    // function of the scan mode alone, so a source that did not move asks for
    // the one already in force -- and a window is not only stranded by a mode
    // change. Nothing else writes these two until a solve succeeds, which is
    // the thing they are stopping.
    InputFormatter::writeReferenceVerticalBlank();

    // The estimate is half of it, not a detail: PLLAD_KS is an octave of CKO,
    // which is the divider TIMES the rate. A count caught mid-transition picks
    // the wrong octave, and the reference divider for a scan mode does not
    // change when the count settles -- so a return keyed on the divider alone
    // leaves KS wrong with PLLAD_MD right, which is a state nothing can measure
    // its way out of.
    if (sampling_.divider() == reference && estimate == referenceRateHz_)
        return;

    referenceRateHz_ = estimate;
    sampling_.holdDivider(reference);
    // The oversampling stays as the mode asks for it: PLLAD_CKOS and the
    // decimators describe one ratio between them, and the IF's units come off
    // the decimated clock. Only the divider is being moved to a known value.
    Adc::applySampleRate(reference, estimate, modeOversample_);
    InputFormatter::writeLineCounter(sampling_.ifLine());
    SyncProcessor::writeRetimeStop(sampling_.retimeStop());
}

// One quantity in three registers, each written by the block that declares it.
// The divider goes first because Adc latches it, and the latch loads KS, CKOS
// and ICP with it -- so anything setting those must already have run.
void VideoPath::writeSampling()
{
    if (!sampling_.usable())
        return;

    Adc::applySampleRate(sampling_.divider(), sampling_.lineRateHz(),
                         modeOversample_);
    InputFormatter::writeLineCounter(sampling_.ifLine());
    SyncProcessor::writeRetimeStop(sampling_.retimeStop());
}

static void logSourceState(SourceState state, uint16_t lines, uint16_t samples,
                           uint16_t divider)
{
    char line[88];
    snprintf(line, sizeof(line),
             "source %s: %u lines, %u samples against divider %u",
             state == SourceAcquired   ? "acquired"
             : state == SourceUnlocked ? "UNLOCKED"
                                       : "absent",
             (unsigned)lines, (unsigned)samples, (unsigned)divider);
    tv5725Log(line);
}

static void logSourceMoved(const char *why, uint16_t lines, uint16_t solved)
{
    char line[72];
    snprintf(line, sizeof(line), "source moved: %s (%u lines, solved %u)",
             why, (unsigned)lines, (unsigned)solved);
    tv5725Log(line);
}

// A solving pass that could not measure the source. sourceMoved() is the only
// other writer of this and the solving branch never reaches it, so without this
// the answer holds whatever the last idle pass concluded -- true -- for as long
// as the solve goes on failing. That is precisely when whoever reads it needs
// to know the source is not usable.
bool VideoPath::noSourceToSolve()
{
    sourceState_ = SourceAbsent;
    return false;
}

// The solve gated on its own steadiness run over this count, longer than the
// idle one, so the idle run starts satisfied rather than re-earning what has
// just been measured and dipping sourceIsPresent() for the polls it takes.
void VideoPath::holdSolvedSource()
{
    idleLines_ = solvedLines_;
    idleRun_ = SourceMeasurement::SteadySamples;

    // A solve that has just written the divider has not had a line counted
    // through it yet, so the sampling half is asked on the next idle pass
    // rather than assumed here.
    sourceState_ = SourceAcquired;
}

// Whether the count has held long enough to be the source's rather than a
// reading taken through something still settling.
bool VideoPath::countHeld(uint16_t lines)
{
    if (lines != idleLines_) {
        idleLines_ = lines;
        idleRun_ = 0;
        return false;
    }
    if (idleRun_ < SourceMeasurement::SteadySamples) {
        ++idleRun_;
        return false;
    }
    return true;
}

// **THIS MUST NOT USE sampling_.sampleSteady().** That call is the solve's own
// steadiness run, and filling it while the engine is idle leaves the next mode
// change's first poll believing a count from the mode before it.
bool VideoPath::sourceMoved()
{
    // Bypass has no scaled raster to re-solve, and enterBypass() drops the mode
    // change so a later poll cannot write one over the setup it just chose.
    if (rasterMode_ == 0 || rasterMode_->isBypass() || solvedLines_ == 0) {
        sourceInterrupted_ = false;
        return false;
    }

    const uint16_t lines = SourceMeasurement::measureSourceLines();

    // ONE ADVANCE OF THE RUN PER POLL. countHeld() mutates it, so a second
    // caller double-advances it and the steadiness both readers depend on is
    // no longer over consecutive polls.
    const bool plausible = SourceMeasurement::countIsSource(lines);
    const bool held = countHeld(lines);

    // The horizontal half, and it is not a second steadiness run: the divider
    // is held state the engine chose, so one reading of what the sync processor
    // counts against it is the whole test.
    const uint16_t lineSamples = SourceMeasurement::measureLineSamples();
    const SourceState was = sourceState_;
    sourceState_ = !(plausible && held) ? SourceAbsent
                   : SourceMeasurement::dividerLatched(lineSamples,
                                                       sampling_.divider())
                       ? SourceAcquired
                       : SourceUnlocked;

    // The state changing is worth a line because the fault it exists to name is
    // INTERMITTENT and a poll fast enough to catch it changes what the unit
    // does. This costs no bus traffic the answer did not already need.
    if (sourceState_ != was)
        logSourceState(sourceState_, lines, lineSamples, sampling_.divider());

    // A count no source runs is the wrong sync path's signature -- 97..137 on a
    // 311-line source, measured -- and a mode change is the only thing that
    // re-establishes the sync type, so the state that most needs a re-probe was
    // the one state that could never arm one. It arms ONCE: the count stays
    // wrong until the probe has moved the path.
    if (!plausible) {
        if (!held || unusableCountArmed_)
            return false;
        unusableCountArmed_ = true;
        logSourceMoved("unusable count", lines, solvedLines_);
        return true;
    }

    unusableCountArmed_ = false;
    if (!held)
        return false;

    // The rate and the interrupt each say the source moved where the count
    // cannot: the same number of lines at a different field rate, which is what
    // 320x256 at 50, 55 and 60 all are. Both wait behind the SAME steadiness run
    // rather than firing on arrival, because a source measured mid-transition
    // yields a rate that passes every check and is tens of percent out --
    // measured at 18806 Hz against a real 31440, held, with every register
    // self-consistent.
    const bool interrupted = sourceInterrupted_;
    sourceInterrupted_ = false;
    const bool countMoved = lines != solvedLines_;
    if (!interrupted && !countMoved && !rateMoved())
        return false;

    logSourceMoved(interrupted ? "interrupt" : countMoved ? "count" : "rate",
                   lines, solvedLines_);
    idleRun_ = 0;
    return true;
}

bool VideoPath::rateMoved()
{
    const uint32_t rate = SourceMeasurement::measureLineRateFromHPeriod(solvedLines_);
    if (rate == 0 || solvedLineRateHz_ == 0
        || SourceMeasurement::ratesAgree(rate, solvedLineRateHz_)) {
        candidateRateHz_ = 0;
        rateRun_ = 0;
        return false;
    }

    if (candidateRateHz_ == 0
        || !SourceMeasurement::ratesAgree(rate, candidateRateHz_)) {
        candidateRateHz_ = rate;
        rateRun_ = 1;
        return false;
    }
    if (rateRun_ < SourceMeasurement::SteadySamples) {
        ++rateRun_;
        return false;
    }

    candidateRateHz_ = 0;
    rateRun_ = 0;

    // HPERIOD_IF rails to a value that is WRONG AND STABLE, which no run can
    // reject: 511 reads as 13183 Hz against a real 15625 and holds. The field
    // rate is measured a different way and does not rail with it. It costs a
    // vsync spin, which is what the cheap gate exists to avoid -- affordable
    // only because a corroborated disagreement is rare.
    const uint32_t confirmed = SourceMeasurement::lineRateFrom(
        solvedLines_, getSourceFieldRate(0));
    if (confirmed == 0 || !SourceMeasurement::ratesAgree(rate, confirmed))
        return false;

    // The held rate is what moved, and measureLineRate() rejects a rate that
    // changed at an unchanged count -- so leaving it would refuse the very
    // measurement this armed the solve for.
    sampling_.forgetHeldRate();
    return true;
}

void VideoPath::solveScanMode()
{
    const uint16_t lines =
        SourceMeasurement::measureSourceLinesCorrected(sampling_.divider());
    if (!SourceMeasurement::countIsSource(lines))
        return;

    // Against the mode ASKED FOR rather than the raster last solved: this runs
    // before solveRaster(), so the held raster is the previous output's, and a
    // mode change would decide the scan mode from the resolution it is leaving.
    // The porch is not known this early either, so the bound is the raster's own
    // edge and a doubling that only just fits is caught by the capture clamp.
    const uint16_t showable =
        rasterMode_ && !rasterMode_->isBypass()
            ? AxisVertical.maximumCapture(rasterMode_->frameLines(), 0) : 0;
    const bool doubled = SourceMeasurement::lineDoublingFor(lines, showable);
    if (scanModeApplied_ && doubled == sampling_.lineDoubled())
        return;

    sampling_.holdLineDoubling(doubled);
    InputFormatter::applyScanMode(doubled ? InputFormatter::LineDoubled
                                          : InputFormatter::Progressive);
    scanModeApplied_ = true;
}

bool VideoPath::solveSampling(uint8_t oversample)
{
    if (!sampling_.solve(sampling_.lineRateHz(), oversample)) {
        samplingPending_ = true;
        return false;
    }
    writeSampling();
    samplingPending_ = false;
    return true;
}

int16_t VideoPath::unitsFor(int16_t pixels, const Scale &scale, const Axis &axis)
{
    return pixels == 0 ? 0 : axis.stepUnits(pixels, scale.magnification());
}

bool VideoPath::pan(int16_t dxPixels, int16_t dyPixels)
{
    PanAndZoom wanted = framing_;
    wanted.panBy(AxisHorizontal, unitsFor(dxPixels, horizontalScale_, AxisHorizontal),
                 usableHorizontal_);
    wanted.panBy(AxisVertical, unitsFor(dyPixels, verticalScale_, AxisVertical),
                 usableVertical_);
    return step(wanted);
}

bool VideoPath::zoom(int16_t dhPixels, int16_t dvPixels)
{
    PanAndZoom wanted = framing_;
    wanted.zoomBy(AxisHorizontal, unitsFor(dhPixels, horizontalScale_, AxisHorizontal),
                  usableHorizontal_);
    wanted.zoomBy(AxisVertical, unitsFor(dvPixels, verticalScale_, AxisVertical),
                  usableVertical_);
    return step(wanted);
}

bool VideoPath::applyFraming(const PanAndZoom &framing)
{
    return step(framing);
}

bool VideoPath::fail()
{
    solvePending_ = true;
    return false;
}

bool VideoPath::measureSourceTimings(CaptureWindow &capture)
{
    capture.setRasters(rasterLinePx_, rasterFrameLines_, activeStop_,
                       activeLinesStop_);
    if (!capture.readRasters(sampling_, SourceMeasurement::measureHsyncLow())) {
        // Bypass is not a failure to retry: there is nothing to solve.
        if (!capture.scaling()) {
            solvePending_ = false;
            return false;
        }
        return fail();
    }
    if (!capture.scaling()) {
        solvePending_ = false;
        return false;
    }
    return true;
}

bool VideoPath::calculateInputFormatterRegisters(CaptureWindow &capture)
{
    capture.setFraming(framing_);
    framing_ = capture.framing();
    usableHorizontal_ = capture.capturableOn(AxisHorizontal);
    usableVertical_ = capture.capturableOn(AxisVertical);
    return capture.usable() ? true : fail();
}

VideoProcessorTimings VideoPath::calculateOutputRaster(const CaptureWindow &capture) const
{
    return VideoProcessorTimings(capture.horizontal().width(), capture.vertical().width(),
                            capture.linePx(), capture.frameLines(),
                            activeStop_, activeLinesStop_);
}

void VideoPath::write(const VideoProcessorTimings &solved, const CaptureWindow &capture)
{
    // 1. Far edges OUTWARD only, which can only add headroom. The memory window
    // hugs the picture, so it moves in as well as out; narrowing it here would
    // leave the old, wider display window showing unwritten memory at the far
    // edge for the length of a write. Inward moves wait for step 5b.
    if (solved.memory().horizontal().start() > GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.memory().horizontal().start());
    if (solved.memory().vertical().start() > GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.memory().vertical().start());

    // 2. Near edges down, if down is where they are going.
    if (solved.memory().horizontal().stop() < GBS::VDS_HB_SP::read())
        GBS::VDS_HB_SP::write(solved.memory().horizontal().stop());
    if (solved.memory().vertical().stop() < GBS::VDS_VB_SP::read())
        GBS::VDS_VB_SP::write(solved.memory().vertical().stop());

    // 3. The picture. BYPS cleared because an explicit scale was computed.
    //
    // The line double's progressive window spans one whole line, so it is
    // recomputed on every solve. Its start is written rather than read, or a
    // clobbered preset byte would propagate into the stop.
    GBS::IF_LINE_ST::write(CaptureWindow::ProgressiveStart);
    GBS::IF_LINE_SP::write(capture.horizontalLine().progressiveStop(CaptureWindow::ProgressiveStart));
    GBS::IF_HB_SP2::write(capture.horizontal().stop());
    GBS::IF_HB_ST2::write(capture.horizontal().start());
    GBS::IF_VB_SP::write(capture.vertical().stop());
    GBS::IF_VB_ST::write(capture.vertical().start());
    GBS::VDS_HSCALE_BYPS::write(0);
    GBS::VDS_VSCALE_BYPS::write(0);
    GBS::VDS_HSCALE::write(solved.horizontalScale().reg());
    GBS::VDS_VSCALE::write(solved.verticalScale().reg());

    // 4. Near edges up, now that the picture they bound is the new one.
    GBS::VDS_HB_SP::write(solved.memory().horizontal().stop());
    GBS::VDS_VB_SP::write(solved.memory().vertical().stop());

    // 5. The aperture, which must hug the picture.
    GBS::VDS_DIS_HB_SP::write(solved.display().horizontal().stop());
    GBS::VDS_DIS_HB_ST::write(solved.display().horizontal().start());
    GBS::VDS_DIS_VB_SP::write(solved.display().vertical().stop());
    GBS::VDS_DIS_VB_ST::write(solved.display().vertical().start());

    // 5b. Far edges INWARD, now that the aperture they bound has closed. The
    // mirror of step 2: a window edge may only cross the display window's in
    // the direction that keeps the picture covered.
    if (solved.memory().horizontal().start() < GBS::VDS_HB_ST::read())
        GBS::VDS_HB_ST::write(solved.memory().horizontal().start());
    if (solved.memory().vertical().start() < GBS::VDS_VB_ST::read())
        GBS::VDS_VB_ST::write(solved.memory().vertical().start());

    // 6. The playback burst, only if it is not already right. Rewriting
    // PB_FETCH_NUM reprograms the playback FIFO while the picture is being read
    // out of it, which flickers even when the value written is identical.
    // docs/investigations/hscale-tearing-characterisation.md
    uint16_t fetch = Memory::fetchFor(capture.horizontal().width());
    uint16_t offset = Memory::offsetFor(capture.horizontalLine().units());
    if (GBS::PB_FETCH_NUM::read() != fetch)
        GBS::PB_FETCH_NUM::write(fetch);
    if (GBS::PB_CAP_OFFSET::read() != offset)
        GBS::PB_CAP_OFFSET::write(offset);
}

bool VideoPath::step(const PanAndZoom &wanted)
{
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
        return true;
    }
    // A press that moved a window is what makes this framing worth a place in
    // the table, and it goes in NOW rather than when the source is left: a unit
    // turned off where it is used would otherwise lose every tuning. Only the
    // flash write is debounced. One that moved nothing stores nothing, which is
    // also what keeps sixteen places from filling with computed defaults.
    rememberFraming(framedKey_, framing_);
    return true;
}

}  // namespace Tv5725
