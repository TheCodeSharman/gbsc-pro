#ifndef TV5725_SYNC_ON_GREEN_H_
#define TV5725_SYNC_ON_GREEN_H_

#include "Tv5725.h"

namespace Tv5725 {

// The sync separator -- the datasheet's name for it, DS-5725-3.2's "internal
// sync separator to support SOG/SOY" -- and the one owner of its level,
// ADC_SOGCTRL.
//
// Green and luma are the same pin -- the datasheet names it SOG/Y, "analog
// SOG/Y input" -- and the encoding is what relabels them. So this covers RGsB
// and YPbPr both, and there is one level rather than one per colour space.
//
// Nothing on the board measures the sync amplitude, so the level cannot be
// calculated. Too high slices into dark picture and invents edges, too low
// misses real pulses, and what is left is to step it and watch whether the sync
// processor sees clean edges.
//
// **THE LEVEL IS HELD, NOT READ BACK.** Every ratchet that walks it takes the
// level it last set as its starting point; deriving the next one from the
// register asks the chip what it was told, and a read taken while the source is
// unlocked walks it somewhere nobody chose. docs/retiring-the-sync-watcher.md
class SyncOnGreen {
public:
    typedef UReg<0x05, 0x02, 1, 5> ADC_SOGCTRL;                       // When = 1, ADC enable SOG mode SOG control signal ADC

    // The widest slice the field carries.
    static const uint8_t LevelMax = 31;

    // Where the walk starts a source it knows nothing about, and where it
    // returns when it reaches the floor without finding a level that works.
    static const uint8_t DefaultLevel = 13;

    // Where a separator whose output does not move at all is parked. The walk
    // needs a reading that changes to work from, so there is nothing to search
    // and a mid level is the whole of the answer.
    static const uint8_t FrozenLevel = 5;

    // Whether the sync separator reaches the sync processor at all. It does only with
    // SP_SOG_MODE 1, which follows the sync type -- so on a separate-sync
    // source every level is inert, and a recovery that walks it is moving a
    // control nothing is reading.
    //
    // Held state, not SP_SOG_MODE read back: the register echoes what
    // applyForSyncType() wrote, and the two disagree while a probe is moving
    // the path.
    static bool inSyncPath();

    // The level to run at, without touching the sync separator. Several sites choose
    // one for a source the ADC has not been brought up for yet, and the
    // bring-up applies it.
    //
    // A level past the field is refused rather than truncated: masking put 32
    // in as 0, which is the sync separator fully open and the one value no ratchet can
    // climb back out of.
    static void choose(uint8_t level);

    // Choose it and put it in force.
    static void apply(uint8_t level);

    // Put the chosen level in force, for a caller that did not choose it.
    static void apply();

    static uint8_t level();

    // Walk the chosen level down until the sync processor holds clean edges
    // over a run, and put DefaultLevel back if the floor is reached without
    // finding one. Chooses DefaultLevel and touches nothing when the sync separator is
    // not in the sync path.
    //
    // Putting a level in force also latches the sampling phases and the ADC
    // PLL, which are Adc's.
    static void acquire(uint32_t (*nowMs)(), void (*putInForce)());

    // The same walk, two levels at a time and without the settling runs, for a
    // source whose sync has only just gone: the windows acquire() waits out
    // cost more than the attempt is worth there. Judges the sync separator's output
    // alone rather than pairing it with a run of HSACT.
    //
    // Leaves the level where it is when the sync separator is not in the sync path,
    // where acquire() puts the default back.
    static void acquireCoarse(void (*putInForce)());

    // Lift the level one step where it has no room left to step down. The walk
    // leaves it at the floor when nothing it tried worked, and a separator that
    // far open slices noise as sync, so a source whose sync has only just gone
    // gets one step back before anything heavier is tried.
    static void liftOffFloor(void (*putInForce)());

    // Re-acquire the level for a source that will not lock. The separator's own
    // output is what decides how: a measured line length that never moves
    // across a run of reads is a separator slicing nothing, where the walk has
    // no evidence to search with and FrozenLevel is the only move left. One
    // that moves is a separator finding edges at the wrong threshold, which is
    // what the walk is for.
    //
    // The walk is handed in for the reason acquire()'s is: refusing to walk is
    // part of it.
    // docs/investigations/refusing-to-walk-is-part-of-the-walk.md
    //
    // `reopen` takes the walk's place with the separator fully open, for a
    // caller that has already run out of walks.
    static void reacquire(void (*walk)(), void (*putInForce)(), bool reopen);

    // What a tuning pass leaves for someone else to do. Each belongs to
    // another class and is claimed by a later step of
    // docs/retiring-the-sync-watcher.md: the sync processor refresh at step 5,
    // the vsync lock stamp at step 11, the sampling phase at step 6.
    struct Tuning {
        bool sourceUnsettled;
        bool levelMoved;
        bool phaseStale;
    };

    // Step the level down ahead of a sync loss, on a source that is acquired.
    // Nothing measures the sync amplitude, so the evidence is a run of
    // bad-hsync samples inside a window and the response is one step down.
    //
    // `sourceClassified` false counts every sample bad: with no standard
    // detected there is nothing to compare a line length against, so the pass
    // stops waiting for evidence it cannot get.
    //
    // The window and the bad-sample count are held across passes, so a mode
    // change has to say they are stale: forgetWindow().
    //
    // `escalate` is the walk, run once the level is too low to step. It is the
    // caller's because REFUSING to walk is part of it: during a detection sweep
    // the walk is called faster than a source can lock, and it pins the level
    // at the floor -- measured at 2, where a separate-sync source then never
    // acquires at all and no restart recovers it.
    static Tuning tune(bool sourceDisturbed, bool sourceClassified,
                       uint32_t (*nowMs)(), void (*putInForce)(),
                       void (*escalate)());

    static void forgetWindow(uint32_t nowMs);

private:
    // How long evidence is gathered before the level is judged, and how many
    // bad samples inside one window ask for a step.
    static const uint16_t WindowMs = 3000;
    static const uint16_t StepThreshold = 17;

    // The count that hands over to the walk, once the level itself is too low
    // to step any further.
    static const uint16_t HandoverThreshold = 40;

    static void step(uint8_t to, void (*putInForce)());
    static bool trimEarnedMargin(void (*putInForce)());
    static bool stepOnEvidence(void (*putInForce)(), void (*escalate)());

    static uint8_t level_;
    static uint32_t windowStart_;
    static uint16_t badSamples_;
    static bool steppedInWindow_;
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_ON_GREEN_H_
