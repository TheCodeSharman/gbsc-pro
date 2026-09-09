#ifndef TV5725_SYNC_MEASUREMENT_H
#define TV5725_SYNC_MEASUREMENT_H

#include <stdint.h>

namespace Tv5725 {

// Measure whether the source is composite sync or separate sync, and hold the
// answer.
//
// The chip cannot report it. STATUS_SYNC_PROC_VSACT tells you which sync path is
// configured, not what the source sends, so deciding from it just confirms
// whatever the unit is already doing. hasOwnVsync() below moves the sync path
// and watches for V instead. docs/sync-type-selection.md
class SyncMeasurement {
public:
    // Composite sync, as last measured or set. Never probes.
    static bool isCsync();

    // Whether the held value came from a probe. A value merely set has not been
    // measured, so it must not suppress one.
    static bool isSet();

    static void set(bool csync);
    static void forget();

    // The held sync type, probing only if nothing has been measured yet.
    static bool syncType(bool (*hasOwnVsync)());

    // Probe regardless, for the paths that have just moved the input.
    static bool probe(bool (*hasOwnVsync)());

    // Move the sync path, watch for V, and put the path back. Costs up to
    // OwnVsyncWindowMs, so callers probe per source rather than per pass.
    //
    // Takes the clock rather than calling millis(), which is not available to a
    // host test.
    static bool hasOwnVsync(uint32_t (*nowMs)());

    // How long the sync processor is given to reacquire V after the path moves.
    static const uint16_t OwnVsyncSettleMs = 240;

    // How long after that V may still arrive and count. SIZED FOR THE TAIL:
    // reacquisition is 2-3 ms most of the time but runs to at least 242 ms, and
    // the error is one-directional -- a false no puts composite separation on a
    // separate-sync source and collapses SP_VTOTAL, while a false yes cannot
    // happen because the bit only rises when V arrives.
    static const uint16_t OwnVsyncWindowMs = 1000;

private:
    static bool csync_;
    static bool set_;
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_MEASUREMENT_H
