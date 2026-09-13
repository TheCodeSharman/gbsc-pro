#include "VideoSourceAcquisition.h"

#include <stdio.h>

#include "../tv5725/OutputMode.h"
#include "../tv5725/SyncProcessor.h"
#include "../tv5725/Tv5725Log.h"

VideoSourceAcquisition::VideoSourceAcquisition(Tv5725::SourceMeasurement &sampling,
                                   Tv5725::VideoPath &videoPath)
    : sampling_(sampling), videoPath_(videoPath), mayRun_(0),
      passThroughSwitch_(0), passThroughAllowed_(false), resolution_(0),
      detectedMs_(0),
      detectedEver_(false), solvedLines_(0), solvedLineRateHz_(0),
      idle_(Tv5725::SourceMeasurement::SteadySamples),
      unusableCountArmed_(false), sourceState_(SourceAbsent),
      candidateRateHz_(0), rateRun_(0), sourceInterrupted_(false),
      unmeasuredPasses_(0) {}

void VideoSourceAcquisition::useRunGate(bool (*mayRun)()) { mayRun_ = mayRun; }

void VideoSourceAcquisition::usePassThroughSwitch(void (*enter)()) { passThroughSwitch_ = enter; }

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
    sampling_.applyReferenceSampling(videoPath_.oversample());

    if (!sampling_.measureLineRate())
        return videoPath_.deferSolve();

    videoPath_.sourceMeasured(sampling_.readSource());
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
           && sampling_.bypassSuitsCount(sampling_.sourceLines())
           && sampling_.rateCanBypass();
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

uint32_t VideoSourceAcquisition::sourceLineRateHz() const { return sampling_.heldLineRateHz(); }

bool VideoSourceAcquisition::sourceLowLineRate() const { return sampling_.lowLineRate(); }

VideoSourceAcquisition::SourceState VideoSourceAcquisition::sourceState() const { return sourceState_; }

bool VideoSourceAcquisition::sourceIsPresent() const
{
    return sourceState_ == SourceAcquired && !videoPath_.changing();
}

void VideoSourceAcquisition::sourceInterrupted() { sourceInterrupted_ = true; }

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
    const bool plausible = Tv5725::SourceMeasurement::countIsSource(lines);
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
    idle_.reset();
    return true;
}

bool VideoSourceAcquisition::rateMoved()
{
    const uint32_t rate = Tv5725::SourceMeasurement::measureLineRateFromHPeriod(solvedLines_);
    if (rate == 0 || solvedLineRateHz_ == 0
        || Tv5725::SourceMeasurement::ratesAgree(rate, solvedLineRateHz_)) {
        candidateRateHz_ = 0;
        rateRun_ = 0;
        return false;
    }

    if (candidateRateHz_ == 0
        || !Tv5725::SourceMeasurement::ratesAgree(rate, candidateRateHz_)) {
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
    const uint32_t confirmed = Tv5725::SourceMeasurement::lineRateFrom(
        solvedLines_, getSourceFieldRate(0));
    if (confirmed == 0 || !Tv5725::SourceMeasurement::ratesAgree(rate, confirmed))
        return false;

    // The held rate is what moved, and measureLineRate() rejects a rate that
    // changed at an unchanged count -- so leaving it would refuse the very
    // measurement this armed the solve for.
    sampling_.forgetHeldRate();
    return true;
}

bool VideoSourceAcquisition::poll(uint32_t nowMs)
{
    if (mayRun_ != 0 && !mayRun_())
        return false;

    const bool solved = runPass(nowMs);

    if (sourceState_ == SourceAcquired)
        unmeasuredPasses_ = 0;
    else
        unmeasuredPasses_ = (uint16_t)((unmeasuredPasses_ + 1) % SyncRecovery::CycleLength);

    return solved;
}

SyncRecovery::Step VideoSourceAcquisition::recoveryDue() const
{
    return SyncRecovery::stepAt(unmeasuredPasses_);
}

bool VideoSourceAcquisition::runPass(uint32_t nowMs)
{
    // Asked once a pass whether it is used or not, so the cadence does not
    // stretch over a mode change and fire the moment one lands.
    const bool detection = detectionDue(nowMs);

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

    // The hsync pulse, now that the reference sampling clock it is counted
    // against is in force and locked. The engine solves every window from this
    // and reads nothing back itself.
    videoPath_.sourceMeasured(sampling_.readSource());

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
    // The cheap gate. Everything below this line measures, and the field rate
    // costs up to 250 ms a vsync pulse. The reference sampling clock is what
    // opens it: a count taken through the previous mode's divider is not the
    // source's.
    if (!sampling_.sampleSteady()) {
        // The count settled on the serrations, so the coast pair in force is
        // not covering them. Margin over the default rather than a search for
        // the lowest pair that works: which pairs measure a source is not
        // reproducible between runs.
        // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
        if (sampling_.countWasSerrations())
            Tv5725::SyncProcessor::widenCoast();
        return false;
    }

    if (!sampling_.measureLineRate())
        return false;

    // A rate is worth sizing a raster from once it has REPEATED. The cross-check
    // inside measureLineRate() bounds the rate against the line count, which
    // catches a settling source off by tens of percent and passes one off by
    // tenths -- and the raster is out by whatever fraction the rate is, for
    // good, because nothing re-solves it.
    settling = !sampling_.rateSettled();
    return !settling;
}
