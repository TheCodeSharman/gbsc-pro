#include "VideoSourceAcquisition.h"

#include <math.h>
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
#include "../tv5725/SourceKey.h"
#include "../tv5725/EqualisationMeasurement.h"
#include "../tv5725/SyncMeasurement.h"
#include "../tv5725/SyncOnGreen.h"
#include "../tv5725/VideoRoute.h"
#include "../tv5725/VideoSignal.h"
#include "VideoSourceSelection.h"
#include "../tv5725/TestBus.h"
#include "../tv5725/SyncProcessor.h"
#include "../tv5725/Tv5725Log.h"

VideoSourceAcquisition::VideoSourceAcquisition(Tv5725::SourceMeasurement &sampling,
                                   Tv5725::VideoPath &videoPath)
    : sampling_(sampling), videoPath_(videoPath), maintenanceAllowed_(false),
      channelSyncServicedEver_(false), channelSyncServicedMs_(0),
      syncAwaySeen_(false), syncAwayMs_(0), phaseSearched_(false),
      mayRun_(0), passThroughSwitch_(0), passThroughAllowed_(false), resolution_(0),
      detectedMs_(0),
      detectedEver_(false), solvedLines_(0), solvedFrame_(0), solvedLineRateHz_(0),
      idle_(Tv5725::SourceMeasurement::SteadySamples,
            Tv5725::SourceMeasurement::CountPairSpread),
      unusableCountArmed_(false), dynamicApplied_(false),
      dynamicForSearch_(false), dynamicForCsync_(false),
      sourceMeasured_(false),
      sourceState_(SourceAbsent),
      unlockedPasses_(0), contradictedPasses_(0),
      solvedLinePeriod_(0), rateRun_(0), recheckPasses_(0),
      sourceInterrupted_(false), unacquiredPasses_(0), absentPasses_(0),
      unlatchedPasses_(0),
      unsettledPasses_(0), unsettledArmed_(false),
      passThroughFieldRateHz_(0.0f),
      unmeasuredPasses_(0), referenceRetryPasses_(0),
      acquiredPasses_(0),
      selectionSeen_(VideoSourceSelection::None), selectionEstablished_(false),
      runAdvanced_(false), phaseRateHz_(0), phaseSettled_(false) {}

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

bool VideoSourceAcquisition::samplingPhaseFound() const
{
    return Tv5725::Adc::phaseFound() && phaseRateHz_ == sampling_.lineRateHz()
           && phaseSettled_;
}

bool VideoSourceAcquisition::acquireSamplingPhase()
{
    if (!Tv5725::Adc::dividerLatched(Tv5725::SyncProcessor::lineSamples(),
                                     LatchSamples))
        return false;

    // What the ADC is RUNNING, not what was asked for: the request is
    // OversampleAsClockAllows on every source and the crossover row decides
    // what that becomes.
    const uint8_t oversample = Tv5725::Adc::oversampleInForce();

    // ALWAYS SWEPT. The separator's level does not gate this: the scores come
    // from the sync processor's count, which on a separate-sync source arrives
    // on the dedicated sync line, and the separator reaches the sync processor
    // only with SP_SOG_MODE 1 -- so there the level is inert and holds whatever
    // the previous source left. The search reports its own bad data through
    // SweepGoodEnough and samplingClockSuspect() besides.
    // ../../../docs/known-issues.md
    const bool found = Tv5725::Adc::acquirePhase(
        oversample, true, Tv5725::SyncProcessor::lineSamples, watchdog_);

    // Recorded so a later pass can tell whether the phase still belongs to the
    // sampling this source is read through.
    if (found) {
        phaseRateHz_ = sampling_.lineRateHz();
        phaseSettled_ = acquiredPasses_ >= PhaseSettledPasses;
    }

    char line[48];
    snprintf(line, sizeof(line), "sampling phase: %s, oversample %u",
             found ? "chosen" : "no clean window", (unsigned)oversample);
    tv5725Log(line);

    // SAID, NOT REPAIRED. No phase can steady a clock that is not running the
    // divider, so another search is not the answer -- and neither is the group
    // and the restart, measured leaving the lock share at 0.5% on the one cause
    // that has been traced. That cause is removed where it arises rather than
    // recovered from here, a stranded sync reference being nothing the phase
    // layer can see.
    // ../../../docs/investigations/leaving-pass-through-strands-the-sync-reference.md
    if (!found && Tv5725::Adc::samplingClockSuspect())
        tv5725Log("sampling clock unlocked: clean phases scattered, not a window");

    return found;
}

// THE PERMISSION MOVES WHILE THE SOURCE STANDS STILL. The route is re-answered
// only on a pass that re-resolves the source, so without an arm here the
// picture stays handed over -- or stays scaled -- until the source next moves:
// the menu's Pass Through row read ON however often it was pressed, and
// choosing a resolution appeared to do nothing.
void VideoSourceAcquisition::allowPassThrough(bool allowed)
{
    if (passThroughAllowed_ == allowed)
        return;
    passThroughAllowed_ = allowed;
    videoPath_.inputTimingsChanged();
}

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

    return videoPath_.setOutputMode(carriedResolution());
}

const Tv5725::OutputMode *VideoSourceAcquisition::carriedResolution() const
{
    return Tv5725::OutputMode::transmittableFor(resolution_,
                                                sampling_.fieldRateHz());
}

const Tv5725::OutputMode *VideoSourceAcquisition::resolutionToMoveTo() const
{
    const Tv5725::OutputMode *carried = carriedResolution();
    return carried == videoPath_.outputMode() ? 0 : carried;
}

bool VideoSourceAcquisition::resolveFromSource(uint32_t nowMs)
{
    // The same preparation the poll pass makes, and for the same reason: a
    // window solved for a taller mode strands the block the rate is timed off.
    videoPath_.prepareToMeasure(sampling_.readSourceLines());

    bool settling = false;
    if (!measureSource(nowMs, settling))
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
        videoPath_.setOutputMode(carriedResolution());
        videoPath_.inputTimingsChanged();
        return false;
    }

    // Re-answered against the rate just measured, the same as the route above.
    const Tv5725::OutputMode *moveTo = resolutionToMoveTo();
    if (moveTo != 0)
        return videoPath_.setOutputMode(moveTo);

    return videoPath_.resolve();
}

