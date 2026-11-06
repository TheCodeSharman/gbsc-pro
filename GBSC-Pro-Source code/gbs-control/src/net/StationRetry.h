#ifndef NET_STATION_RETRY_H_
#define NET_STATION_RETRY_H_

#include <stdint.h>

namespace Net {

// When to attempt the station association again, so that falling back to the
// access point is not the end of it.
//
// The fallback used to be terminal: startApMode() cleared the one timestamp
// handleWiFi() gates on, and nothing set it again, so a boot that missed the
// association -- or one association drop -- took the unit off the network for
// the life of the boot and only a power cycle brought it back. Every "the unit
// is down and needs a mains power cycle" reading came from that.
// docs/known-issues.md
class StationRetry {
public:
    // The gap between the START of one attempt and the next, so it must
    // exceed WIFI_CONNECT_TIMEOUT -- 45 s -- or a retry lands on top of an
    // attempt still running. That bound is why this is not smaller; the unit
    // then rejoins within about a minute of the network coming back.
    static const uint32_t RetryIntervalMs = 60000;

    StationRetry();

    // Begin retrying, from now. Called where the access point is raised.
    void arm(uint32_t nowMs);

    // Stop asking -- the station is associated.
    void disarm();

    bool armed() const;

    // Whether an attempt is due. **Signed difference**, so the cadence
    // survives the millis() wrap; unsigned, a retry falling either side of it
    // would wait another 49 days, which is the defect this class removes.
    bool due(uint32_t nowMs) const;

    // Record an attempt, which restarts the interval whether it succeeded or
    // not: a caller that only recorded successes would retry continuously.
    void attempted(uint32_t nowMs);

private:
    bool armed_;
    uint32_t lastAttemptMs_;
};

} // namespace Net

#endif
