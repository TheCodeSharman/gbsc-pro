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
    // What can be said about the source, which is three answers and not two.
    //
    // A steadiness run over the line count answers the VERTICAL question alone,
    // and a source can hold a correct, steady count while the ADC samples a line
    // it is not locked to. Measured after a sync-type round trip:
    // STATUS_SYNC_PROC_VTOTAL 311 held for a minute against
    // STATUS_SYNC_PROC_HTOTAL near 3250 for a divider of 2250, the ADC PLL out
    // of lock, the picture scrambled and every config register reading correct.
    // Absent and Unlocked both want recovery run and Acquired wants none, so
    // collapsing the first two loses nothing there -- but only Unlocked is worth
    // re-probing the sync type on, and a caller that cannot tell a source that
    // is GONE from one that is THERE AND WRONG has to guess.
    enum SourceState {
        SourceAbsent,     // no line count anything video runs at, held
        SourceUnlocked,   // counting, and the ADC is not sampling the line chosen for it
        SourceAcquired,   // counting steadily at the solved count, and sampling it
    };

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

    // What the last idle pass concluded. Published by that pass rather than
    // recomputed, because the steadiness run behind it is advanced by the
    // reading it is taken from.
    //
    // A LIVE COUNT IS NOT THIS ANSWER. An unlocked sync processor produces
    // counts inside the source bounds -- 216, 271, 276, 312, 305 measured on a
    // source that was genuinely gone -- so whatever withholds a recovery has to
    // see the count hold still first.
    SourceState sourceState() const;

    // Whether the source is acquired AND nothing is outstanding against it,
    // which is the one state that wants no recovery run.
    //
    // THE SECOND HALF IS NOT BELT AND BRACES. The state is published by the idle
    // pass, so a mode change in flight leaves the verdict taken BEFORE the source
    // moved standing -- acquired -- and a gate reading the state alone withholds
    // recovery for as long as the engine goes on failing to settle.
    bool sourceIsPresent() const;

    // The source disturbed, as the chip latched it. Arms a re-measure, which the
    // line count alone cannot: a source returning at the same count and a
    // different field rate moves nothing the count can see.
    void sourceInterrupted();

private:
    // Whether this pass is a detection pass. Consumes the tick, so it is asked
    // once.
    bool detectionDue(uint32_t nowMs);

    // Whether the source has settled on something the last solve did not run
    // against. Measured here, so a mode change needs nobody to announce it.
    bool sourceMoved();

    // Whether the source is running a rate the last solve did not run against.
    // A refusal from HPERIOD_IF is no information rather than a change, and a
    // rate that does not hold across a run is the register railing -- which
    // passes the judgement inside lineRateFromHPeriod() on its own.
    bool rateMoved();

    // Whether the count has held for a steadiness run.
    bool countHeld(uint16_t lines);

    // Take the count a completed solve ran against as a run already held.
    void holdSolvedSource();

    Tv5725::SourceMeasurement &sampling_;
    Tv5725::VideoPath &videoPath_;
    bool (*mayRun_)();
    uint32_t detectedMs_;
    bool detectedEver_;

    uint16_t idleLines_;     // the count seen while no mode change is outstanding
    uint8_t idleRun_;        // how many detection passes it has held it
    bool unusableCountArmed_;  // a count no source runs has already armed a change
    SourceState sourceState_;  // what the idle pass last concluded
    uint32_t candidateRateHz_;  // a rate not yet corroborated across a run
    uint8_t rateRun_;           // how many passes have agreed on it
    bool sourceInterrupted_;    // the chip latched a disturbance, nothing has re-measured
};

#endif  // INPUT_ACQUISITION_H_
