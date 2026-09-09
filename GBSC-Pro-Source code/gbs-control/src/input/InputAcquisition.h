#ifndef INPUT_ACQUISITION_H_
#define INPUT_ACQUISITION_H_

// Deciding where video comes from, and keeping it coming.
//
// It sits above Tv5725:: and owns the tick: the escalation, the input policy
// and the no-signal report are its, and the scaler's share is VideoPath's.
// docs/input-acquisition.md

#include <stdint.h>

#include "../tv5725/SourceMeasurement.h"
#include "../tv5725/VideoPath.h"

class InputAcquisition {
public:
    InputAcquisition(Tv5725::SourceMeasurement &sampling,
                     Tv5725::VideoPath &videoPath);

    // One tick for the whole acquisition path, taken from loop(). True on the
    // pass that completes a mode change.
    bool poll(uint32_t nowMs);

    // Whether the tick may be taken at all, asked at the top of every poll().
    // Everything below it writes registers, so a bench measurement that has
    // frozen automation has to stop it. Without one the path always runs, which
    // is what every caller did before.
    //
    // A change outstanding when the gate shuts stays outstanding, so the output
    // stays blanked until it opens again: a mode change stopped half way
    // through has no settled timing to show the encoder.
    void useRunGate(bool (*mayRun)());

    // How often the source is counted. loop() goes round far faster than this,
    // so a steadiness run counted per call is not the same length as one
    // counted per tick and every threshold keyed on it means something
    // different.
    static const uint32_t DetectionIntervalMs = 20;

    // What the source is running, as the last measurement found it. This class
    // coordinates the measurement, so it is the one that can answer; VideoPath
    // is handed the reading and derives registers from it.
    // docs/input-acquisition.md
    float sourceFieldRateHz() const;
    uint32_t sourceLineRateHz() const;

    // Whether the source runs a 15 kHz line. Held across a bypass switch, which
    // measures nothing, so a caller asking whether the display can show this
    // source gets the rate from the mode that preceded it.
    bool sourceLowLineRate() const;

private:
    // Whether this pass is a detection pass. Consumes the tick, so it is asked
    // once.
    bool detectionDue(uint32_t nowMs);

    Tv5725::SourceMeasurement &sampling_;
    Tv5725::VideoPath &videoPath_;
    bool (*mayRun_)();
    uint32_t detectedMs_;
    bool detectedEver_;
};

#endif  // INPUT_ACQUISITION_H_
