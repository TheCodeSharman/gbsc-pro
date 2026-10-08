#ifndef VIDEOSOURCE_SYNC_RECOVERY_H_
#define VIDEOSOURCE_SYNC_RECOVERY_H_

// What to try on a source that is not arriving, as three acts on a timer.
//
// It sits beside VideoSourceAcquisition rather than under Tv5725:: because an
// act is a step of ACQUISITION, not a register block: each one names operations
// the chip classes perform, and none of them is this one's to write. The class
// that holds the tick is the one that asks which act is due.
//
// **THESE ARE THE THREE ACTS NO RECONFIGURE CAN PERFORM FOR ITSELF**, which is
// what makes the list this length. Reconfiguring is everything a selection
// does; resetting the blocks is what is left when a configuration cannot take;
// moving the mux is the guess of last resort.
//
// **AN ACT IS A SELECTION'S WORTH OF WORK, SO IT GETS A SELECTION'S WORTH OF
// TIME.** A recovery fired into an acquisition that was working is what the
// timer exists to prevent, and it costs nothing to wait: the per-pass
// machinery -- the separator tuning, the search settings taken back on the
// first count, the maintenance a settled source is due -- is what recovers a
// momentary loss, not this.
//
// docs/video-source-acquisition.md, "Escalation".

#include <stdint.h>

class SyncRecovery {
public:
    enum Act : uint8_t {
        None = 0,
        Reconfigure,    // everything a selection does, to a path nothing counts
        ResetBlocks,    // the sync processor, Mode Detect and a held capture
        MoveInput,      // the other ADC input
    };

    // How long a source gets to arrive before anything is recovered. It is the
    // acquisition BUDGET: a source change is allowed ten seconds to the picture
    // being shown, so a source still absent at ten has spent it.
    static const uint32_t FirstActMs = 10000;

    // How long each act is given to show its effect before the next judges it.
    // The same budget, because an act is a selection's worth of work.
    static const uint32_t ActIntervalMs = FirstActMs;

    // The act due after this long without an acquired source. Pure: the caller
    // still decides whether the act's own precondition holds -- a mux it is
    // allowed to move, a source whose V sync proves it is there -- because those
    // are facts about the source rather than about the clock.
    //
    // It names the act whose window the reading falls in rather than firing one,
    // so the caller acts on the EDGE: an act that has run holds its window
    // without running again.
    static Act actAt(uint32_t unacquiredMs);

    // What to call an act in a diagnostic. Every act is a register write made to
    // a source nobody can see, so without this a unit that has been hunting for
    // a minute gives no account of what it has already tried.
    static const char *nameOf(Act act);
};


#endif  // VIDEOSOURCE_SYNC_RECOVERY_H_
