#ifndef VIDEOSOURCE_FRAME_TIME_LOCK_H_
#define VIDEOSOURCE_FRAME_TIME_LOCK_H_

// When the frame time lock is allowed to run, and which of its two corrections
// runs. Tv5725::FrameSync is the correction itself; this is everything that has
// to be true before one is worth making.
//
// It says which condition is shut, because an option whose gate is shut and an
// option that reaches nothing look identical from the picture.

#include <stdint.h>

namespace Tv5725 {
class FrameSync;
class SourceMeasurement;
class VideoPath;
}

class VideoSourceAcquisition;

class FrameTimeLock {
public:
    // How long a source must have held before a correction is made against it,
    // in acquisition passes. A correction measured across a settling source
    // steers the output towards a rate the source is about to leave.
    static const uint8_t HeldPasses = 20;

    // What ARMING wants of the same source: a shorter hold, and nothing
    // disturbing the lock for this long.
    static const uint8_t ArmPasses = 10;
    static const uint16_t ArmQuietMs = 500;

    // How many consecutive failed corrections are forgiven before the lock is
    // reset. A measurement fails for reasons that pass, so resetting on the
    // first one never lets it establish.
    static const uint8_t FailuresForgiven = 16;

    // What the sketch holds and this cannot measure.
    struct Conditions {
        bool optionEnabled;
        bool sourcePresent;
        bool syncWatcherEnabled;

        // 0 moves the vsync pulse with the raster total, 1 leaves it alone.
        uint8_t method;
    };

    FrameTimeLock(Tv5725::FrameSync &lock, VideoSourceAcquisition &acquisition,
                  Tv5725::VideoPath &videoPath,
                  Tv5725::SourceMeasurement &sampling);

    // One pass, from loop(). Arms the lock, runs it when it can, and reports
    // which of the two it is doing.
    void service(const Conditions &conditions, uint32_t nowMs);

    // Which condition is shut, or NULL while all of them are open.
    //
    // **THE ORDER IS THE COST ORDER.** Arming takes two blocking vsync samples
    // and the divider check costs a register read, so neither is asked until
    // the free conditions have passed.
    const char *blockedBy(const Conditions &conditions, uint32_t nowMs);

    // Start the failure count again, for a caller that has just moved the
    // output out from under the lock.
    void forgiveFailures();

private:
    const char *unarmedBecause(uint32_t nowMs);
    void report(const char *state);

    Tv5725::FrameSync &lock_;
    VideoSourceAcquisition &acquisition_;
    Tv5725::VideoPath &videoPath_;
    Tv5725::SourceMeasurement &sampling_;
    uint8_t failuresLeft_;
    const char *reported_;
};

#endif  // VIDEOSOURCE_FRAME_TIME_LOCK_H_
