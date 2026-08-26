#ifndef TV5725_SYNC_TYPE_H
#define TV5725_SYNC_TYPE_H

namespace Tv5725 {

// Whether the source carries composite sync or brings its own H and V.
//
// It cannot be read back: STATUS_SYNC_PROC_VSACT reports the path already
// configured, not a property of the source, so anything deciding from it
// confirms whatever the unit is already doing. docs/sync-type-selection.md
//
// The probe that breaks that costs ~500 ms, so it runs once per SOURCE. The
// probe itself belongs to the sketch -- it is a 240 ms settle and a 250 ms poll
// -- and arrives here as a function to call.
class SyncType {
public:
    static bool isCsync();

    // Whether the held value came from a probe. A value merely set has not been
    // measured, so it must not suppress the probe.
    static bool isSet();

    static void set(bool csync);
    static void forget();

    // Probes only if nothing has been measured for this source.
    static bool probeOnce(bool (*sourceHasOwnVsync)());

    // Probes regardless, for the paths that have just moved the input.
    static bool probe(bool (*sourceHasOwnVsync)());

private:
    static bool csync_;
    static bool set_;
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_TYPE_H
