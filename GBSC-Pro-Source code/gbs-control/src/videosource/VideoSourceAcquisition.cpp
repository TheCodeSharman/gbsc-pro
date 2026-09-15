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
    : sampling_(sampling), videoPath_(videoPath), mayRun_(0),
      passThroughSwitch_(0), maintenanceAllowed_(false),
      channelSyncServicedEver_(false), channelSyncServicedMs_(0), passThroughAllowed_(false), resolution_(0),
      detectedMs_(0),
      detectedEver_(false), solvedLines_(0), solvedLineRateHz_(0),
      idle_(Tv5725::SourceMeasurement::SteadySamples),
      unusableCountArmed_(false), sourceState_(SourceAbsent),
      candidateRateHz_(0), rateRun_(0), sourceInterrupted_(false),
      unsettledPasses_(0), unsettledArmed_(false),
      unmeasuredPasses_(0), acquiredPasses_(0), runAdvanced_(false) {}

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

void VideoSourceAcquisition::allowMaintenance(bool allowed) { maintenanceAllowed_ = allowed; }

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
    if (!Tv5725::SourceMeasurement::dividerLatched(
            Tv5725::SourceMeasurement::measureLineSamples(),
            Tv5725::Adc::PLLAD_MD::read(), LatchSamples))
        return false;

    // What the ADC is RUNNING, not what was asked for: the request is
    // OversampleAsClockAllows on every source and the crossover row decides
    // what that becomes.
    const uint8_t oversample = Tv5725::Adc::oversampleInForce();

    const bool found = Tv5725::Adc::acquirePhase(
        oversample, Tv5725::SyncOnGreen::level() > Tv5725::SyncOnGreen::StarvedLevel,
        Tv5725::SourceMeasurement::measureLineSamples,
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
    // The same reference the poll pass takes, and for the same reason: a window
    // solved for a taller mode strands the block the rate is timed off, and a
    // count taken through the previous mode's divider is not the source's.
    sampling_.applyReferenceSampling();

    if (sampling_.measure() != Tv5725::SourceMeasurement::Measured)
        return videoPath_.deferSolve();

    videoPath_.sourceMeasured(sampling_.hsync());
    return videoPath_.resolve();
}

bool VideoSourceAcquisition::outputIsPassedThrough() const
{
    const Tv5725::OutputMode *mode = videoPath_.outputMode();
    return mode != 0 && mode->isBypass();
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
        passThroughSwitch_();
    }

    videoPath_.setOutputMode(&Tv5725::ModeBypass);
    return true;
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
               Tv5725::SourceMeasurement::measureSourceLines())
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

// The solve gated on its own steadiness run over this count, longer than the
// idle one, so the idle run starts satisfied rather than re-earning what has
// just been measured and dipping sourceIsPresent() for the polls it takes.
void VideoSourceAcquisition::holdSolvedSource()
{
    solvedLines_ = sampling_.sourceLines();
    solvedLineRateHz_ = sampling_.lineRateHz();

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
    const Tv5725::OutputMode *mode = videoPath_.outputMode();
    if (mode == 0 || solvedLines_ == 0) {
        sourceInterrupted_ = false;
        return false;
    }

    const uint16_t lines = Tv5725::SourceMeasurement::measureSourceLines();

    // ONE ADVANCE OF THE RUN PER POLL. countHeld() mutates it, so a second
    // caller double-advances it and the steadiness both readers depend on is
    // no longer over consecutive polls.
    const bool plausible = Tv5725::VideoSignal::countIsSource(lines);
    const bool held = countHeld(lines);

    // The horizontal half, and it is not a second steadiness run: the divider
    // is held state the engine chose, so one reading of what the sync processor
    // counts against it is the whole test.
    const uint16_t lineSamples = Tv5725::SourceMeasurement::measureLineSamples();
    const SourceState was = sourceState_;
    sourceState_ = !(plausible && held) ? SourceAbsent
                   : Tv5725::SourceMeasurement::dividerLatched(lineSamples,
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
        logSourceMoved("unsettled count", lines, solvedLines_);
        return true;
    }
    unsettledPasses_ = 0;
    unsettledArmed_ = false;

    // The rate and the interrupt each say the source moved where the count
    // cannot: the same number of lines at a different field rate, which is what
    // 320x256 at 50, 55 and 60 all are. Both wait behind the SAME steadiness run
    // rather than firing on arrival, because a source measured mid-transition
    // yields a rate that passes every check and is tens of percent out --
    // measured at 18806 Hz against a real 31440, held, with every register
    // self-consistent.
    const bool interrupted = sourceInterrupted_;
    sourceInterrupted_ = false;
    const bool countMoved = !Tv5725::SteadyRun::agree(lines, solvedLines_);
    if (!interrupted && !countMoved && !rateMoved())
        return false;

    logSourceMoved(interrupted ? "interrupt" : countMoved ? "count" : "rate",
                   lines, solvedLines_);
    idle_.reset();
    return true;
}

