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

    // A run of identical samples that collapses a widened pair back onto one
    // value. Without it the pair never narrows and alternated() is true for the
    // life of the run, which leaves motion adapt weaving a progressive picture.
    //
    // It has to clear the runs a genuinely interlaced source shows, and those
    // are measured rather than assumed: RISC PC at 800x600@60 with INTERLACE
    // ON, 1873 samples at the engine's own 20 ms detection interval, the
    // longest run of either value is five. This is three times that, and
    // Deinterlacer::FilteredPasses is a second filter behind it.
    static const uint8_t CollapseSamples = 16;

    // How many times the count must reach the second value before the pair
    // widens onto it.
    //
    // One reading off by one is not evidence of interlace: a source wobbles
    // once as it is acquired, and a pair widened by that reads as alternating
    // for the whole run, because nothing narrows it again until CollapseSamples
    // -- which is eight times what Deinterlacer::FilteredPasses waits. An
    // interlaced field count reaches its second value every other sample for as
    // long as it runs, so three of them separates the two and costs a genuinely
    // interlaced source a few fields.
    static const uint8_t CrossingsForInterlace = 3;

    // A run of one value this long forgets a second value seen but not yet
    // earned. Without it, wobbles far enough apart to be the same evidence
    // twice add up to an alternation the source never showed. The bound is the
    // measurement CollapseSamples rests on -- the longest run either value of a
    // genuinely interlaced count holds is five -- so it cannot fire on one.
    static const uint8_t AlternationStaleRun = 8;

    // Whether two counts are the same measurement: equal, or the pair an
    // interlaced field alternates between.
    //
    // **THE ONE DEFINITION**, shared with sample() and with anything comparing
    // a fresh count against a settled one. A caller using == instead re-arms a
    // mode change on every field of an alternating source, and each one costs a
    // sync-type probe and a re-solve.
    // ../../../../docs/known-issues.md
    static bool agree(uint16_t a, uint16_t b);

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

    // Whether enough samples have agreed for the run to say anything at all.
    bool settled() const;

    // Whether the settled run is a pair differing by one, which only an
    // interlaced field makes a count do. A latch: the samples that would keep
    // answering it stop once the source is acquired, so what the pair is
    // ALLOWED to hold is where the evidence is weighed, not what it is asked
    // afterwards.
    bool alternated() const;

private:
    uint8_t samples_;
    uint8_t run_;
    uint8_t same_;
    uint8_t crossings_;
    uint16_t high_;
    uint16_t low_;
    uint16_t latest_;
    uint16_t candidate_;

    void forgetCandidate();
};

}  // namespace Tv5725

#endif  // TV5725_STEADY_RUN_H_
