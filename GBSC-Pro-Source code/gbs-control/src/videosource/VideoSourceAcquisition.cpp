#include "VideoSourceAcquisition.h"

#include <stdio.h>

#include "../tv5725/Adc.h"
#include "../tv5725/Deinterlacer.h"
#include "../tv5725/FrameBuffer.h"
#include "../tv5725/Interrupts.h"
#include "../tv5725/ModeDetect.h"
#include "../tv5725/Chip.h"
#include "../tv5725/HdBypass.h"
#include "../tv5725/OutputMode.h"
#include "../tv5725/RgbhvOutput.h"
#include "../tv5725/SyncMeasurement.h"
#include "../tv5725/SyncOnGreen.h"
#include "../tv5725/VideoRoute.h"
#include "VideoSourceSelection.h"
#include "../tv5725/SyncProcessor.h"
#include "../tv5725/Tv5725Log.h"

VideoSourceAcquisition::VideoSourceAcquisition(Tv5725::SourceMeasurement &sampling,
                                   Tv5725::VideoPath &videoPath)
    : sampling_(sampling), videoPath_(videoPath), maintenanceAllowed_(false),
      channelSyncServicedEver_(false), channelSyncServicedMs_(0),
      encoderLooking_(false), encoderLookMs_(0),
      mayRun_(0), passThroughSwitch_(0), passThroughAllowed_(false), resolution_(0),
      detectedMs_(0),
      detectedEver_(false), solvedLines_(0), solvedLineRateHz_(0),
      idle_(Tv5725::SourceMeasurement::SteadySamples),
      unusableCountArmed_(false), ownVsyncFound_(false),
      sourceState_(SourceAbsent),
      solvedLinePeriod_(0), rateRun_(0), sourceInterrupted_(false),
      unsettledPasses_(0), unsettledArmed_(false),
      vsyncAbsentPasses_(0), vsyncAbsentArmed_(false),
      unmeasuredPasses_(0), acquiredPasses_(0), recoveryPosition_(0),
      firstAcquisition_(true), firstAcquisitionTimed_(false),
      firstAcquisitionMs_(0),
      selectionSeen_(VideoSourceSelection::selected()),
      runAdvanced_(false) {}

void VideoSourceAcquisition::useRunGate(bool (*mayRun)()) { mayRun_ = mayRun; }

void VideoSourceAcquisition::usePassThroughSwitch(void (*enter)()) { passThroughSwitch_ = enter; }

namespace {

// The platform, which is one fact for the whole firmware. Unset, each is a
// harmless stand-in rather than a crash: a host test that does not need the
// clock must not have to supply one.
uint32_t noClock() { return 0; }
void noWatchdog() {}

uint32_t (*clock_)() = noClock;
void (*watchdog_)() = noWatchdog;

}  // namespace

void VideoSourceAcquisition::useWatchdogFeed(void (*feed)())
{
    watchdog_ = feed != 0 ? feed : noWatchdog;
}

void VideoSourceAcquisition::allowMaintenance(bool allowed)
{
    // Withdrawn after being granted is detection giving up on the source, which
    // is the engine's chance spent: the ladder is warranted from here.
    if (maintenanceAllowed_ && !allowed)
        firstAcquisition_ = false;
    maintenanceAllowed_ = allowed;
}

void VideoSourceAcquisition::useClock(uint32_t (*nowMs)())
{
    clock_ = nowMs != 0 ? nowMs : noClock;
}

namespace {

// How many agreeing readings say the divider is latched.
const uint8_t LatchSamples = 8;

}  // namespace

bool VideoSourceAcquisition::acquireSamplingPhase()
{
    if (!Tv5725::Adc::dividerLatched(Tv5725::SyncProcessor::lineSamples(),
                                     LatchSamples))
        return false;

    // What the ADC is RUNNING, not what was asked for: the request is
    // OversampleAsClockAllows on every source and the crossover row decides
    // what that becomes.
    const uint8_t oversample = Tv5725::Adc::oversampleInForce();

    const bool found = Tv5725::Adc::acquirePhase(
        oversample, Tv5725::SyncOnGreen::level() > Tv5725::SyncOnGreen::StarvedLevel,
        Tv5725::SyncProcessor::lineSamples,
        watchdog_);

    char line[48];
    snprintf(line, sizeof(line), "sampling phase: %s, oversample %u",
             found ? "chosen" : "no clean window", (unsigned)oversample);
    tv5725Log(line);
    return found;
}

void VideoSourceAcquisition::allowPassThrough(bool allowed) { passThroughAllowed_ = allowed; }

bool VideoSourceAcquisition::setOutputResolution(const Tv5725::OutputMode *mode)
{
    resolution_ = mode;

    // Not a reason to leave pass-through, and not a reason to load a preset
    // either. The source has not moved, so it still arrives intact only by being
    // handed over; the resolution is where the output returns when it stops
    // qualifying. A preset load here would take the chip off the bypass route
    // with nothing telling the engine, leaving it holding a mode the chip is no
    // longer in.
    if (outputIsPassedThrough())
        return true;

    return videoPath_.setOutputMode(mode);
}

