#ifndef VIDEOSOURCE_SOURCE_MAINTENANCE_H_
#define VIDEOSOURCE_SOURCE_MAINTENANCE_H_

// What an acquired source is due to have done to it, and when. The acts belong
// to the classes that own the registers behind them; what lives here is the
// cadence, which is the part that was only expressible as a run of literal pass
// counts inside one function. docs/video-source-acquisition.md
//
// It sits beside SyncRecovery, the same shape one level up: that one names the
// rung a FAILING source is due, this one the maintenance an acquired one is.
// Neither performs anything, and both leave a step's own precondition to the
// caller, because those are facts about the source rather than about the count.

#include <stdint.h>

class SourceMaintenance {
public:
    // The run as the acquisition tick counts it, plus the one fact the cadence
    // cannot derive: whether the sampling phase has been found, which stops the
    // search rather than timing it.
    struct Source {
        uint16_t acquiredPasses;
        uint16_t unmeasuredPasses;
        bool samplingPhaseFound;
    };

    // Named acts, each true only on the pass it falls due.
    struct Due {
        // The run of failed passes behind this source was long enough that
        // nothing established against the previous one can be trusted.
        bool restoreAfterLongAbsence;

        bool holdCapture;
        bool syncProcessorDynamic;
        bool sogLevel;
        bool samplingPhase;
        bool forgetPositions;
        bool acknowledgeSogBad;
        bool steerDeinterlacer;
    };

    // How long an acquired source must have held before an act is worth
    // making at all. NOT Due: these stay true once reached, and what stops
    // them running twice is the register state the act leaves behind rather
    // than the pass they fell due on.
    struct Ready {
        bool coastWindow;
        bool clampWindow;
        bool autoGain;
    };

    SourceMaintenance();

    Due dueAt(const Source &source);
    static Ready readyAt(const Source &source);

    // Where a run of failed passes stops being a dropped measurement and starts
    // being a source that went away. The ladder's own full reset sits here too.
    static const uint16_t LongAbsencePasses = 150;

private:
    // Whether a long absence is still waiting to be answered. The absence is
    // seen while the source is unmeasured and the answer runs once it is back,
    // so the two cannot be the same pass.
    bool restoreArmed_;
};

#endif  // VIDEOSOURCE_SOURCE_MAINTENANCE_H_
