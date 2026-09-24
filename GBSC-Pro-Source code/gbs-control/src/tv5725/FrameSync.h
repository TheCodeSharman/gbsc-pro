#ifndef TV5725_FRAME_SYNC_H_
#define TV5725_FRAME_SYNC_H_

// Steer the output frame time towards the source's, so the read pointer crosses
// the write pointer where it does no harm.
//
// Two mechanisms, one aim. Where an external generator drives the display clock
// the output rate is steered directly -- runFrequency(). Where the internal PLL
// drives it the rate is fixed, so the raster is stretched by a line or two
// instead -- runVsync(). The first is smooth and the second is not, which is
// why the generator is preferred wherever the board has one.
//
// The crossover shows on the picture as a stationary tear at targetPhase/360
// down the screen, and it belongs in vertical blanking. docs/framesync.md

#include <stdint.h>

namespace Tv5725 {

class DisplayClock;

class FrameSync {
public:
    // Degrees of one INPUT frame. Settable because the only instrument that can
    // judge it is the picture.
    static const int32_t DefaultTargetPhase = 90;

    // How far one vsync correction moves the raster, in lines.
    static const int16_t Correction = 2;

    // How often a correction is due: a hundred frames at 60 Hz. Exposed because
    // the gate paces itself on it and takes a different interval for arming.
    static const uint32_t LockIntervalMs = 1670;

    // What a field rate has to be inside to be believed. Outside it the
    // measurement is noise, not a source running at an unusual rate.
    static const float FieldRateMinHz;
    static const float FieldRateMaxHz;

    explicit FrameSync(DisplayClock &clock);

    // The lock was run, or something disturbed it: a source not steady enough
    // to correct against, a mode change, or a user command that moved the
    // output. Either way a correction measured before now is worthless.
    void defer(uint32_t nowMs);

    // Nothing has disturbed the lock for this long. Both callers ask it of
    // their own interval -- running a correction is due less often than arming
    // one -- so the interval is the caller's.
    bool quietFor(uint32_t ms, uint32_t nowMs) const;

    // Arm the lock. False where the source or the output cannot be measured
    // yet, which leaves it unarmed for the caller to retry.
    bool init();
    bool ready() const;

    // Take the last correction back out of the raster and disarm. The raster
    // has to land somewhere defined in both directions, or a correction
    // measured against the state before a change stays in it for ever.
    void reset(uint8_t frameTimeLockMethod);

    // Disarm and forget the correction without unwinding it, for a raster that
    // is about to be rewritten anyway.
    void cleanup();

    int32_t targetPhase() const;
    void setTargetPhase(int32_t degrees);
    int16_t lastCorrection() const;

    // Forget the clock-to-frame-rate ratio, which a raster change invalidates.
    void clearFrequency();

    // Establish that ratio: each output frame is a fixed number of video clocks
    // long at a given output resolution, so the clock rate should be
    // proportional to the input field rate.
    void initFrequency(float outFramesPerS, uint32_t displayClockHz);

    // One correction. True means the lock ran or had nothing to do; false means
    // the measurement failed and the caller should consider resetting.
    bool runFrequency();
    bool runVsync(uint8_t frameTimeLockMethod);

private:
    bool vsyncPeriodAndPhase(int32_t *periodInput, int32_t *periodOutput,
                             int32_t *phase);
    bool bothVsyncPeriodsReadable();

    // Move the raster by `delta` lines, between fields so the write lands on a
    // raster nothing is reading.
    void moveRaster(int16_t delta, uint8_t frameTimeLockMethod);

    DisplayClock &clock_;
    int32_t targetPhase_;
    bool ready_;
    uint8_t delayLock_;
    int16_t lastCorrection_;
    uint32_t disturbedMs_;

    // Display clocks per output frame, or -1 where no ratio has been
    // established. Kept across reset(): callers reset without re-establishing
    // it and expect runFrequency() to keep working.
    float clockPerFrameRate_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAME_SYNC_H_