bool VideoSourceAcquisition::resolveFromSource()
{
    // The same preparation the poll pass makes, and for the same reason: a
    // window solved for a taller mode strands the block the rate is timed off.
    videoPath_.prepareToMeasure(sampling_.readSourceLines());

    bool settling = false;
    if (!measureSource(settling))
        return videoPath_.deferSolve();

    videoPath_.sourceMeasured(sampling_.hsync());

    // The route is re-answered here as well as on a mode change. Pass-through
    // is a statement about the source AND about what the user permits, and the
    // permission moves while the source stands still.
    if (passSourceThrough())
        return true;

    if (outputIsPassedThrough()) {
        // Leaving configures the scaling path and solves nothing. The raster
        // and the line doubling both move with the output and resolve() carries
        // neither, so the whole solve is armed rather than run from here.
        videoPath_.setOutputMode(resolution_);
        videoPath_.inputTimingsChanged();
        return false;
    }

    return videoPath_.resolve();
}

bool VideoSourceAcquisition::outputIsPassedThrough() const
{
    return Tv5725::VideoRoute::isHdBypassChannel();
}

bool VideoSourceAcquisition::passThroughSuitsSource() const
{
    return passThroughAllowed_
           && Tv5725::HdBypass::suitsSource(sampling_.sourceLines(),
                                  sampling_.fieldRateHz())
           && Tv5725::HdBypass::suitsLineRate(sampling_.lineRateHz());
}

bool VideoSourceAcquisition::passSourceThrough()
{
    if (!passThroughSuitsSource())
        return false;

    if (!outputIsPassedThrough()) {
        if (passThroughSwitch_ == 0)
            return false;

        // Entry sizes the channel as part of moving the route, and ends in a
        // phase search -- so re-sizing after it would rewrite the sampling
        // group the search just settled.
        passThroughSwitch_();
    } else {
        // THE ONLY PLACE A SOURCE THAT MOVED INSIDE PASS-THROUGH IS RE-SIZED.
        // The switch sizes the channel on entry and cannot run again while the
        // route is already where it belongs, and VideoPath::installSampling()
        // and prepareToMeasure() both return early here because the bypass
        // divider is not theirs. Without this a mode change keeps the previous
        // mode's divider: measured, 800x600 to 640x480 held PLLAD_MD at 2039
        // against a 524-line source with the ADC PLL unlocked.
        resizePassThrough();
    }

    videoPath_.setOutputMode(&Tv5725::ModeBypass);
    return true;
}

void VideoSourceAcquisition::resizePassThrough()
{
    const uint32_t lineRateHz = sampling_.lineRateHz();
    Tv5725::HdBypass::applyForSource(Tv5725::HdBypass::dividerFor(lineRateHz),
                                     lineRateHz, videoPath_.sourceTiming(),
                                     sampling_.sourceLines() + 1);
}

float VideoSourceAcquisition::sourceFieldRateHz() const { return sampling_.fieldRateHz(); }

uint32_t VideoSourceAcquisition::sourceLineRateHz() const { return sampling_.lineRateHz(); }

bool VideoSourceAcquisition::sourceLowLineRate() const { return sampling_.lowLineRate(); }

VideoSourceAcquisition::SourceState VideoSourceAcquisition::sourceState() const { return sourceState_; }

bool VideoSourceAcquisition::sourceIsPresent() const
{
    return sourceState_ == SourceAcquired && !videoPath_.changing();
}

bool VideoSourceAcquisition::sourceIsSearching() const
{
    return !Tv5725::VideoSignal::countIsSource(
               Tv5725::SyncProcessor::lineCount())
           && !sourceIsPresent();
}

void VideoSourceAcquisition::sourceInterrupted() { sourceInterrupted_ = true; }

bool VideoSourceAcquisition::runAdvanced() const { return runAdvanced_; }

bool VideoSourceAcquisition::detectionDue(uint32_t nowMs)
{
    if (detectedEver_ && nowMs - detectedMs_ < DetectionIntervalMs)
        return false;
    detectedMs_ = nowMs;
    detectedEver_ = true;
    return true;
}

