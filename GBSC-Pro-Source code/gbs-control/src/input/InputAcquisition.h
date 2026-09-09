#ifndef INPUT_ACQUISITION_H_
#define INPUT_ACQUISITION_H_

// Decide where video comes from and keep it coming. Owns the tick, measures the
// source, and calls Tv5725::VideoPath to solve the registers from the reading.
// docs/input-acquisition.md

#include <stdint.h>

#include "../tv5725/SourceMeasurement.h"
#include "../tv5725/VideoPath.h"

class InputAcquisition {
public:
    // Three answers, not two. A steady line count is the vertical half only: a
    // source can hold a correct count while the ADC samples a line it is not
    // locked to. Absent and Unlocked both want recovery, but only Unlocked is
    // worth re-probing the sync type on. docs/input-acquisition.md
    enum SourceState {
        SourceAbsent,
        SourceUnlocked,
        SourceAcquired,
    };

    InputAcquisition(Tv5725::SourceMeasurement &sampling,
                     Tv5725::VideoPath &videoPath);

    // One pass, from loop(). True when a mode change completes.
    bool poll(uint32_t nowMs);

    // Stop the pass running at all, for a bench measurement that has frozen
    // automation. An outstanding mode change survives the gate shutting.
    void useRunGate(bool (*mayRun)());

    // How often the source is counted. Every steadiness run below is counted in
    // these, so loop()'s own rate must not reach them.
    static const uint32_t DetectionIntervalMs = 20;

    float sourceFieldRateHz() const;
    uint32_t sourceLineRateHz() const;

    // Whether the source runs a 15 kHz line. Survives a bypass switch, which
    // measures nothing, so it answers with the rate from the mode before it.
    bool sourceLowLineRate() const;

    SourceState sourceState() const;

    // Acquired, and nothing outstanding against it. The second half matters: a
    // mode change in flight leaves the verdict taken before the source moved.
    bool sourceIsPresent() const;

    // Tell it the chip latched a disturbance. Arms a re-measure for a source
    // that returns at the same line count and a different field rate.
    void sourceInterrupted();

private:
    bool detectionDue(uint32_t nowMs);

    bool sourceMoved();
    bool rateMoved();
    bool countHeld(uint16_t lines);
    void holdSolvedSource();

    // Measure the source, through the state prepareToMeasure() just established.
    // `settling` distinguishes a source that cannot be read YET from one that
    // cannot be read at all; only the second is absent.
    bool measureSource(bool &settling);

    Tv5725::SourceMeasurement &sampling_;
    Tv5725::VideoPath &videoPath_;
    bool (*mayRun_)();
    uint32_t detectedMs_;
    bool detectedEver_;

    uint16_t idleLines_;
    uint8_t idleRun_;
    bool unusableCountArmed_;
    SourceState sourceState_;
    uint32_t candidateRateHz_;
    uint8_t rateRun_;
    bool sourceInterrupted_;
};

#endif  // INPUT_ACQUISITION_H_