bool VideoSourceAcquisition::outputIsPassedThrough() const
{
    return Tv5725::VideoRoute::isHdBypassChannel();
}

// A MEASUREMENT THAT DID NOT COMPLETE DECIDES NOTHING -- except about the
// route, and only here. Everywhere else a failed reading is a dropped sample
// and acting on one would cost a good picture; in pass-through it is also what
// a real mode change looks like, because the count is taken through a divider
// sized for the source that has already gone. The field rate is the one
// reading that cannot say so falsely, so it is the only thing asked.
//
// The output alone goes back. The mode change that brought the pass here is
// already armed, and the rate held still names the mode pass-through was
// entered on, so the next pass measures this source through the chip
// setOutputMode() has just put back.
bool VideoSourceAcquisition::gaveBackPassThrough()
{
    if (!passThroughSourceMoved())
        return false;

    videoPath_.setOutputMode(carriedResolution());
    return true;
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
        // mode's divider, leaving the ADC PLL unlocked on the arriving source.
        // ../../../docs/video-source-acquisition.md
        //
        // It holds the divider already in force, which is what stops it
        // running on a source standing still.
        resizePassThrough();
    }

    passThroughFieldRateHz_ = sampling_.fieldRateHz();
    videoPath_.setOutputMode(&Tv5725::ModeBypass);
    return true;
}

// WHETHER THE SOURCE UNDER PASS-THROUGH IS STILL THE ONE THE CHANNEL WAS SIZED
// FOR, asked of the field rate because it is the only measurement the route
// cannot poison: it is timed off vsync edges on DEBUG_IN_PIN, where the count
// is taken in ADC clocks through a divider sized for the source that has just
// left. So a count that disagrees says nothing here and a rate that disagrees
// says the source moved.
//
// No reading is not a disagreement. A source that did not pulse reports 0, and
// dropping a handed-over picture on one missed sample is what the rule above
// exists to prevent.
bool VideoSourceAcquisition::passThroughSourceMoved() const
{
    if (!outputIsPassedThrough() || passThroughFieldRateHz_ <= 0.0f)
        return false;

    const float measured = sampling_.fieldRateHz();
    if (measured <= 0.0f)
        return false;

    return !Tv5725::VideoSignal::ratesAgree(
        (uint32_t)lrintf(measured * Tv5725::RateStepsPerHz),
        (uint32_t)lrintf(passThroughFieldRateHz_ * Tv5725::RateStepsPerHz),
        Tv5725::SourceIdentityPerThousand);
}

// The clock the rate just measured asks for, installed BEFORE the duty is read:
// the duty is a ratio of the pulse to the LINE and the line is the divider, so
// one counted through another mode's is out by the ratio between them.
//
// ONE STEP FOR BOTH ROUTES. The channel's clock is HdBypass's rather than a
// solve's, so installSampling() returns without writing it -- and left to the
// pass that follows the duty, the rule above is not kept on that route at all.
bool VideoSourceAcquisition::installSamplingForMeasurement()
{
    if (outputIsPassedThrough()) {
        installPassThroughClock();
        return true;
    }

    return videoPath_.installSampling(
        Tv5725::VideoPath::SamplingFollowsMeasurement);
}

// A CLOCK ALREADY IN FORCE AND LATCHED IS LEFT ALONE, the same rule
// installSampling() holds, and for the same reason: applying it re-latches the
// ADC PLL. Here that closes a loop, because the field rate this route judges a
// source by is timed off a pin the sync processor drives -- so an install taken
// on a source standing still moves the next reading, which reads as a source
// that moved, which takes another install.
//
// **AND A CLOCK THE PLL IS NOT RUNNING IS NOT IN FORCE.** The register answers
// for what was written and the sync processor's line total for what the ADC is
// clocking, so on the register alone a reference clock installed by the
// rate-failure retry is never replaced: takeDuty() refuses a reading through an
// unlatched clock, and every pass after it is gated on the duty.
// docs/known-issues.md
void VideoSourceAcquisition::installPassThroughClock()
{
    const uint32_t lineRateHz = sampling_.lineRateHz();
    const uint16_t divider = Tv5725::HdBypass::dividerFor(lineRateHz);
    if (divider == 0)
        return;
    if (divider == Tv5725::Adc::dividerInForce()
        && videoPath_.samplingClockLatched())
        return;

    Tv5725::HdBypass::applyPassThroughSampling(divider, lineRateHz,
                                               sampling_.hsync());

    // The measurement has chosen, so no reference is in force any longer -- the
    // same note installSampling() makes for the scaling path, and what stops the
    // retry installing the next reference over the clock just chosen.
    Tv5725::Adc::forgetReferenceClock();
    sampling_.samplingClockLatched();
}

// The channel's raster, from the measurement just taken, and the one place a
// source that moved inside pass-through is re-sized: the switch sizes the
// channel on entry and cannot run again while the route is already where it
// belongs.
//
// Unguarded, because it writes no part of the ADC PLL group -- so nothing is
// re-latched by applying it, and a raster that must follow a changed count
// cannot be suppressed by a divider that happens not to have moved.
void VideoSourceAcquisition::resizePassThrough()
{
    Tv5725::HdBypass::applyChannelRaster(videoPath_.sourceTiming(),
                                         sampling_.sourceLines());
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
               Tv5725::SyncProcessor::frameLines())
           && !sourceIsPresent();
}

