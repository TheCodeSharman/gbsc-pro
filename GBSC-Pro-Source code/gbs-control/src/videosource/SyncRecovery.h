#ifndef VIDEOSOURCE_SYNC_RECOVERY_H_
#define VIDEOSOURCE_SYNC_RECOVERY_H_

// The escalation ladder as an ordered list of named recoveries, one per
// position, rather than a run of moduli on a counter.
//
// It sits beside VideoSourceAcquisition rather than under Tv5725:: because a
// rung is an act of ACQUISITION, not a register block: every step names an
// operation one of the chip classes performs, and none of them is this one's
// to write. The class that holds the tick is the one that asks which step is
// due.
//
// **THE MODULI CARRIED TWO FACTS AND ONLY ONE WAS INTENDED.** `% 27` says both
// "third in the order" and "again for ever", and the second is what makes the
// list unreadable: rungs interleaved by accident of their periods, so which
// recovery had been tried by a given count could not be read off the code. That
// is also why a gate cannot open in front of the ladder as it stands -- letting
// the counter advance where it used to sit pinned starts rungs that never ran.
//
// So each step fires ONCE at its position. The positions are today's first-fire
// counts, unchanged, so the first pass through the list keeps the timing that
// was tuned on the bench; what goes is the repetition.
//
// **THE LIST CYCLES, because a source that is genuinely unplugged needs it to.**
// The moduli never stopped, and stopping would leave a unit that had been
// switched off and on again with no way back. Cycling keeps that while making a
// position mean something, and it costs nothing extra: the input toggle is the
// last step, so it stays the rarest thing the ladder does, which is what its
// `% 413` was buying.
//
// docs/video-source-acquisition.md, "Escalation".

#include <stdint.h>

class SyncRecovery {
public:
    // One named recovery each, in the order they are tried.
    enum Step : uint8_t {
        None = 0,
        LiftSogFloor,           // the separator is on its floor and sync is serrated
        CoastWindow,            // default coast, widened for serration
        SyncProcessorDynamic,   // re-apply the dynamic sync-processor settings
        ReleaseCapture,         // the write FIFO is holding a frame
        HoldClamp,              // component sources, whose clamp can sit wrong
        NudgeModeDetect,        // move the thresholds off a boundary
        HsyncOverflowProtect,   // csync only
        FullReset,              // sync processor and Mode Detect, with the windows
        ReprobeSyncType,        // ask whether the source has its own V sync
        ToggleInput,            // the guess of last resort: the other ADC input
        ReopenSogSeparator,     // walks exhausted: reopen the separator fully
    };

    // Nothing escalates on the first failed pass. One dropped measurement is not
    // a source going away, and the free pass is what stops a single one costing
    // a recovery.
    static const uint16_t FirstEscalationPass = 2;

    // Where the list restarts. One past the last step's position, so the last
    // step fires at its own count rather than at zero.
    static const uint16_t CycleLength = 451;

    // The step due after this many consecutive failed passes, or None. Pure:
    // the caller still decides whether that step's own precondition holds --
    // serrated sync, a component input, csync -- because those are facts about
    // the source rather than about the position.
    static Step stepAt(uint16_t passes);

    // The position each step occupies, for a caller that wants to say how far
    // the ladder has got. 0 for None.
    static uint16_t positionOf(Step step);
};


#endif  // VIDEOSOURCE_SYNC_RECOVERY_H_
