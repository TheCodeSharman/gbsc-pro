#ifndef VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_
#define VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_

// Decide where video comes from and keep it coming. Owns the tick, measures the
// video source, and calls Tv5725::VideoPath to solve the registers from the
// reading. docs/video-source-acquisition.md

#include <stdint.h>

#include "../tv5725/SourceMeasurement.h"
#include "../tv5725/VideoPath.h"
#include "SyncRecovery.h"

class VideoSourceAcquisition {
public:
    // Three answers, not two. A steady line count is the vertical half only: a
    // source can hold a correct count while the ADC samples a line it is not
    // locked to. Absent and Unlocked both want recovery, but only Unlocked is
    // worth re-probing the sync type on. docs/video-source-acquisition.md
    enum SourceState {
        SourceAbsent,
        SourceUnlocked,
        SourceAcquired,
    };

    VideoSourceAcquisition(Tv5725::SourceMeasurement &sampling,
                     Tv5725::VideoPath &videoPath);

    // One pass, from loop(). True when a mode change completes.
    bool poll(uint32_t nowMs);

    // Stop the pass running at all, for a bench measurement that has frozen
    // automation. An outstanding mode change survives the gate shutting.
    void useRunGate(bool (*mayRun)());

    // How the output route is moved into pass-through. Deciding to is this
    // class's, because pass-through is a statement about the measured source;
    // moving the route is a chip-wide switch the sketch still owns.
    void usePassThroughSwitch(void (*enter)());

    // Whether pass-through is offerable at all. The interim stand-in for a
    // per-source override -- a single boolean cannot express one.
    // docs/video-source-acquisition.md
    void allowPassThrough(bool allowed);

    // Re-derive every register from the framing held and the source as it reads
    // NOW: measure, hand the reading over, and have the engine solve from it.
    // Measures, so it must run from loop().
    bool resolveFromSource();

    // The output resolution the user asked for. Held HERE, because what the
    // output should do is decided here: pass-through suspends the resolution
    // rather than replacing it, so the way back is to this rather than to one
    // nobody chose -- and while it is suspended this only records, because the
    // measurement is what decides when the output returns.
    //
    // False where the caller has to load a preset instead.
    bool setOutputResolution(const Tv5725::OutputMode *mode);

    // How often the source is counted. Every steadiness run below is counted in
    // these, so loop()'s own rate must not reach them.
    static const uint32_t DetectionIntervalMs = 20;

    // Where the acquired run stops counting. Every threshold keyed on it is
    // well below this, and a run that saturates says the same thing as one that
    // keeps going: the source has been good for a long time.
    static const uint16_t AcquiredPassCeiling = 255;

    float sourceFieldRateHz() const;
    uint32_t sourceLineRateHz() const;

    // Whether the source runs a 15 kHz line. Survives a bypass switch, which
    // measures nothing, so it answers with the rate from the mode before it.
    bool sourceLowLineRate() const;

    SourceState sourceState() const;

    // Which recovery the escalation ladder is due, from this class's own run of
    // failed passes. The count lives here because the measurement that decides
    // it does: rto->noSyncCounter advanced on a source the engine calls
    // present, and walked the ADC and the sync processor off it.
    // docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md
    SyncRecovery::Step recoveryDue() const;

    // The run starts again, for a rung that settled the question rather than
    // advancing it -- a lock found on the other ADC input, or a sync-type
    // re-probe that found no V sync. Whether a step settled anything is the
    // caller's to say; the count cannot tell.
    void restartRecovery();

    // The two halves of the same run, counted off the same measurement. Passes
    // since the source was last acquired, and passes since it stopped being --
    // one of them is always zero. Maintenance keys off the first and the
    // escalation ladder off the second.
    uint16_t acquiredPasses() const;
    uint16_t unmeasuredPasses() const;

    // Acquired, and nothing outstanding against it. The second half matters: a
    // mode change in flight leaves the verdict taken before the source moved.
    bool sourceIsPresent() const;

    // Tell it the chip latched a disturbance. Arms a re-measure for a source
    // that returns at the same line count and a different field rate.
    void sourceInterrupted();

private:
    bool detectionDue(uint32_t nowMs);

    bool sourceMoved();

    // Whether the source just measured arrives intact only by being handed
    // over. Both halves are the measurement's: a raster the line doubler is not
    // needed for, at a rate that reaches the sink. docs/capture-limits.md
    bool passThroughSuitsSource() const;

    // Whether video routes around the VDS, which the mode in force says.
    bool outputIsPassedThrough() const;

    // Hand the source to the panel, moving the route only where it is not
    // already there: every measurement re-answers pass-through, so staying is
    // the usual answer and re-running the switch would drop sync output on a
    // picture that is working.
    bool passSourceThrough();

    bool rateMoved();
    bool countHeld(uint16_t lines);
    void holdSolvedSource();


    // Measure the source, through the state prepareToMeasure() just established.
    // `settling` distinguishes a source that cannot be read YET from one that
    // cannot be read at all; only the second is absent.
    bool measureSource(bool &settling);

    // One pass, with the run gate already asked and the ladder's count still
    // to advance. Split out so every return from it is counted.
    bool runPass(uint32_t nowMs, bool &detectionPass);

    Tv5725::SourceMeasurement &sampling_;
    Tv5725::VideoPath &videoPath_;
    bool (*mayRun_)();
    void (*passThroughSwitch_)();
    bool passThroughAllowed_;
    const Tv5725::OutputMode *resolution_;
    uint32_t detectedMs_;
    bool detectedEver_;

    // What the last solve ran against, which is what a fresh reading is compared
    // against. 0 lines means nothing has been solved, which is what bypass
    // leaves too.
    uint16_t solvedLines_;
    uint32_t solvedLineRateHz_;

    Tv5725::SteadyRun idle_;
    bool unusableCountArmed_;
    SourceState sourceState_;
    uint32_t candidateRateHz_;
    uint8_t rateRun_;
    bool sourceInterrupted_;

    // Consecutive passes that did not reach an acquired source. Wrapped at the
    // ladder's cycle rather than left to run, so the cycle stays aligned.
    uint16_t unmeasuredPasses_;
    uint16_t acquiredPasses_;
};

#endif  // VIDEOSOURCE_VIDEO_SOURCE_ACQUISITION_H_