void VideoSourceAcquisition::configureAfterLoad(void (*forgetFrameLock)(),
                                                const LoadState &state)
{
    videoPath_.configureSyncPath();

    // THE SEPARATOR LEVEL IS NOT HANDED IN HERE. It is a measurement of the
    // source, established by searching for it, and a constant written over that
    // undoes the search -- the same second owner the channel's entry had.
    if (state.scalingRgbhv)
        Tv5725::Adc::choosePhaseAdc(Tv5725::Adc::MidField);

    if (!Tv5725::VideoRoute::isHdBypassChannel()
        && !videoPath_.scalingRgbhvInForce()) {
        applySyncProcessorDynamic();
    }

    Tv5725::SyncProcessor::holdClamp();

    videoPath_.putSeparatorAndPhasesInForce();
    *state.sourceDisconnected = false;
    Tv5725::Chip::holdPower(true);

    if (Tv5725::SyncMeasurement::isCsync()) {
        if (Tv5725::TestBus::readHigh() == 0) {
            delay(4);
            if (Tv5725::TestBus::readHigh() == 0) {
                acquireSeparatorLevel();
                delay(4);
            }
        }
    }

    videoPath_.armSolveForSource();

    forgetFrameLock();

    placeCoastWindow(0);
    placeClampWindow();

    videoPath_.restartAndLatch();

    // THE CLAMP IS THE SOURCE'S BLACK LEVEL, so the route the video takes after
    // it is no reason to leave it held. prepareForDetection() holds it for the
    // count and this is where it is let go, above the channel's return: the
    // return is there for the VDS solve that is not coming, and the sketch's own
    // recovery is gated on the window not yet being placed, which the sequence
    // above has already placed.
    placeClampWindow();
    Tv5725::SyncProcessor::releaseClampIfPlaced();

    applySyncProcessorDynamic();

    if (Tv5725::VideoRoute::isHdBypassChannel()) {
        Tv5725::Interrupts::acknowledgeAll();

        // Video routes around the VDS here, so the mode change armed above has
        // no solve coming and the freeze it took would never be released.
        videoPath_.setOutputMode(&Tv5725::ModeBypass);
        return;
    }

    if (!state.syncWatcherEnabled) {
        Tv5725::SyncProcessor::releaseClamp();
    }

    Tv5725::Interrupts::acknowledgeAll();

    // Pass-through is not decided here. It is a statement about the measured
    // source, and this runs at the end of a load with whatever the PREVIOUS
    // source measured still held -- passSourceThrough() owns it and re-answers
    // it from each measurement. docs/video-source-acquisition.md
    *state.applyPresetDoneStage = 1;

    // Capture stays frozen: it is released by the poll() that lands the
    // windows, once the source has settled into the new mode. Releasing it here
    // shows the previous mode's geometry against the new source until then.
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

// WHICH KIND OF CHANGE, because the two need different things of the pass that
// follows. A MUX change means a different source is arriving, so nothing held
// about the previous one is evidence; a TIMINGS change means the same source
// moved, and the sync type it was measured on still stands unless the probe
// says otherwise. Only "input" is the mux.
static void logSourceMoved(const char *why, bool mux, uint16_t lines,
                           uint16_t solved)
{
    char line[88];
    snprintf(line, sizeof(line),
             "measure: start, %s change (%s), %u lines, solved %u",
             mux ? "MUX" : "TIMINGS", why, (unsigned)lines, (unsigned)solved);
    tv5725Log(line);
}

// THE ARM SPENDS THE LATCH. The re-measure an arm opens answers whatever
// disturbed the sync separator, so a disturbance taken before an arm raised for
// another reason is not news once that measurement lands. Left set it re-arms
// the moment the solve completes, on the count that solve has just measured,
// and the output is blanked for a second encoder relock nothing asked for.
// docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
bool VideoSourceAcquisition::armMove(const char *why, uint16_t lines, bool mux)
{
    sourceInterrupted_ = false;
    logSourceMoved(why, mux, lines, solvedLines_);
    return true;
}

// The solve gated on its own steadiness run over this count, longer than the
// idle one, so the idle run starts satisfied rather than re-earning what has
// just been measured and dipping sourceIsPresent() for the polls it takes.
void VideoSourceAcquisition::holdSolvedSource()
{
    solvedLines_ = sampling_.sourceLines();
    solvedLineRateHz_ = sampling_.lineRateHz();
    solvedFrame_ = sampling_.verticalPeriod();

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

    // AFTER the state, because the placement refuses to write for a searching
    // source and this is what stops it searching. A search leaves the window
    // at DefaultCoastStop, which coasts over the wrong part of the line and
    // counts short -- so a solve that does not re-place it measures a source
    // that looks like it moved, and searches again.
    // ../../../../docs/investigations/the-coast-window-is-placed-once-the-rate-is-known.md
    placeCoastWindow(false);
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

    const uint16_t lines = sampling_.readSourceLines();

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
        return armMove("input", lines, true);
    }

    // A COUNT THE INPUT FORMATTER'S OWN FRAME CONTRADICTS IS NOT A READING OF
    // THIS SOURCE, so the pass is DROPPED rather than judged: it must not
    // advance the steadiness run, move the presence verdict or arm anything.
    // The sync processor has lost or gained a vertical pulse, and on a
    // composite source it does that in runs longer than any steadiness run
    // reaches.
    // ../../../docs/investigations/the-composite-sync-count-re-arms-its-own-solve.md
    // BUT NOT FOR EVER. A source that changed mode under a divider the
    // processor cannot count it through contradicts the formatter on every
    // pass, and dropping every one of them left the previous solve standing,
    // acquired and silent, with no arm reachable. A run far longer than any
    // lost pulse produces is the processor not counting this source, which is
    // what a re-measure is for. It re-arms every run rather than once.
    //
    // AND ONLY WHERE THE RE-MEASURE HAS SOMEWHERE TO GO. A count equal to the
    // one already solved is what the re-measure would return, so arming on it
    // repeats for ever -- an interlaced source whose formatter counts a frame
    // the processor counts a field of reconciles by no factor, and cycles.
    // There the raw count stands, which is what refusing the reading was
    // always meant to leave. ../../../docs/known-issues.md
    //
    // UNLESS THE FORMATTER'S FRAME MOVED, which says the count is equal to the
    // solved one because it STOPPED rather than because the source stood
    // still. A processor that stops counting holds its last value exactly, so
    // it equals the solve by construction and every arm here reads it as
    // nothing happening. The source that cycles has the other shape: its
    // formatter disagrees from the solve onward and never moves.
    // ../../../docs/investigations/the-coast-window-blocks-a-source-it-has-lost.md
    if (sampling_.countContradicted(lines)) {
        if (lines == solvedLines_ && !sampling_.frameMovedFrom(solvedFrame_)) {
            contradictedPasses_ = 0;
            return false;
        }
        if (++contradictedPasses_ < ContradictedArmPasses)
            return false;
        contradictedPasses_ = 0;
        return armMove("contradicted count", lines, false);
    }
    contradictedPasses_ = 0;

    // ONE ADVANCE OF THE RUN PER POLL. countHeld() mutates it, so a second
    // caller double-advances it and the steadiness both readers depend on is
    // no longer over consecutive polls.
    const bool plausible = Tv5725::VideoSignal::countIsSource(lines);
    const bool held = countHeld(lines);
    const bool readsSource = plausible && held;

    if (readsSource)
        absentPasses_ = 0;
    else if (absentPasses_ < AbsentPasses)
        ++absentPasses_;

    // The horizontal half, and BOTH HALVES FORGIVE A DROPPED READING FOR THE
    // SAME RUN: a processor that loses the line for a moment reads exactly like
    // one clocking a divider the engine did not choose, and the verdict is what
    // the acquired run -- and so every maintenance threshold keyed on it --
    // starts over from.
    // ../../../docs/investigations/a-dropped-line-count-is-not-an-unlocked-source.md
    //
    // A reading that cannot read the source at all moves nothing until the run
    // is spent: the verdict HOLDS what it had, because one such reading is a
    // miscount rather than a source leaving. AbsentPasses.
    const uint16_t lineSamples = Tv5725::SyncProcessor::lineSamples();
    const bool latched = Tv5725::Adc::dividerLatched(lineSamples);
    if (latched)
        unlatchedPasses_ = 0;
    else if (unlatchedPasses_ < AbsentPasses)
        ++unlatchedPasses_;

    const SourceState was = sourceState_;
    if (readsSource) {
        if (latched)
            sourceState_ = SourceAcquired;
        else if (unlatchedPasses_ >= AbsentPasses)
            sourceState_ = SourceUnlocked;
    } else if (absentPasses_ >= AbsentPasses) {
        sourceState_ = SourceAbsent;
    }

    // The state changing is worth a line because the fault it exists to name is
    // INTERMITTENT and a poll fast enough to catch it changes what the unit
    // does. This costs no bus traffic the answer did not already need.
    if (sourceState_ != was)
        logSourceState(sourceState_, lines, lineSamples, Tv5725::Adc::dividerInForce());

    // AND THE OUTPUT BLANK BELONGS HERE, ahead of any arm: the arms below have
    // runs to sit through first, and until one fires the panel is showing the
    // previous mode's geometry applied to a source that has left. It waits
    // OutputBlankPasses, because a state that comes back sooner than the pad
    // can is not a source that left. Presenting is not decided here: acquired
    // is the first step of a setup rather than the last, and
    // presentWhenSettled() waits for the rest.
    if (sourceState_ == SourceAcquired) {
        unacquiredPasses_ = 0;
    } else if (unacquiredPasses_ < OutputBlankPasses) {
        ++unacquiredPasses_;
        if (unacquiredPasses_ >= OutputBlankPasses)
            videoPath_.showOutput(false);
    }

    // A count no source runs arms a re-measure and NOTHING ELSE. It used to
    // discard the held sync type as well, on the reading that such a count is
    // the wrong path's signature -- but it is equally the signature of no
    // source, the same 97 either way, and steadiness does not separate them.
    // ../../../docs/investigations/an-absent-source-is-not-a-sync-type.md
    if (!plausible) {
        if (!held || unusableCountArmed_)
            return false;
        unusableCountArmed_ = true;
        return armMove("unusable count", lines, false);
    }

    unusableCountArmed_ = false;

    // A count inside the source bounds that never SETTLES had no arm at all:
    // the unusable-count arm above needs the count out of range, and everything
    // below needs it held. A divider left behind by a mode change makes the
    // sync processor retime against a window sized for the wrong line, so the
    // count wanders -- plausible every sample, steady on none -- and
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
        return armMove("unsettled count", lines, false);
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
        return armMove("first count", lines, false);

    // AN UNLOCKED SOURCE IS THE ENGINE'S OWN DIVIDER RATHER THAN THE SOURCE
    // MOVING, so no arm below can see it: the count is the one the last solve
    // measured and the rate has not changed. Only a solve rewrites the divider,
    // so without this the engine names the state and then waits on it -- a
    // selection edge installs the reference divider, a detection that returns
    // without solving leaves it in force, and the source never comes back.
    //
    // IT RE-ARMS EVERY RUN RATHER THAN ONCE, because one attempt that does not
    // take is the difference between slow recovery and none.
    if (sourceState_ == SourceUnlocked) {
        if (unlockedPasses_ < UnlockedArmPasses)
            ++unlockedPasses_;
        if (unlockedPasses_ >= UnlockedArmPasses) {
            unlockedPasses_ = 0;
            return armMove("unlocked", lines, false);
        }
    } else {
        unlockedPasses_ = 0;
    }

    // A composite-sync source held as separate-sync runs uncoasted: the count
    // loses the lines the vertical pulse occupies, dithers, and the picture
    // bounces. Every one of those counts is PLAUSIBLE, so
    // neither arm above fires, and nothing else arms a re-probe because the
    // count does not move when a source changes its sync type.

    // The rate says the source moved where the count cannot: the same number of
    // lines at a different field rate, which is what 320x256 at 50, 55 and 60
    // all are. It waits behind a steadiness run rather than firing on arrival,
    // because a source measured mid-transition yields a rate that passes every
    // check and is tens of per cent out, held, with every register
    // self-consistent.
    //
    // The latched disturbance is an event INSIDE that question rather than an
    // arm of its own, because the engine's own writes raise it too.
    // Asked of the run that holds the pair, so the arm and the presence verdict
    // answer the same question: a count the chip miscounts by three alternates
    // without the source moving, and comparing a raw sample against the solve's
    // raw sample armed a re-solve on every crossing.
    // A COUNT THAT IS A WHOLE MULTIPLE OF THE SOLVED ONE AT AN UNCHANGED LINE
    // PERIOD IS A LOST VERTICAL PULSE, NOT A SOURCE THAT MOVED. Doubling the
    // count at an unchanged field rate doubles the line RATE, so a source that
    // really moved there moves the period with it.
    //
    // The corroboration above cannot refuse these: both halves derive from the
    // same separated vertical, so a pulse the separator loses is lost by both
    // and they agree on the doubled frame.
    //
    // IT NARROWS THE COUNT ARM RATHER THAN DROPPING THE PASS, because the
    // arithmetic has no second instrument behind it and must not be the last
    // word. rateMoved()'s recheck still reaches a source that did move, and
    // arms on a field rate measured a different way.
    // ../../../docs/investigations/the-composite-sync-count-re-arms-its-own-solve.md
    const bool pulseLost = Tv5725::VideoSignal::countIsMultipleOf(lines, solvedLines_)
                           && sampling_.linePeriodHolds(solvedLinePeriod_);

    // A restoration narrower than the spread agrees with the count already
    // solved, so no arm below can recover a capture sized before the hold took.
    // ../../../docs/known-issues.md
    const bool restored = sampling_.countRestored() && lines != solvedLines_;

    const bool countMoved = !idle_.agrees(lines, solvedLines_) && !pulseLost;
    if (!restored && !countMoved && !rateMoved())
        return false;

    idle_.reset();
    const char *const why = restored ? "restored count"
                          : countMoved ? "count" : "rate";
    return armMove(why, lines, false);
}