bool VideoSourceAcquisition::rateMoved()
{
    const uint32_t rate = Tv5725::SourceMeasurement::measureLineRateFromHPeriod(solvedLines_);
    if (rate == 0 || solvedLineRateHz_ == 0
        || Tv5725::VideoSignal::ratesAgree(rate, solvedLineRateHz_)) {
        candidateRateHz_ = 0;
        rateRun_ = 0;
        return false;
    }

    if (candidateRateHz_ == 0
        || !Tv5725::VideoSignal::ratesAgree(rate, candidateRateHz_)) {
        candidateRateHz_ = rate;
        rateRun_ = 1;
        return false;
    }
    if (rateRun_ < Tv5725::SourceMeasurement::SteadySamples) {
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
    const float fieldRateHz = Tv5725::TestBusRateMeasurement::sourceFieldRateHz(false);
    if (!Tv5725::VideoSignal::isVideo(solvedLines_, fieldRateHz))
        return false;
    if (!Tv5725::VideoSignal::ratesAgree(
            rate, Tv5725::VideoSignal::lineRateFor(solvedLines_, fieldRateHz)))
        return false;

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
        if (acquiredPasses_ < AcquiredPassCeiling)
            ++acquiredPasses_;
    } else {
        acquiredPasses_ = 0;
        unmeasuredPasses_ = (uint16_t)((unmeasuredPasses_ + 1) % SyncRecovery::CycleLength);
    }

    if (maintenanceAllowed_)
        keepSourceComing(nowMs);

    return solved;
}

void VideoSourceAcquisition::keepSourceComing(uint32_t nowMs)
{
    // ONE CLAIMANT PER LATCHED BIT. Reading STATUS_INT_SOG_SW claims it, and
    // the pre-emptive separator adjustment below wants the same event, so it is
    // sampled here and nowhere else and both are handed the answer. A source
    // that returns at the SAME line count and a different field rate is
    // invisible to VideoPath::sourceMoved(), which has only the count to go on;
    // the chip latches the disturbance instead, and without it a wrong rate
    // solved against a correct count survives indefinitely.
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
    return SyncRecovery::stepAt(unmeasuredPasses_);
}

void VideoSourceAcquisition::restartRecovery() { unmeasuredPasses_ = 0; }

uint16_t VideoSourceAcquisition::acquiredPasses() const { return acquiredPasses_; }

uint16_t VideoSourceAcquisition::unmeasuredPasses() const { return unmeasuredPasses_; }

bool VideoSourceAcquisition::runPass(uint32_t nowMs, bool &detectionPass)
{
    // Asked once a pass whether it is used or not, so the cadence does not
    // stretch over a mode change and fire the moment one lands.
    const bool detection = detectionDue(nowMs);
    detectionPass = detection;

    // The source event, ahead of the engine and never during a change it is
    // still working through. Arming one ends the pass: the solve wants a
    // measurement taken after the reference sampling clock is in force, which
    // inputTimingsChanged() has only just written.
    if (!videoPath_.changingMode() && detection && sourceMoved()) {
        videoPath_.inputTimingsChanged();
        return false;
    }

    // Nothing outstanding that needs the source read again, unless a solve was
    // refused against the reading it had.
    if (!videoPath_.changingMode())
        return videoPath_.solveDeferred() && resolveFromSource();

    // Sync type, then the count, then the scan mode and the sampling clock. The
    // order is the whole point: the sync path decides what the sync processor
    // counts, and the scan mode decides the clock every later reading is taken
    // against. docs/video-source-acquisition.md
    videoPath_.establishSyncType();
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
    switch (sampling_.measure()) {
    case Tv5725::SourceMeasurement::Serrations:
        // The coast pair in force is not covering the serrations. Margin over
        // the default rather than a search for the lowest pair that works:
        // which pairs measure a source is not reproducible between runs.
        // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
        Tv5725::SyncProcessor::widenCoast();
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
    const bool passedThroughRgbhv =
        VideoSourceSelection::isRgbhv(VideoSourceSelection::selected())
        && !Tv5725::RgbhvOutput::isScaling();

    if (!mayWriteForSource() || passedThroughRgbhv)
        return;

    Tv5725::SyncProcessor::acquireCoastWindow(autoCoast);
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

    if (!Tv5725::SyncProcessor::acquireClampWindow(
            Tv5725::SyncMeasurement::isCsync(), component, offset))
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
    return sampling_.lowLineRate() && Tv5725::SyncMeasurement::isCsync();
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
        // A V sync arriving is proof of a source, so the run restarts rather
        // than escalating on to the input toggle.
        if (!videoPath_.reacquireSyncType()) {
            tv5725Log("recovery: own V sync found, the run restarts");
            return true;
        }
        break;

    case SyncRecovery::ToggleInput:
        if (mayChangeInput())
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

    if (runRecovery(recoveryDue(), true)) {
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
    const uint16_t verticalPeriod = Tv5725::SourceMeasurement::measureVerticalPeriod();
    if (verticalPeriod == 0)
        return;
    const Tv5725::Deinterlacer::Steering steering = Tv5725::Deinterlacer::steer(
        verticalPeriod, sampling_.scanType(verticalPeriod),
        Tv5725::FrameBuffer::releaseCapture);

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
