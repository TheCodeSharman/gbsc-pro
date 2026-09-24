#include "FrameTimeLock.h"

#include <stdio.h>
#include <string.h>

#include "../tv5725/Adc.h"
#include "../tv5725/FrameSync.h"
#include "../tv5725/SourceMeasurement.h"
#include "../tv5725/SyncProcessor.h"
#include "../tv5725/Tv5725Log.h"
#include "../tv5725/VideoPath.h"
#include "VideoSourceAcquisition.h"

const uint8_t FrameTimeLock::HeldPasses;
const uint8_t FrameTimeLock::ArmPasses;
const uint16_t FrameTimeLock::ArmQuietMs;
const uint8_t FrameTimeLock::FailuresForgiven;

namespace {

// Reported to nobody. The lock defers itself after each correction, so this is
// the answer for most of the passes between two of them, and a state that
// alternates with "running" every second is the console telling itself the
// time.
const char *const Pacing = "pacing";

const char *const NotLatched = "the divider is not latched";

bool dividerLatched()
{
    return Tv5725::Adc::dividerLatched(Tv5725::SyncProcessor::lineSamples());
}

}  // namespace

FrameTimeLock::FrameTimeLock(Tv5725::FrameSync &lock,
                             VideoSourceAcquisition &acquisition,
                             Tv5725::VideoPath &videoPath,
                             Tv5725::SourceMeasurement &sampling)
    : lock_(lock), acquisition_(acquisition), videoPath_(videoPath),
      sampling_(sampling), failuresLeft_(FailuresForgiven), reported_(NULL) {}

void FrameTimeLock::forgiveFailures() { failuresLeft_ = FailuresForgiven; }

// Asked only where the lock is not already armed, so every answer is a reason
// and none of them is "it is".
const char *FrameTimeLock::unarmedBecause(uint32_t nowMs)
{
    if (acquisition_.acquiredPasses() < ArmPasses)
        return "not armed: the source has not held long enough";
    if (!Tv5725::SyncProcessor::coastPlaced())
        return "not armed: no coast window";
    if (!lock_.quietFor(ArmQuietMs, nowMs))
        return "not armed: something keeps disturbing the lock";
    if (!dividerLatched())
        return "not armed: the divider is not latched";
    if (!lock_.init())
        return "not armed: the vsync periods cannot be read";
    return NULL;
}

const char *FrameTimeLock::blockedBy(const Conditions &conditions, uint32_t nowMs)
{
    if (!conditions.optionEnabled)
        return "the option is off";
    if (!conditions.sourcePresent)
        return "no source";
    if (!videoPath_.scalerCarriesVideo())
        return "the video bypasses the scaler";
    if (!conditions.syncWatcherEnabled)
        return "the sync watcher is off";
    if (!lock_.ready())
        return unarmedBecause(nowMs);
    if (!lock_.quietFor(Tv5725::FrameSync::LockIntervalMs, nowMs))
        return Pacing;
    if (acquisition_.acquiredPasses() <= HeldPasses)
        return "the source has not held long enough";
    if (acquisition_.unmeasuredPasses() != 0)
        return "the source is unmeasured";
    return NULL;
}

// On change only. The gate is asked every pass, and a state that holds for
// minutes would flood the console it exists to explain.
void FrameTimeLock::report(const char *state)
{
    if (reported_ != NULL && strcmp(reported_, state) == 0)
        return;
    reported_ = state;

    char line[80];
    snprintf(line, sizeof line, "frame time lock: %s", state);
    tv5725Log(line);
}

void FrameTimeLock::service(const Conditions &conditions, uint32_t nowMs)
{
    const char *blocked = blockedBy(conditions, nowMs);

    if (blocked == NULL) {
        if (!dividerLatched()) {
            blocked = NotLatched;
        } else {
            // The rate correction is smooth and the raster one is not, so the
            // rate is preferred wherever a generator can carry it.
            const bool ran =
                lock_.canSteerRate()
                    ? lock_.runFrequency(sampling_.settledFieldRateHz())
                    : lock_.runVsync(conditions.method);
            if (ran) {
                forgiveFailures();
            } else if (failuresLeft_ == 0) {
                lock_.reset(conditions.method);
                forgiveFailures();
            } else {
                --failuresLeft_;
            }
        }
        lock_.defer(nowMs);
    }

    if (blocked != Pacing)
        report(blocked == NULL ? "running" : blocked);
}