// The cheap half asks only whether the line period MOVED, and the expensive half
// says what the rate is. Split that way the unreliable register cannot reach a
// decision: a rail, a wrong-and-steady value or a bias all compare equal to
// themselves, and anything that does move is confirmed by the field rate before
// it arms anything.
// Every key move must have a solve behind it, or the stored framing swaps on
// drift with nothing recomputing the windows. The arm is this corroboration and
// the key is Tv5725::SourceKey, so the arm has to be at least as sensitive.
static_assert(VideoSourceAcquisition::RateCorroborationPerThousand
                  <= Tv5725::SourceIdentityPerThousand,
              "a rate change that moves the key must also arm a solve");

bool VideoSourceAcquisition::rateMoved()
{
    // The solve could not settle a reading to compare against, so this takes
    // the first one that holds still. **IT DOES NOT GATE THE QUESTION**, only
    // the cheap half of it: HPERIOD_IF is the one register that may never hold
    // still, and a source whose period never settles is the one that most needs
    // the other two events below.
    // ../../../../docs/investigations/hperiod-if-railing.md
    if (solvedLinePeriod_ == 0)
        solvedLinePeriod_ = sampling_.settledLinePeriod();

    // The register moving is the cheap half's event. THE RECHECK IS THE OTHER
    // WAY IN, and without it nothing on the board ever asks whether the held
    // rate is still the source's: every arm here detects a CHANGE, so a rate
    // taken mid-change and wrong by a per cent is kept for as long as the
    // source stands still.
    //
    // AND THE LATCHED DISTURBANCE IS THE THIRD, which is the only thing that
    // sees a rate change the line period missed, one count running at two
    // field rates with the register still. It opens the
    // expensive half rather than arming anything, because the engine's own
    // divider, clamp and coast placements switch the separator too and the chip
    // latches that about a second after they land. Read as an arm, a solve
    // re-arms itself for ever on a source standing still.
    // ../../../../docs/investigations/the-reference-clock-is-applied-to-a-working-picture.md
    const bool periodMoved = solvedLinePeriod_ != 0
                             && sampling_.hasLineRateMoved(solvedLinePeriod_);
    if (recheckPasses_ < RateRecheckPasses)
        ++recheckPasses_;
    const bool recheckDue = recheckPasses_ >= RateRecheckPasses;

    if (solvedLineRateHz_ == 0
        || !(periodMoved || recheckDue || sourceInterrupted_)) {
        rateRun_ = 0;
        return false;
    }

    if (rateRun_ < Tv5725::SourceMeasurement::SteadySamples) {
        ++rateRun_;
        return false;
    }
    rateRun_ = 0;
    recheckPasses_ = 0;

    // Spent by the reading that answers it, the way an arm spends it.
    sourceInterrupted_ = false;

    // THE FIRST RECHECK OF A BOOT IS MEASURED BETTER RATHER THAN TRUSTED LESS.
    // A corroboration timed over one pulse cannot reject what it is asked
    // about: one pulse carries a whole source line of edge latency, so the
    // tolerance has to be wider than the error being looked for. Both readings
    // are spanned here instead, which is the tolerance
    // BootRateConfirmPerThousand states -- and a re-solve blanks the output for
    // the encoder relook, so a boot whose rate is right must not pay for it.
    //
    // THE RECHECK AND NOT WHATEVER REACHES HERE FIRST. A line period that
    // twitches early spends the confirmation inside the window it exists to
    // outlast, and what it then corroborates is the same wrong rate.
    const bool confirming = recheckDue && !sampling_.rateConfirmed();

    // What the rate IS, measured a different way, and asked only here. It costs
    // a vsync spin, which is what the cheap half exists to avoid -- affordable
    // because a corroborated disagreement is rare.
    //
    // Spanned while the boot's rate is unconfirmed, because this is the reading
    // that has to be able to refuse it, and the boot's own was spanned too.
    const uint32_t pulses =
        confirming ? Tv5725::SourceMeasurement::UnconfirmedRatePulses : 1;
    const float fieldRateHz =
        Tv5725::TestBusRateMeasurement::sourceFieldRateHz(false, pulses);

    if (confirming)
        sampling_.confirmRate();

    // A FIELD RATE THAT CANNOT BE MEASURED CANNOT REFUSE A LINE PERIOD THAT
    // MOVED, so it must not be the reading that settles it. Both guards ahead
    // of this read the input formatter's frame, which is 0 whenever
    // STATUS_IF_VT_OK is -- and where that bit is clear a count frozen by a
    // coast window the source has left equals the solve exactly, nothing
    // contradicts it, and this is the last instrument. Returning false holds a
    // dead count at acquired for as long as the state lasts.
    //
    // The held rate goes with it: the count has not moved, and
    // measureLineRate() refuses a rate that changed at an unchanged count, so
    // leaving it would refuse the measurement this armed the solve for.
    // ../../../../docs/investigations/the-formatters-frame-is-gated-off-when-the-count-stops.md
    if (!Tv5725::VideoSignal::isVideo(solvedLines_, fieldRateHz)) {
        if (!confirming && !periodMoved)
            return false;
        // Nothing to judge the boot's rate against, so re-solve rather than
        // keep a rate this never managed to check.
        sampling_.forgetHeldRate();
        return true;
    }

    // Two spanned readings are comparable at a tolerance a single pulse cannot
    // support, which is what lets the boot's rate be confirmed where it is
    // right instead of replaced because it could not be told apart.
    const uint16_t within =
        confirming ? BootRateConfirmPerThousand : RateCorroborationPerThousand;
    if (Tv5725::VideoSignal::ratesAgree(
            Tv5725::VideoSignal::lineRateFor(solvedLines_, fieldRateHz),
            solvedLineRateHz_, within)) {
        // The register moved and the rate did not, which is the register being
        // unreliable. Adopt what it reads now, so the same disagreement does
        // not buy another spin every pass.
        solvedLinePeriod_ = sampling_.settledLinePeriod();
        return false;
    }

    // **A DISAGREEMENT IS ASKED AGAIN BEFORE ANYTHING IS ARMED**, whichever
    // reading found it, because an arm blanks the output for the encoder relook.
    // One timing carries a whole source line of edge latency, which is wider
    // than the tolerance this compares at, and spanning DIVIDES that rather
    // than removing it -- so a spanned reading is occasionally out too.
    // ../../../docs/investigations/one-source-takes-many-keys.md
    //
    // The first reading stays in front of it: a span costs that many field
    // periods, and a disagreement is rare enough to pay for a second only once
    // something has been seen.
    const float again = Tv5725::TestBusRateMeasurement::sourceFieldRateHz(
        false, Tv5725::SourceMeasurement::UnconfirmedRatePulses);
    if (!Tv5725::VideoSignal::isVideo(solvedLines_, again))
        return false;
    if (Tv5725::VideoSignal::ratesAgree(
            Tv5725::VideoSignal::lineRateFor(solvedLines_, again),
            solvedLineRateHz_, within)) {
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

    // THE SELECTION BASELINE IS TAKEN ON THE FIRST POLL. setup() applies the
    // saved input before loop() runs, so the first pass already sees it -- and
    // taking it at construction instead left the boot's own selection unseen
    // until its first acquisition had finished, where it read as a source
    // change and re-acquired everything that acquisition established.
    //
    // Here rather than in selectionMoved(), which runPass() reaches only once a
    // mode change has finished: a selection made before that is a COMMAND and
    // has to be answered, so the baseline cannot wait for the first comparison.
    if (!selectionEstablished_) {
        selectionEstablished_ = true;
        selectionSeen_ = VideoSourceSelection::selected();
    }

    bool detectionPass = false;
    sourceMeasured_ = false;
    const bool solved = runPass(nowMs, detectionPass);

    // On the cadence, not per call: loop() polls every time round and the
    // thresholds every reader keys on were tuned against a 20 ms pass.
    runAdvanced_ = detectionPass;
    if (!detectionPass)
        return solved;

    if (sourceState_ == SourceAcquired) {
        unmeasuredPasses_ = 0;
        referenceRetryPasses_ = 0;
        if (acquiredPasses_ < AcquiredPassCeiling)
            ++acquiredPasses_;
    } else {
        acquiredPasses_ = 0;
        if (unmeasuredPasses_ < AcquiredPassCeiling)
            ++unmeasuredPasses_;

        // Held one past the threshold rather than left to grow: a retry has to
        // stay reachable for as long as the source is unmeasurable, and the
        // reset on a successful advance is what paces it.
        if (referenceRetryPasses_ <= ReferenceRetryPasses)
            ++referenceRetryPasses_;
    }

    // Ungated: an output left away is a dark panel, and whether maintenance is
    // wanted says nothing about that. Asked every pass rather than after a
    // source solve, because an OUTPUT change takes the output away too and no
    // source solve follows one -- the source has not moved. Not on the pass
    // that solved: the caller matches the output rate to the source on that
    // pass, after this returns, and the rate belongs before the picture.
    if (!solved)
        presentWhenSettled(nowMs);

    if (maintenanceAllowed_)
        keepSourceComing(nowMs);

    return solved;
}

void VideoSourceAcquisition::presentWhenSettled(uint32_t nowMs)
{
    // Timed from the pass that first sees the pad away, whichever route took
    // it: a source event through this layer, or an output change through the
    // engine directly.
    if (videoPath_.syncOutAway()) {
        if (!syncAwaySeen_) {
            syncAwaySeen_ = true;
            syncAwayMs_ = nowMs;
            phaseSearched_ = false;
        }
    } else {
        syncAwaySeen_ = false;
    }

    if (videoPath_.outputShown() || sourceState_ != SourceAcquired
        || videoPath_.changing())
        return;

    if (syncAwaySeen_) {
        if ((uint32_t)(nowMs - syncAwayMs_) < MinimumSyncAwayMs)
            return;
        if (!Tv5725::Adc::dividerLatched(Tv5725::SyncProcessor::lineSamples()))
            return;
        // Once per transition, whatever the search finds: the maintenance
        // cadence tries again on a picture that is showing, and a transition
        // has re-latched the clock the last phase was found against.
        if (!phaseSearched_) {
            phaseSearched_ = true;
            acquireSamplingPhase();
        }
    }

    videoPath_.showOutput(true);
    syncAwaySeen_ = false;
}

void VideoSourceAcquisition::moveOutputForMeasuredRate()
{
    if (outputIsPassedThrough())
        return;
    const Tv5725::OutputMode *moveTo = resolutionToMoveTo();
    if (moveTo == 0)
        return;
    videoPath_.setOutputMode(moveTo);
    videoPath_.prepareToMeasure(sampling_.sourceLines());
}

void VideoSourceAcquisition::keepSourceComing(uint32_t nowMs)
{
    // ONE CLAIMANT PER LATCHED BIT. Reading STATUS_INT_SOG_SW claims it, and
    // the pre-emptive separator adjustment below wants the same event, so it is
    // sampled here and nowhere else and both are handed the answer.
    //
    // **ACTED ON ONLY WHERE THE SEPARATOR IS IN THE PATH.** A separate-sync
    // source does not go through one, and there the bit chatters -- every set
    // re-picking the divider and re-steering the display clock for a source
    // standing still. docs/sync-type-selection.md
    //
    // **IT DOES NOT COVER A RATE CHANGE AT AN UNCHANGED COUNT, so it is one of
    // two arms for that case and not the one to rely on.** One count running
    // at two field rates leaves the bit clear and rateMoved() is what notices.
    // The arm reason is chosen by priority, so the console printing `rate`
    // rather than `interrupt` is what says so. docs/sync-type-selection.md
    const bool disturbed = Tv5725::Interrupts::takeSourceDisturbed();
    if (disturbed && Tv5725::SyncMeasurement::isCsync())
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
            applySyncProcessorDynamic();
        if (tuning.phaseStale)
            Tv5725::Adc::forgetPhase();
    }

    followDynamicFacts();

    if (!sourceIsPresent())
        noteSourceAbsent();
    else
        maintainSource();

    serviceChannelSync(nowMs);
}