static void logSourceState(VideoSourceAcquisition::SourceState state, uint16_t lines, uint16_t samples,
                           uint16_t divider)
{
    char line[88];
    snprintf(line, sizeof(line),
             "source %s: %u lines, %u samples against divider %u",
             state == VideoSourceAcquisition::SourceAcquired   ? "acquired"
             : state == VideoSourceAcquisition::SourceUnlocked ? "UNLOCKED"
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

// THE ARM SPENDS THE LATCH. The re-measure an arm opens answers whatever
// disturbed the sync separator, so a disturbance taken before an arm raised for
// another reason is not news once that measurement lands. Left set it re-arms
// the moment the solve completes, on the count that solve has just measured,
// and the output is blanked for a second encoder relock nothing asked for.
// docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
bool VideoSourceAcquisition::armMove(const char *why, uint16_t lines)
{
    sourceInterrupted_ = false;
    logSourceMoved(why, lines, solvedLines_);
    return true;
}

// The solve gated on its own steadiness run over this count, longer than the
// idle one, so the idle run starts satisfied rather than re-earning what has
// just been measured and dipping sourceIsPresent() for the polls it takes.
void VideoSourceAcquisition::holdSolvedSource()
{
    solvedLines_ = sampling_.sourceLines();
    solvedLineRateHz_ = sampling_.lineRateHz();

    // The line period this rate was solved against, to compare later readings
    // with. Taken here rather than on an idle pass because HPERIOD_IF counts
    // against the chip's own 27 MHz: the divider this solve just wrote does not
    // change it, so there is nothing to wait for. 0 where it will not hold
    // still, and rateMoved() takes one later.
    solvedLinePeriod_ = sampling_.settledLinePeriod();

    idle_.settle(solvedLines_);

    // A solve that has just written the divider has not had a line counted
    // through it yet, so the sampling half is asked on the next idle pass
    // rather than assumed here.
    sourceState_ = SourceAcquired;
}

// Whether the count has held long enough to be the source's rather than a
// reading taken through something still settling.
bool VideoSourceAcquisition::countHeld(uint16_t lines)
{
    return idle_.sample(lines);
}

// **THIS MUST NOT USE sampling_.sampleSteady().** That call is the solve's own
// steadiness run, and filling it while the engine is idle leaves the next mode
// change's first poll believing a count from the mode before it.
bool VideoSourceAcquisition::sourceMoved()
{
    // A bypassed output is watched like any other. The line count is a property
    // of the SOURCE and bypass does not change it, so the count the mode change
    // into bypass settled on is the reference a later move is measured against
    // -- and without one there is no event, which is what left a source that
    // moved under bypass with no route back.
    // docs/investigations/leaving-bypass-needs-a-count-the-divider-cannot-give.md
    // Nothing solved AND the output passed through is the one cold state with
    // no way forward: bypass has no raster to solve, and the switch into it can
    // leave nothing behind, so reading a state here would blank the pad and arm
    // a solve straight over the bypass setup.
    const Tv5725::OutputMode *mode = videoPath_.outputMode();
    if (mode == 0 || (solvedLines_ == 0 && outputIsPassedThrough())) {
        sourceInterrupted_ = false;
        return false;
    }

    const uint16_t lines = sampling_.countNow();

    // **A DIFFERENT INPUT IS A DIFFERENT SOURCE, AND NO MEASUREMENT CAN SEE
    // IT.** The sync processor may be watching pins the selection did not move,
    // so the count reads the same either side of the change -- which is what
    // left the held sync arrangement in force on a connector that does not
    // carry it. The selection is the event; the count is not evidence about it.
    // docs/known-issues.md, "The sync arrangement outlives an input change"
    if (selectionMoved()) {
        // Nothing measured through the previous input survives the change, this
        // verdict included: inputTimingsChanged() blanks the pad, and a state
        // left at acquired would be read as one the new source had earned.
        sourceState_ = SourceAbsent;
        return armMove("input", lines);
    }

    // ONE ADVANCE OF THE RUN PER POLL. countHeld() mutates it, so a second
    // caller double-advances it and the steadiness both readers depend on is
    // no longer over consecutive polls.
    const bool plausible = Tv5725::VideoSignal::countIsSource(lines);
    const bool held = countHeld(lines);

    // The horizontal half, and it is not a second steadiness run: the divider
    // is held state the engine chose, so one reading of what the sync processor
    // counts against it is the whole test.
    const uint16_t lineSamples = Tv5725::SyncProcessor::lineSamples();
    const SourceState was = sourceState_;
    sourceState_ = !(plausible && held) ? SourceAbsent
                   : Tv5725::Adc::dividerLatched(lineSamples)
                       ? SourceAcquired
                       : SourceUnlocked;

    // The state changing is worth a line because the fault it exists to name is
    // INTERMITTENT and a poll fast enough to catch it changes what the unit
    // does. This costs no bus traffic the answer did not already need.
    //
    // AND IT IS WHERE THE OUTPUT BLANK BELONGS, half a second ahead of any arm:
    // the arms below have runs to sit through first, and until one fires the
    // panel is showing the previous mode's geometry applied to a source that
    // has left.
    if (sourceState_ != was) {
        logSourceState(sourceState_, lines, lineSamples, Tv5725::Adc::dividerInForce());
        videoPath_.showOutput(sourceState_ == SourceAcquired && !videoPath_.changing());
    }

    // A count no source runs is the wrong sync path's signature -- 97..137 on a
    // 311-line source, measured -- and a mode change is the only thing that
    // re-establishes the sync type, so the state that most needs a re-probe was
    // the one state that could never arm one. It arms ONCE: the count stays
    // wrong until the probe has moved the path.
    if (!plausible) {
        if (!held || unusableCountArmed_)
            return false;
        unusableCountArmed_ = true;

        // The one thing that says the held sync type is wrong. Every other
        // arm below is a source that moved, which says nothing about how it
        // carries sync, so only this one stops trusting the held answer.
        videoPath_.forgetSyncType();
        return armMove("unusable count", lines);
    }

    unusableCountArmed_ = false;

    // A count inside the source bounds that never SETTLES had no arm at all:
    // the unusable-count arm above needs the count out of range, and everything
    // below needs it held. A divider left behind by a mode change makes the
    // sync processor retime against a window sized for the wrong line, so the
    // count wanders 191..292 -- plausible every sample, steady on none -- and
    // nothing rewrites the divider that causes it, because prepareToMeasure()
    // is only reached once this arms.
    //
    // Arming only opens a re-measure. measureSource() still has to find the
    // count steady and the rate repeated before anything is solved.
    // docs/investigations/hperiod-if-railing.md
    if (!held) {
        if (unsettledPasses_ < UnsettledArmPasses)
            ++unsettledPasses_;
        if (unsettledPasses_ < UnsettledArmPasses || unsettledArmed_)
            return false;
        unsettledArmed_ = true;
        return armMove("unsettled count", lines);
    }
    unsettledPasses_ = 0;
    unsettledArmed_ = false;

    // THE FIRST SOLVE HAS NO PREVIOUS COUNT TO DIFFER FROM. Every arm below
    // asks whether the source moved AWAY from solvedLines_, which only a solve
    // writes and only an arm opens -- so an engine that has never solved could
    // raise none of them, and the one other armer is a preset load. A boot
    // whose detection pass is refused then has no route in for the life of the
    // boot, with the recovery ladder cycling over a source the sync processor
    // is counting perfectly.
    //
    // Guarded by the two checks above rather than trusted: the count is already
    // known to be inside the source bounds and to have held still.
    // Every arm below compares against solvedLines_, and 0 differs from every
    // count -- so this answers for all of them rather than falling through to
    // one that would fire for the wrong reason.
    if (solvedLines_ == 0)
        return armMove("first count", lines);

    // A composite-sync source held as separate-sync runs uncoasted: the count
    // loses the lines the vertical pulse occupies -- 308 against 311 -- dithers,
    // and the picture bounces. Every one of those counts is PLAUSIBLE, so
    // neither arm above fires, and nothing else arms a re-probe because the
    // count does not move when a source changes its sync type.
    //
    // The separate-sync answer is what puts the separator out, so the V-active
    // bit read here reports the SOURCE. Measured at two modes with the
    // configuration held still and only the source moving: 1 on separate sync,
    // 0 on composite, every sample, and 668 of 668 over three minutes on a live
    // separate-sync source.
    // docs/investigations/a-sync-type-change-arms-no-probe.md
    if (!Tv5725::SyncMeasurement::isCsync() && !Tv5725::SyncProcessor::vsyncActive()) {
        if (vsyncAbsentPasses_ < VsyncAbsentArmPasses)
            ++vsyncAbsentPasses_;
        if (vsyncAbsentPasses_ >= VsyncAbsentArmPasses && !vsyncAbsentArmed_) {
            vsyncAbsentArmed_ = true;
            videoPath_.forgetSyncType();
            return armMove("no source V sync", lines);
        }
    } else {
        vsyncAbsentPasses_ = 0;
        vsyncAbsentArmed_ = false;
    }

    // The rate and the interrupt each say the source moved where the count
    // cannot: the same number of lines at a different field rate, which is what
    // 320x256 at 50, 55 and 60 all are. Both wait behind the SAME steadiness run
    // rather than firing on arrival, because a source measured mid-transition
    // yields a rate that passes every check and is tens of percent out --
    // measured at 18806 Hz against a real 31440, held, with every register
    // self-consistent.
    const bool interrupted = sourceInterrupted_;
    const bool countMoved = !Tv5725::SteadyRun::agree(lines, solvedLines_);
    if (!interrupted && !countMoved && !rateMoved())
        return false;

    idle_.reset();
    return armMove(interrupted ? "interrupt" : countMoved ? "count" : "rate", lines);
}

// The cheap half asks only whether the line period MOVED, and the expensive half
// says what the rate is. Split that way the unreliable register cannot reach a
// decision: a rail, a wrong-and-steady value or a bias all compare equal to
// themselves, and anything that does move is confirmed by the field rate before
// it arms anything.
bool VideoSourceAcquisition::rateMoved()
{
    // The solve could not settle a reading to compare against, so this is the
    // first one that holds still.
    if (solvedLinePeriod_ == 0) {
        solvedLinePeriod_ = sampling_.settledLinePeriod();
        rateRun_ = 0;
        return false;
    }

    if (solvedLineRateHz_ == 0
        || !sampling_.hasLineRateMoved(solvedLinePeriod_)) {
        rateRun_ = 0;
        return false;
    }

    if (rateRun_ < Tv5725::SourceMeasurement::SteadySamples) {
        ++rateRun_;
        return false;
    }
    rateRun_ = 0;

    // What the rate IS, measured a different way, and asked only here. It costs
    // a vsync spin, which is what the cheap half exists to avoid -- affordable
    // because a corroborated disagreement is rare.
    const float fieldRateHz = Tv5725::TestBusRateMeasurement::sourceFieldRateHz(false);
    if (!Tv5725::VideoSignal::isVideo(solvedLines_, fieldRateHz))
        return false;
    if (Tv5725::VideoSignal::ratesAgree(
            Tv5725::VideoSignal::lineRateFor(solvedLines_, fieldRateHz),
            solvedLineRateHz_, RateCorroborationPerMille)) {
        // The register moved and the rate did not, which is the register being
        // unreliable. Adopt what it reads now, so the same disagreement does
        // not buy another spin every pass.
        solvedLinePeriod_ = sampling_.settledLinePeriod();
        return false;
    }

    // The held rate is what moved, and measureLineRate() rejects a rate that
    // changed at an unchanged count -- so leaving it would refuse the very
    // measurement this armed the solve for.
    sampling_.forgetHeldRate();
    return true;
}

const VideoSourceAcquisition::Report &VideoSourceAcquisition::report() const { return report_; }

bool VideoSourceAcquisition::poll(uint32_t nowMs)
{
    runAdvanced_ = false;
    const Report nothing = {false, false, false};
    report_ = nothing;

    if (mayRun_ != 0 && !mayRun_())
        return false;

    if (!Tv5725::Chip::hasPower())
        return false;

    bool detectionPass = false;
    const bool solved = runPass(nowMs, detectionPass);

    // On the cadence, not per call: loop() polls every time round and the
    // thresholds every reader keys on were tuned against a 20 ms pass.
    runAdvanced_ = detectionPass;
    if (!detectionPass)
        return solved;

    if (sourceState_ == SourceAcquired) {
        unmeasuredPasses_ = 0;
        recoveryPosition_ = 0;
        firstAcquisition_ = false;
        firstAcquisitionTimed_ = false;
        if (acquiredPasses_ < AcquiredPassCeiling)
            ++acquiredPasses_;
    } else {
        acquiredPasses_ = 0;
        unmeasuredPasses_ = (uint16_t)((unmeasuredPasses_ + 1) % SyncRecovery::CycleLength);
        if (firstAcquisition_) {
            if (!firstAcquisitionTimed_) {
                firstAcquisitionTimed_ = true;
                firstAcquisitionMs_ = nowMs;
            } else if (nowMs - firstAcquisitionMs_ >= FirstAcquisitionGraceMs) {
                firstAcquisition_ = false;
            }
        }
        if (!firstAcquisition_)
            recoveryPosition_ =
                (uint16_t)((recoveryPosition_ + 1) % SyncRecovery::CycleLength);
    }

    // Ungated: a sync pad left away is a dark panel, and whether maintenance is
    // wanted says nothing about that. Asked every pass rather than after a
    // source solve, because an OUTPUT change moves the raster too and no source
    // solve follows one -- the source has not moved.
    if (!encoderLooking_ && videoPath_.encoderTimingMoved()) {
        tv5725Log("encoder relook: hold");
        videoPath_.holdOutputSync(true);
        encoderLooking_ = true;
        encoderLookMs_ = nowMs;
    }
    serviceEncoderRelook(nowMs);

    if (maintenanceAllowed_)
        keepSourceComing(nowMs);

    return solved;
}

void VideoSourceAcquisition::serviceEncoderRelook(uint32_t nowMs)
{
    if (!encoderLooking_ || (uint32_t)(nowMs - encoderLookMs_) < EncoderRelookMs)
        return;
    encoderLooking_ = false;
    tv5725Log("encoder relook: release");
    videoPath_.holdOutputSync(false);
}

void VideoSourceAcquisition::keepSourceComing(uint32_t nowMs)
{
    // ONE CLAIMANT PER LATCHED BIT. Reading STATUS_INT_SOG_SW claims it, and
    // the pre-emptive separator adjustment below wants the same event, so it is
    // sampled here and nowhere else and both are handed the answer.
    //
    // **IT DOES NOT COVER A RATE CHANGE AT AN UNCHANGED COUNT, so it is one of
    // two arms for that case and not the one to rely on.** Measured on the
    // bench, 240x352 at 449 lines in both 59.96 Hz and 70.08 Hz -- a line rate
    // of 26923 against 31428 with the count pinned at 448 -- the bit did not
    // set, and rateMoved() is what noticed. The arm reason is chosen by
    // priority, so the console printing `rate` rather than `interrupt` is what
    // says so.
    const bool disturbed = Tv5725::Interrupts::takeSourceDisturbed();
    if (disturbed)
        sourceInterrupted();

    // Not on a component source: it chooses its own separator level and this
    // would walk it off.
    if (!Tv5725::Adc::inputIsComponent()) {
        const Tv5725::SyncOnGreen::Tuning tuning = Tv5725::SyncOnGreen::tune(
            disturbed, sourceIsPresent(), clock_,
            Tv5725::SyncOnGreen::putInForce, acquireSeparatorLevel);
        if (tuning.sourceUnsettled)
            report_.vsyncLockStale = true;
        if (tuning.levelMoved)
            applySyncProcessorDynamic(false);
        if (tuning.phaseStale)
            Tv5725::Adc::forgetPhase();
    }

    if (!sourceIsPresent())
        recoverSource();
    else
        maintainSource();

    serviceChannelSync(nowMs);
}

SyncRecovery::Step VideoSourceAcquisition::recoveryDue() const
{
    return SyncRecovery::stepAt(recoveryPosition_);
}

void VideoSourceAcquisition::restartRecovery()
{
    unmeasuredPasses_ = 0;
    recoveryPosition_ = 0;
    ownVsyncFound_ = false;
}

bool VideoSourceAcquisition::selectionMoved()
{
    const VideoSourceSelection::Id chosenNow = VideoSourceSelection::selected();
    if (chosenNow == selectionSeen_)
        return false;
    selectionSeen_ = chosenNow;
    firstAcquisition_ = true;
    firstAcquisitionTimed_ = false;
    recoveryPosition_ = 0;
    return true;
}

uint16_t VideoSourceAcquisition::acquiredPasses() const { return acquiredPasses_; }

uint16_t VideoSourceAcquisition::unmeasuredPasses() const { return unmeasuredPasses_; }

bool VideoSourceAcquisition::runPass(uint32_t nowMs, bool &detectionPass)
{
    // Asked once a pass whether it is used or not, so the cadence does not
    // stretch over a mode change and fire the moment one lands.
    const bool detection = detectionDue(nowMs);
    detectionPass = detection;

    // The source event, ahead of the engine and never during a change it is
    // still working through. Arming one ends the pass: everything the solve
    // reads is measured after the scan mode is established, which
    // inputTimingsChanged() has only just discarded.
    if (!videoPath_.changingMode() && detection && sourceMoved()) {
        videoPath_.inputTimingsChanged();
        return false;
    }

    // Nothing outstanding that needs the source read again, unless a solve was
    // refused against the reading it had.
    if (!videoPath_.changingMode())
        return videoPath_.solveDeferred() && resolveFromSource();

    // Sync type, then the count, then the scan mode. The order is the whole
    // point: the sync path decides what the sync processor counts, and the scan
    // mode decides the divider the measurement asks for.
    // docs/video-source-acquisition.md
    videoPath_.establishSyncType((uint8_t)VideoSourceSelection::selected());
    videoPath_.prepareToMeasure(sampling_.readSourceLines());

    bool settling = false;
    if (!measureSource(settling)) {
        // The idle pass is the only other writer of this and the solve never
        // reaches it, so without this the answer holds whatever that pass last
        // concluded -- present -- for as long as the source cannot be read.
        // That is precisely when a reader needs to know it cannot.
        if (!settling)
            sourceState_ = SourceAbsent;
        return false;
    }

    // Taken in the same pass as the count and the rate above, so every window
    // the engine solves describes one state of the source.
    videoPath_.sourceMeasured(sampling_.hsync());

    // What the output should do, from the measurement just taken. Pass-through
    // is a statement about what the SOURCE is -- a raster the panel can take
    // straight, at a rate that reaches it -- so every measurement re-answers it
    // and a source that stops qualifying is not left stranded in it.
    if (passSourceThrough()) {
        holdSolvedSource();
        return true;
    }

    if (outputIsPassedThrough()) {
        // Outgrown it, so the output goes back to the resolution chosen. The
        // mode change stays armed: the rate held names the mode pass-through was
        // entered on, so the next pass measures this source through the chip
        // setOutputMode() has just put back.
        videoPath_.setOutputMode(resolution_);
        return false;
    }

    if (videoPath_.solveFromMeasurement() != Tv5725::VideoPath::PollSolved)
        return false;
    holdSolvedSource();
    return true;
}

bool VideoSourceAcquisition::measureSource(bool &settling)
{
    if (!reading(sampling_.measureRate(), settling))
        return false;

    // The clock the rate just measured asks for, installed BEFORE the duty is
    // read: the duty is a ratio of the pulse to the LINE and the line is the
    // divider, so one counted through another mode's is out by the ratio
    // between them. The engine owns the choice because it depends on the output
    // mode; a divider already in force for this rate is left alone, because
    // writing it re-latches the ADC PLL.
    if (!videoPath_.installSampling(Tv5725::VideoPath::SamplingFollowsMeasurement))
        return false;

    return reading(sampling_.measureDuty(), settling);
}

bool VideoSourceAcquisition::reading(Tv5725::SourceMeasurement::MeasurementStatus status,
                                     bool &settling)
{
    switch (status) {
    case Tv5725::SourceMeasurement::Serrations:
        // The coast pair in force is not covering the serrations. Margin over
        // the default rather than a search for the lowest pair that works:
        // which pairs measure a source is not reproducible between runs.
        // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
        Tv5725::SyncProcessor::widenCoast();
        return false;

    case Tv5725::SourceMeasurement::ClockSettling:
        // Not absent. The sampling clock was latched a moment ago and nothing
        // read through it is the source's yet, which is a wait rather than a
        // source that is not there -- and reporting absent here would advance
        // the recovery ladder against a source that is present and fine.
        settling = true;
        return false;

    case Tv5725::SourceMeasurement::Settling:
        // A rate is worth sizing a raster from once it has REPEATED. The
        // cross-check inside bounds the rate against the line count, which
        // catches a settling source off by tens of percent and passes one off
        // by tenths -- and the raster is out by whatever fraction the rate is,
        // for good, because nothing re-solves it.
        settling = true;
        return false;

    case Tv5725::SourceMeasurement::Measured:
        return true;

    default:
        return false;
    }
}

bool VideoSourceAcquisition::mayWriteForSource() const
{
    return Tv5725::Chip::hasPower() && !sourceIsSearching();
}

void VideoSourceAcquisition::placeCoastWindow(bool autoCoast)
{
    // Bypass is excluded because the sync processor's window is the SCALING
    // path's: the channel plays out the source's own timing.
    //
    // Asked of the output mode the engine solved, which every path that changes
    // what the output is doing sets. RgbhvOutput::isScaling() is the same fact
    // held in a flag with several writers, and it reads pass-through on a unit
    // that is scaling -- which stopped this being called at all, so the frame
    // time lock had no coast window to arm against.
    const Tv5725::OutputMode *mode = videoPath_.outputMode();
    const bool passedThrough = mode == NULL || mode->isBypass();

    if (!mayWriteForSource() || passedThrough)
        return;

    Tv5725::SyncProcessor::acquireCoastWindow(autoCoast, sampling_.lineRateHz());
}

void VideoSourceAcquisition::placeClampWindow()
{
    if (!mayWriteForSource())
        return;

    const bool component = Tv5725::Adc::inputIsComponent();
    Tv5725::SyncProcessor::clampManually(!component);

    // A component source on the channel at a 15 kHz line clamps later still:
    // the sync tip it has to clear is longer against that line.
    const uint16_t offset =
        component && Tv5725::VideoRoute::isHdBypassChannel() && sampling_.lowLineRate()
            ? Tv5725::SyncProcessor::ChannelComponentClampOffset : 0;

    const bool csync = Tv5725::SyncMeasurement::isCsync();
    if (!Tv5725::SyncProcessor::acquireClampWindow(
            csync, component,
            Tv5725::SyncProcessor::clampLineFor(csync, sampling_.lineRateHz(),
                                                Tv5725::Adc::dividerInForce()),
            offset))
        return;

    if (Tv5725::VideoRoute::isHdBypassChannel())
        Tv5725::HdBypass::applyBlankLevel(component);

    Tv5725::SyncProcessor::adoptClampPlacement();
}

void VideoSourceAcquisition::applySyncProcessorDynamic(bool hunting)
{
    if (!Tv5725::Chip::hasPower())
        return;

    Tv5725::SyncProcessor::Dynamic source;
    source.searching = sourceIsSearching();
    source.present = sourceIsPresent();
    source.hunting = hunting;
    source.csync = Tv5725::SyncMeasurement::isCsync();
    source.pathSource =
        VideoSourceSelection::isRgbhv(VideoSourceSelection::selected())
        || Tv5725::VideoRoute::isHdBypassChannel();
    source.serrated = sampling_.lowLineRate() && Tv5725::SyncMeasurement::isCsync();

    Tv5725::SyncProcessor::applyDynamic(source);
}

bool VideoSourceAcquisition::sourceHasSerratedSync() const
{
    return sampling_.hasSerratedSync();
}

bool VideoSourceAcquisition::mayChangeInput()
{
    return !VideoSourceSelection::chosen(VideoSourceSelection::selected());
}

void VideoSourceAcquisition::acquireSeparatorLevel()
{
    if (!Tv5725::Chip::hasPower()) {
        Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::DefaultLevel);
        return;
    }

    Tv5725::SyncOnGreen::choose(Tv5725::Adc::inputIsComponent()
                                    ? Tv5725::SyncOnGreen::ComponentLevel
                                    : Tv5725::SyncOnGreen::DefaultLevel);
    Tv5725::SyncOnGreen::acquire(clock_, Tv5725::SyncOnGreen::putInForce);
}

void VideoSourceAcquisition::reacquireSeparator(bool reopen)
{
    Tv5725::SyncOnGreen::reacquire(acquireSeparatorLevel,
                                   Tv5725::SyncOnGreen::putInForce, reopen);
}

bool VideoSourceAcquisition::tryOtherAdcInput()
{
    const uint8_t previousInput = Tv5725::Adc::selectOtherInput();
    delay(40);

    // Counted rather than clocked: the wait is a millisecond a pass, so the
    // count IS the time, and a rung that hangs when nobody supplied a clock is
    // worse than one that waits a little long.
    for (uint16_t waited = 0; waited < OtherInputLockMs; ++waited) {
        if (Tv5725::SyncProcessor::hsyncActive()) {
            tv5725Log("recovery: locked on the other ADC input");
            return true;
        }
        watchdog_();
        delay(1);
    }

    Tv5725::Adc::selectInput(previousInput);
    return false;
}

bool VideoSourceAcquisition::runRecovery(SyncRecovery::Step step, bool modeSettled)
{
    switch (step) {
    case SyncRecovery::None:
        break;

    case SyncRecovery::LiftSogFloor:
        if (modeSettled && sourceHasSerratedSync())
            Tv5725::SyncOnGreen::liftOffFloor(Tv5725::SyncOnGreen::putInForce);
        break;

    case SyncRecovery::CoastWindow:
        Tv5725::SyncProcessor::applyDefaultCoastWindow();
        if (sourceHasSerratedSync())
            Tv5725::SyncProcessor::widenCoastForSerration();
        Tv5725::SyncProcessor::forgetPositions();
        break;

    case SyncRecovery::SyncProcessorDynamic:
        applySyncProcessorDynamic(true);
        break;

    case SyncRecovery::ReleaseCapture:
        if (Tv5725::SyncProcessor::hsyncActive())
            Tv5725::FrameBuffer::releaseCapture();
        break;

    case SyncRecovery::HoldClamp:
        if (Tv5725::Adc::inputIsComponent()) {
            Tv5725::SyncProcessor::holdClamp();
            Tv5725::SyncProcessor::forgetPositions();
        }
        break;

    case SyncRecovery::NudgeModeDetect:
        Tv5725::ModeDetect::nudge();
        break;

    case SyncRecovery::HsyncOverflowProtect:
        if (Tv5725::SyncMeasurement::isCsync())
            Tv5725::SyncProcessor::toggleHsyncOverflowProtect();
        break;

    case SyncRecovery::RestartSamplingClock:
        videoPath_.restartSamplingClock();
        break;

    case SyncRecovery::FullReset:
        Tv5725::SyncProcessor::setHsyncOverflowProtect(false);
        Tv5725::SyncProcessor::applyDefaultCoastWindow();
        Tv5725::SyncProcessor::applyDefaultClampWindow();
        applySyncProcessorDynamic(true);
        Tv5725::ModeDetect::nudge();
        delay(80);
        reacquireSeparator(false);
        Tv5725::SyncProcessor::reset();
        delay(8);
        Tv5725::ModeDetect::reset();
        delay(8);
        break;

    case SyncRecovery::ReprobeSyncType:
        // A V sync arriving is proof of a SOURCE, which is a reason not to move
        // the mux out from under it -- the input toggle's precondition, not the
        // whole ladder's. Restarting the run here capped the ladder at this
        // rung, because a separate-sync source answers yes on every cycle.
        ownVsyncFound_ = !videoPath_.reacquireSyncType();
        if (ownVsyncFound_)
            tv5725Log("recovery: own V sync found, the input stays");
        break;

    case SyncRecovery::ToggleInput:
        if (!ownVsyncFound_ && mayChangeInput())
            return tryOtherAdcInput();
        break;

    case SyncRecovery::ReopenSogSeparator:
        reacquireSeparator(true);
        break;
    }
    return false;
}

void VideoSourceAcquisition::recoverSource()
{
    report_.vsyncLockStale = true;

    // The first pass without a measurement is a dropped reading rather than a
    // source going away.
    if (unmeasuredPasses_ == 1)
        return;

    Tv5725::Adc::forgetPhase();

    const SyncRecovery::Step due = recoveryDue();
    if (due != SyncRecovery::None) {
        char line[64];
        snprintf(line, sizeof(line), "recovery: %s at pass %u",
                 SyncRecovery::nameOf(due), (unsigned)recoveryPosition_);
        tv5725Log(line);
    }

    if (runRecovery(due, true)) {
        restartRecovery();
        tv5725Log("No Signal Out");
    }
}

void VideoSourceAcquisition::maintainSource()
{
    SourceMaintenance::Source run;
    run.acquiredPasses = acquiredPasses_;
    run.unmeasuredPasses = unmeasuredPasses_;
    run.samplingPhaseFound = Tv5725::Adc::phaseFound();

    const SourceMaintenance::Due due = maintenance_.dueAt(run);

    if (due.restoreAfterLongAbsence) {
        Tv5725::Adc::forgetPhase();
        report_.frameTimingMoved = true;
    }

    if (due.forgetPositions)
        Tv5725::SyncProcessor::forgetPositions();

    if (due.syncProcessorDynamic)
        applySyncProcessorDynamic(false);

    if (due.sogLevel) {
        delay(20);
        acquireSeparatorLevel();
    }

    if (due.holdCapture)
        Tv5725::FrameBuffer::releaseCapture();

    if (due.samplingPhase)
        acquireSamplingPhase();

    if (due.acknowledgeSogBad)
        Tv5725::Interrupts::acknowledgeSogBad();

    if (!due.steerDeinterlacer || Tv5725::VideoRoute::isHdBypassChannel())
        return;

    // Measured here rather than held: the deinterlacer steers on the
    // maintenance cadence, and a settled source runs no measure() pass at all.
    const Tv5725::SourceMeasurement::ScanType scan = sampling_.measureScanType();
    const Tv5725::Deinterlacer::Steering steering = Tv5725::Deinterlacer::steer(
        sampling_.verticalPeriod(), scan, Tv5725::FrameBuffer::releaseCapture);

    if (steering.frameTimingMoved) {
        report_.frameTimingMoved = true;
        report_.vsyncLockStale = true;
    }
    if (steering.outputRateSettled) {
        delay(10);
        report_.outputRateSettled = true;
    }
}

void VideoSourceAcquisition::serviceChannelSync(uint32_t nowMs)
{
    // STATUS_INT_SOG_BAD latches, so it reports NOW only for a reader that
    // clears it -- and two readers want that: the polarity step below and the
    // auto-gain gate above this layer. The polarity step's own gate is whether
    // the CHANNEL is in circuit, because these are the pulses it emits.
    //
    // **THE SYNC TYPE HAS ONE OWNER, AND STATUS_INT_SOG_BAD IS NOT EVIDENCE
    // ABOUT IT.** A second route to csync used to sit here, flipping the type
    // after four runs with that bit set. It only ever ran with the type ALREADY
    // separate -- where the separator is out of the sync path and the bit
    // reports a comparator with nothing to slice, so it is set permanently.
    // docs/sync-type-selection.md
    if (channelSyncServicedEver_ && nowMs - channelSyncServicedMs_ <= ChannelSyncIntervalMs)
        return;

    channelSyncServicedEver_ = true;
    channelSyncServicedMs_ = nowMs;

    if (Tv5725::VideoRoute::isHdBypassChannel()
        && GBS::STATUS_INT_SOG_BAD::read() == 0) {
        Tv5725::HdBypass::applyChannelSyncEdges(Tv5725::HdBypass::readSourceSyncEdges());
        delay(100);
    }

    Tv5725::Interrupts::acknowledgeSogBad();
}
