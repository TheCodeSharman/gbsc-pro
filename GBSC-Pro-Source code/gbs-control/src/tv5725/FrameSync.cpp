#include "FrameSync.h"

#include <math.h>
#include <stdio.h>

#include "../../gbs_types.h"
#include "../clock/RateAgreement.h"
#include "DebugPin.h"
#include "DisplayClock.h"
#include "TestBus.h"
#include "TestBusRateMeasurement.h"
#include "Tv5725Log.h"
#include "VideoRoute.h"

namespace Tv5725 {

const int32_t FrameSync::DefaultTargetPhase;
const int16_t FrameSync::Correction;
const uint32_t FrameSync::LockIntervalMs;

const float FrameSync::FieldRateMinHz = 47.0f;
const float FrameSync::FieldRateMaxHz = 86.0f;

namespace {

// How long the raster write waits for the field it wants, in polls of
// STATUS_VDS_FIELD. A bound rather than a duration: the field always turns
// over, and spinning for one that does not is how the loop stops running.
const uint16_t FieldWaitPolls = 400;

// The raster is stretched by 2/525 of a frame per unit of latency error -- the
// difference between SNES and Wii 240p. Arbitrary, and it works in practice.
const float CorrectionPerFrame = 0.0038f;

// Some displays lose sync on a 0.1% frequency change, which is what switching
// between 59.94 and 60 costs. Clamping to 0.06% keeps the output inside what a
// sink will follow, both against the source's rate and against the last frame's
// own output rate.
const float MaxCorrection = 0.0006f;
const float MaxFrameRateChange = 0.0006f;

// Two tries at measuring the phase. One is enough where the pin is carrying
// vsync at all, and a second costs a frame period; more than that is a signal
// that is not there, which the caller's forgiveness count is for.
const int PhaseAttempts = 2;

// How many measurements of the output rate are taken looking for two that
// agree. Each one spins for up to a frame period, so they are few.
const uint8_t OutputRateAttempts = 5;

bool rateIsPlausible(float hz)
{
    return hz >= FrameSync::FieldRateMinHz && hz <= FrameSync::FieldRateMaxHz;
}

}  // namespace

FrameSync::FrameSync(DisplayClock &clock)
    : clock_(clock), targetPhase_(DefaultTargetPhase), ready_(false),
      delayLock_(0), lastCorrection_(0), disturbedMs_(0),
      clockPerFrameRate_(-1.0f) {}

void FrameSync::defer(uint32_t nowMs) { disturbedMs_ = nowMs; }

bool FrameSync::quietFor(uint32_t ms, uint32_t nowMs) const
{
    return nowMs - disturbedMs_ > ms;
}

bool FrameSync::ready() const { return ready_; }

bool FrameSync::canSteerRate() const { return clock_.driving(); }

int32_t FrameSync::targetPhase() const { return targetPhase_; }

void FrameSync::setTargetPhase(int32_t degrees)
{
    targetPhase_ = ((degrees % 360) + 360) % 360;
}

int16_t FrameSync::lastCorrection() const { return lastCorrection_; }

bool FrameSync::steerable(const char *what) const
{
    char line[80];

    if (!clock_.driving()) {
        snprintf(line, sizeof line, "%s: no external display clock to steer", what);
        tv5725Log(line);
        return false;
    }
    if (GBS::PAD_CKIN_ENZ::read() != 0) {
        snprintf(line, sizeof line, "%s: the external clock input pad is off", what);
        tv5725Log(line);
        return false;
    }
    if (VideoRoute::isHdBypassChannel()) {
        snprintf(line, sizeof line, "%s: the HD bypass channel carries the video", what);
        tv5725Log(line);
        return false;
    }
    // Not a sentinel: PLL_VS4 = 11 is what takes the display clock from PCLKIN.
    // Any other mapped byte is the internal PLL, whose rate nothing can slew.
    // ../../../docs/tv5725-chip.md
    if (GBS::PLL648_CONTROL_01::read() != DisplayClock::ExternalPclkIn) {
        snprintf(line, sizeof line, "%s: the display clock is internal", what);
        tv5725Log(line);
        return false;
    }
    return true;
}

// Every sample is printed. The display clock is set to a RATIO involving this
// rate, so a pair that agrees and is wrong beats against the source for as long
// as the boot runs -- and declining to steer is silent, so without the samples
// a refusal and a route that never ran look the same.
// ../../../docs/known-issues.md
float FrameSync::agreedOutputRate() const
{
    char line[64];
    float previous = 0.0f;

    for (uint8_t attempt = 0; attempt < OutputRateAttempts; ++attempt) {
        float rate = TestBusRateMeasurement::outputFrameRateHz();

        snprintf(line, sizeof line, "rate sample %u: %lu mHz", (unsigned)attempt,
                 (unsigned long)(rate * 1000.0f));
        tv5725Log(line);

        if (!rateIsPlausible(rate)) {
            previous = 0.0f;
            continue;
        }
        if (previous != 0.0f && Clock::RateAgreement::agree(previous, rate))
            return rate;
        previous = rate;
    }

    tv5725Log("rate: no two samples agreed, not steering");
    return 0.0f;
}

bool FrameSync::matchRate(float sourceFieldRateHz)
{
    if (!steerable("rate match"))
        return false;

    if (!rateIsPlausible(sourceFieldRateHz)) {
        char line[64];
        snprintf(line, sizeof line, "rate: no settled source rate yet (%lu mHz)",
                 (unsigned long)(sourceFieldRateHz * 1000.0f));
        tv5725Log(line);
        return false;
    }

    float outputRate = agreedOutputRate();
    if (outputRate == 0.0f)
        return false;

    const uint32_t from = clock_.hzNow();
    initFrequency(outputRate, from);
    clock_.slewTo((uint32_t)((sourceFieldRateHz / outputRate) * from));

    // In milli-hertz: a pair that agrees to the 0.05% tolerance can still be
    // 0.03 Hz apart at 60 Hz, and whole hertz cannot see that.
    char line[104];
    snprintf(line, sizeof line,
             "rate match: source %lu mHz, output %lu mHz, clock %lu -> %lu",
             (unsigned long)(sourceFieldRateHz * 1000.0f),
             (unsigned long)(outputRate * 1000.0f), (unsigned long)from,
             (unsigned long)clock_.hzNow());
    tv5725Log(line);

    return true;
}

bool FrameSync::vsyncPeriodAndPhase(int32_t *periodInput, int32_t *periodOutput,
                                    int32_t *phase)
{
    TestBus::selectInputVsync();

    uint32_t inStart, inStop, outStart, outStop;

    if (!debugPinPulseEdges(&inStart, &inStop)) {
        tv5725Log("vsyncPeriodAndPhase(): no INPUT vsync");
        debugPinProbe();
        return false;
    }

    // The VDS's blanking, which is where the read pointer is.
    TestBus::selectOutputVsync();

    uint32_t inPeriod = inStop - inStart;
    if (!debugPinPulseEdges(&outStart, &outStop)) {
        tv5725Log("vsyncPeriodAndPhase(): no OUTPUT vsync");
        return false;
    }

    uint32_t diff = (outStart - inStart) % inPeriod;

    if (periodInput)
        *periodInput = inPeriod;
    if (periodOutput)
        *periodOutput = outStop - outStart;
    if (phase)
        *phase = (diff < inPeriod) ? diff : diff - inPeriod;

    return true;
}

// Whether there is a raster and both vsync periods can be read, which is the
// whole of what arms the lock. **DO NOT PUT AN HTOTAL SEARCH BACK HERE**:
// Tv5725::VideoPath solves the raster. ../../../docs/video-source-acquisition.md
bool FrameSync::bothVsyncPeriodsReadable()
{
    if (GBS::VDS_HSYNC_RST::read() == 0)
        return false;

    int32_t inPeriod, outPeriod;
    if (!vsyncPeriodAndPhase(&inPeriod, &outPeriod, NULL))
        return false;

    return inPeriod != 0 && outPeriod != 0;
}

bool FrameSync::init()
{
    if (!bothVsyncPeriodsReadable())
        return false;

    ready_ = true;
    delayLock_ = 0;
    return true;
}

void FrameSync::moveRaster(int16_t delta, uint8_t frameTimeLockMethod)
{
    uint16_t vtotal = 0, vsst = 0;
    GBS::Tie<GBS::VDS_VSYNC_RST, GBS::VDS_VS_ST>::read(vtotal, vsst);

    vtotal += delta;

    // Method 0 moves the vsync position with the total, so the pulse keeps its
    // place in the raster. Method 1 leaves it where it is.
    if (frameTimeLockMethod == 0)
        vsst += delta;

    // Each write lands in the field that is not being read out.
    uint16_t polls = 0;
    while (GBS::STATUS_VDS_FIELD::read() == 1 && ++polls < FieldWaitPolls)
        ;
    GBS::VDS_VS_ST::write(vsst);

    polls = 0;
    while (GBS::STATUS_VDS_FIELD::read() == 0 && ++polls < FieldWaitPolls)
        ;
    GBS::VDS_VSYNC_RST::write(vtotal);
}

void FrameSync::reset(uint8_t frameTimeLockMethod)
{
    if (lastCorrection_ != 0)
        moveRaster(-lastCorrection_, frameTimeLockMethod);

    char line[48];
    snprintf(line, sizeof line, "frame time lock: reset(%u)",
             (unsigned)frameTimeLockMethod);
    tv5725Log(line);

    ready_ = false;
    lastCorrection_ = 0;
    delayLock_ = 0;

    // clockPerFrameRate_ is deliberately kept. Callers reset() without going on
    // to initFrequency() and expect runFrequency() to keep working; the raster
    // paths that do invalidate it call clearFrequency() themselves.
}

void FrameSync::cleanup()
{
    tv5725Log("frame time lock: cleanup, forgetting the output rate");

    lastCorrection_ = 0;
    ready_ = false;
    delayLock_ = 0;
    clockPerFrameRate_ = -1.0f;
}

bool FrameSync::runVsync(uint8_t frameTimeLockMethod)
{
    if (!ready_)
        return false;

    // Two passes of settling before the first correction: the raster has just
    // been written and a phase measured across that is not the steady one.
    if (delayLock_ < 2) {
        delayLock_++;
        return true;
    }

    int32_t period, phase;
    if (!vsyncPeriodAndPhase(&period, NULL, &phase))
        return false;

    int32_t target = (targetPhase_ * period) / 360;
    int16_t correction = phase > target ? 0 : Correction;

    if (correction == lastCorrection_)
        return true;

    moveRaster(correction - lastCorrection_, frameTimeLockMethod);
    lastCorrection_ = correction;
    return true;
}

void FrameSync::clearFrequency() { clockPerFrameRate_ = -1.0f; }

void FrameSync::initFrequency(float outFramesPerS, uint32_t displayClockHz)
{
    clockPerFrameRate_ = (float)displayClockHz / outFramesPerS;
}

bool FrameSync::runFrequency(float sourceFieldRateHz)
{
    if (clockPerFrameRate_ < 0) {
        tv5725Log("frame time lock: no output/input rate ratio yet");
        return true;
    }

    // An external state rather than a bad signal, so not a lock failure.
    if (!steerable("frame time lock"))
        return true;

    if (!ready_) {
        tv5725Log("frame time lock: not armed");
        return false;
    }

    if (!rateIsPlausible(sourceFieldRateHz)) {
        tv5725Log("frame time lock: no settled source rate to correct towards");
        return true;
    }

    const float ticksPerSecond = (float)debugPinTicksPerSecond();
    const float rateInput = sourceFieldRateHz;

    int32_t periodInput = 0;
    int32_t phase = 0;
    bool measured = false;

    for (int attempt = 0; attempt < PhaseAttempts; attempt++) {
        if (vsyncPeriodAndPhase(&periodInput, NULL, &phase)) {
            measured = true;
            break;
        }
    }

    if (!measured) {
        tv5725Log("frame time lock: the phase could not be measured");
        return false;
    }

    int32_t target = (targetPhase_ * periodInput) / 360;

    // Distance behind target, in fractional frames. Latency rising means the
    // read pointer is falling behind, so the output rate goes up.
    const float latencyErrFrames =
        (float)(phase - target) / ticksPerSecond * rateInput;

    float correction = CorrectionPerFrame * latencyErrFrames;
    if (correction > MaxCorrection)
        correction = MaxCorrection;
    if (correction < -MaxCorrection)
        correction = -MaxCorrection;

    const float rawRateOutput = rateInput * (1 + correction);
    const float previousRateOutput = (float)clock_.hzNow() / clockPerFrameRate_;

    // Where the input rate is measured wrong the raw figure can be far from the
    // last output rate, so the step is clamped against that too: the first
    // clamp is long-term stability against the source, this one is short-term
    // stability against ourselves.
    float rateOutput = rawRateOutput;
    if (rateOutput > previousRateOutput * (1 + MaxFrameRateChange))
        rateOutput = previousRateOutput * (1 + MaxFrameRateChange);
    if (rateOutput < previousRateOutput * (1 - MaxFrameRateChange))
        rateOutput = previousRateOutput * (1 - MaxFrameRateChange);

    const uint32_t steered = (uint32_t)(clockPerFrameRate_ * rateOutput);

    // In milli-hertz: the whole correction is bounded at 0.06%, which whole
    // hertz cannot show at 60 Hz.
    char line[112];
    snprintf(line, sizeof line,
             "frame time lock: in %lu mHz, out %lu -> %lu mHz, clock %lu -> %lu",
             (unsigned long)(rateInput * 1000.0f),
             (unsigned long)(previousRateOutput * 1000.0f),
             (unsigned long)(rateOutput * 1000.0f),
             (unsigned long)clock_.hzNow(), (unsigned long)steered);
    tv5725Log(line);

    clock_.slewTo(steered);
    return true;
}

}  // namespace Tv5725