void VideoSourceAcquisition::restartRecovery()
{
    unmeasuredPasses_ = 0;
    referenceRetryPasses_ = 0;
}

bool VideoSourceAcquisition::selectionMoved()
{
    const VideoSourceSelection::Id chosenNow = VideoSourceSelection::selected();
    if (chosenNow == selectionSeen_)
        return false;
    selectionSeen_ = chosenNow;
    restartRecovery();
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
        return videoPath_.solveDeferred() && resolveFromSource(nowMs);

    // Sync type, then the count, then the scan mode. The order is the whole
    // point: the sync path decides what the sync processor counts, and the scan
    // mode decides the divider the measurement asks for.
    // docs/video-source-acquisition.md
    // THE SETTLED COUNT, NOT A READING. The scan mode is a property of the
    // source, and every other decision here waits for SteadySamples agreeing
    // counts. Any count inside the source bounds is a source, so one reading
    // dipping below DoubleBelowLines puts a progressive source on the doubled
    // scan at twice the divider, which the eleven-bit line counter cannot
    // hold.
    //
    // The sync processor's own count rather than the latch: it counts the source
    // directly and does not care what the input formatter is doing, which is
    // what breaks the circularity -- a scan mode left wrong makes the line rate
    // unmeasurable, and one derived after that gate is never reached.
    videoPath_.establishSyncType((uint8_t)VideoSourceSelection::selected());
    videoPath_.prepareToMeasure(sampling_.settledSourceLines());

    // Then whether the interval is serrated, which decides how long the hold
    // after it is. AFTER the reference divider the line above applies, because
    // a count read through the previous source's clock answers about that
    // source; and BEFORE the rate below, because the post coast gates the input
    // formatter's vertical and the rate is timed on it.
    videoPath_.measureVerticalInterval(sampling_.settledSourceLines());

    // The rate, then the output that rate can be carried at, then the clock
    // that output bounds, then the duty against that clock. The idle pass is
    // the only other writer of the state and the solve never reaches it, so
    // without the absent verdict here the answer holds whatever that pass last
    // concluded -- present -- for as long as the source cannot be read.
    bool settling = false;
    const Tv5725::SourceMeasurement::MeasurementStatus status = sampling_.measureRate(nowMs);
    if (!reading(status, settling)) {
        if (!settling) {
            sourceState_ = SourceAbsent;

            // STILL PART OF MEASURING THE SOURCE, not a timed recovery: once
            // a reading has had longer than one takes to settle, the clock it
            // was taken through is what has failed, so the next reference is
            // tried. VideoPath decides whether there is one.
            //
            // **NOT ON THE FIRST FAILURES.** A count the divider correction can
            // recover is recovered by comparing it AGAINST the divider in
            // force, so moving the clock underneath that prevents any
            // correction from completing, and the count never arrives.
            // A COUNT THAT NEVER STEADIES IS ALSO A CLOCK THAT CANNOT MEASURE
            // THE SOURCE. NotSteady returns before the line rate is read, so it
            // produces no reading and prints nothing: a recovery reachable only
            // from the Unmeasurable exit never runs here, and the state is a
            // silent stall rather than a cycle. What both exits share is the
            // discriminator -- something is pulsing, so the signal is there and
            // this clock cannot count it.
            const bool clockCannotMeasure =
                (status == Tv5725::SourceMeasurement::Unmeasurable
                 || status == Tv5725::SourceMeasurement::NotSteady)
                && sampling_.fieldRateHz() > 0.0f;
            if (referenceRetryPasses_ > ReferenceRetryPasses
                && videoPath_.tryNextReferenceClock(clockCannotMeasure)) {
                referenceRetryPasses_ = 0;
                tv5725Log("measure: the reference clock measured nothing, trying the next");
            }
        }
        gaveBackPassThrough();
        return false;
    }

    sourceMeasured_ = true;

    moveOutputForMeasuredRate();

    if (!installSamplingForMeasurement())
        return false;

    // AFTER THE INSTALL AND BEFORE THE DUTY. The two thresholds it compares
    // are fractions of the line in ADC samples, so the probe is of nothing
    // until the divider it is counted against is the one in force; and the
    // threshold it settles on is what every reading after it is taken through.
    // The install above re-latched the ADC PLL, so the settled state the probe
    // has to measure in does not exist until the settle it armed is spent --
    // and this is the only route that measures the interval, so the pass comes
    // back for it rather than leaving it unmeasured for the session.
    if (!videoPath_.measureEqualisation())
        return false;

    if (!reading(sampling_.measureDuty(nowMs), settling)) {
        if (!settling)
            sourceState_ = SourceAbsent;
        gaveBackPassThrough();
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
        videoPath_.setOutputMode(carriedResolution());
        return false;
    }

    if (videoPath_.solveFromMeasurement() != Tv5725::VideoPath::PollSolved)
        return false;
    holdSolvedSource();
    return true;
}

