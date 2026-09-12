#ifndef TV5725_STEADY_RUN_H_
#define TV5725_STEADY_RUN_H_

#include <stdint.h>

namespace Tv5725 {

// A run of samples agreeing, and the one definition of what agreeing means.
//
// There are two of these over the same register read -- the gate before the
// field rate is paid for, and the gate that decides the source moved -- and
// they must advance independently, because filling one while the engine is idle
// leaves the next mode change's first poll believing a count from the mode
// before it. Separate instances, one policy.
//
// **A PAIR ALTERNATING BY ONE COUNTS AS AGREEING.** An interlaced field carries
// a half line, so its count cannot hold still and a run demanding identical
// samples never completes. That is measured on both bench sources, and it is
// the only scan-type signal that survives separate sync.
// docs/investigations/interlaced-source-measurement.md
class SteadyRun {
public:
    explicit SteadyRun(uint8_t samples);

    // Take one sample. True once enough have agreed, and on every sample that
    // keeps agreeing after that.
    bool sample(uint16_t value);

    // Hold a value the caller has already judged unusable: it is what value()
    // reports, and it settles nothing.
    void restart(uint16_t value);

    // Start satisfied at a value another, longer run has already established,
    // so this one does not re-earn it.
    void settle(uint16_t value);

    void reset();

    // The settled value, and the HIGHER of an alternating pair -- both of those
    // undercount the true field, so the higher is the closer.
    uint16_t value() const;

    // Whether the settled run is a pair differing by one, which only an
    // interlaced field makes a count do.
    bool alternated() const;

private:
    uint8_t samples_;
    uint8_t run_;
    uint16_t high_;
    uint16_t low_;
};

}  // namespace Tv5725

#endif  // TV5725_STEADY_RUN_H_