bool VideoSourceAcquisition::measureSource(uint32_t nowMs, bool &settling)
{
    if (!reading(sampling_.measureRate(nowMs), settling))
        return false;

    if (!installSamplingForMeasurement())
        return false;

    return reading(sampling_.measureDuty(nowMs), settling);
}

bool VideoSourceAcquisition::reading(Tv5725::SourceMeasurement::MeasurementStatus status,
                                     bool &settling)
{
    switch (status) {
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

    const bool csync = Tv5725::SyncMeasurement::isCsync();
    const bool onChannel = Tv5725::VideoRoute::isHdBypassChannel();

    // A component source on the channel at a 15 kHz line clamps later still:
    // the sync tip it has to clear is longer against that line.
    uint16_t offset =
        component && onChannel && sampling_.lowLineRate()
            ? Tv5725::SyncProcessor::ChannelComponentClampOffset : 0;

    // And an inverted SOG reference, or a leading-edge one, moves where the
    // processor counts from by a whole pulse. The clamp counts from that same
    // reference, so it follows by the same distance or it lands on the sync
    // tip. Asked of the block that wrote the reference rather than decided
    // again here, so the two cannot drift. The offsets describe different
    // things -- a sync tip and a displaced reference -- so they add; no source
    // here carries both.
    if (Tv5725::SyncProcessor::referenceMovedByPulse())
        offset = (uint16_t)(offset
                            + Tv5725::SyncProcessor::clampOffsetForPulse(
                                  sampling_.hsync(), sampling_.lineRateHz()));

    if (!Tv5725::SyncProcessor::acquireClampWindow(
            csync, component, sampling_.lineRateHz(),
            Tv5725::Adc::dividerInForce(), offset))
        return;

    if (Tv5725::VideoRoute::isHdBypassChannel())
        Tv5725::HdBypass::applyBlankLevel(component);

    Tv5725::SyncProcessor::adoptClampPlacement();
}

// THE LATCH HOLDS EVERY FACT THE CONFIGURATION IS KEYED ON. Half of
// SyncProcessor::applyDynamic() is decided by the sync type -- the coast
// inversion, the separation thresholds and the pulse ignore -- so a latch that
// remembers only the searching answer reports the configuration applied when
// the csync half was skipped, and nothing puts it back.
//
// The pass that falls back into searching is exactly the pass that forgets the
// sync type, the unusable-count arm doing both, so the inversion the search
// needs was written on none of them.
// ../../../docs/investigations/the-search-coast-inversion-outlived-the-search.md
void VideoSourceAcquisition::followDynamicFacts()
{
    if (dynamicApplied_ && dynamicForSearch_ == sourceIsSearching()
        && dynamicForCsync_ == Tv5725::SyncMeasurement::isCsync())
        return;

    applySyncProcessorDynamic();
}

void VideoSourceAcquisition::applySyncProcessorDynamic()
{
    if (!Tv5725::Chip::hasPower())
        return;

    Tv5725::SyncProcessor::Dynamic source;
    source.searching = sourceIsSearching();
    source.present = sourceIsPresent();
    source.csync = Tv5725::SyncMeasurement::isCsync();
    source.divider = Tv5725::Adc::dividerInForce();
    source.latched = videoPath_.samplingClockLatched();
    source.equalised = Tv5725::EqualisationMeasurement::assumeEqualisation();
    source.pathSource =
        VideoSourceSelection::isRgbhv(VideoSourceSelection::selected())
        || Tv5725::VideoRoute::isHdBypassChannel();

    dynamicApplied_ = true;
    dynamicForSearch_ = source.searching;
    dynamicForCsync_ = source.csync;

    Tv5725::SyncProcessor::applyDynamic(source);
}

void VideoSourceAcquisition::acquireSeparatorLevel()
{
    if (!Tv5725::Chip::hasPower()) {
        Tv5725::SyncOnGreen::choose(Tv5725::SyncOnGreen::DefaultLevel);
        return;
    }

    Tv5725::SyncOnGreen::chooseForInput();
    Tv5725::SyncOnGreen::acquire(clock_, Tv5725::SyncOnGreen::putInForce);
}

void VideoSourceAcquisition::noteSourceAbsent()
{
    report_.vsyncLockStale = true;

    // The first pass without a measurement is a dropped reading rather than a
    // source going away.
    if (unmeasuredPasses_ == 1)
        return;

    Tv5725::Adc::forgetPhase();
}

void VideoSourceAcquisition::maintainSource()
{
    SourceMaintenance::Source run;
    run.acquiredPasses = acquiredPasses_;
    run.unmeasuredPasses = unmeasuredPasses_;
    run.samplingPhaseFound = samplingPhaseFound();
    run.samplingClockSuspect = Tv5725::Adc::samplingClockSuspect();

    const SourceMaintenance::Due due = maintenance_.dueAt(run);

    if (due.restoreAfterLongAbsence) {
        Tv5725::Adc::forgetPhase();
        report_.frameTimingMoved = true;
    }

    if (due.forgetPositions)
        Tv5725::SyncProcessor::forgetPositions();

    if (due.syncProcessorDynamic)
        applySyncProcessorDynamic();

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

    if (!due.steerDeinterlacer)
        return;

    // Measured here rather than held: the deinterlacer steers on the
    // maintenance cadence, and a settled source runs no measure() pass at all.
    //
    // ON EVERY ROUTE, ahead of the channel's own return: the separation
    // threshold follows the scan whether or not the deinterlacer runs.
    // ../../../../docs/investigations/the-pulse-ignore-outlived-the-channels-entry.md
    const Tv5725::SourceMeasurement::ScanType scan = sampling_.measureScanType();

    if (Tv5725::VideoRoute::isHdBypassChannel())
        return;

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
