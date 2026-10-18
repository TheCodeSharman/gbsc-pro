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
    // How far apart the two values of an alternating pair may sit. An
    // interlaced field's half line moves the count by one, so that is the
    // default and it is what the scan decision reads.
    static const uint16_t InterlacedPairSpread = 1;

    // **THE SPREAD IS THE CALLER'S**, because the runs over this register are
    // not asking the same question: whether a field is interlaced and whether
    // the source moved have different answers. A count the chip miscounts
    // alternates further than one -- the Wii at 480p reads 521 against a true
    // 524 -- and a run fixed at the interlaced spread reads that as the source
    // leaving and arriving several times a second.
    //
    // It never merges two modes, because what earns a pair is the count
    // RETURNING: CrossingsForInterlace crossings, where a one-way move holds
    // and is adopted instead.
    explicit SteadyRun(uint8_t samples,
                       uint16_t pairSpread = InterlacedPairSpread);

    // A run of identical samples that collapses a widened pair back onto one
    // value. Without it the pair never narrows and alternated() is true for the
    // life of the run, which leaves motion adapt weaving a progressive picture.
    //
    // It has to clear the runs a genuinely interlaced source shows, and those
    // are measured rather than assumed -- this is three times the longest run
    // either value held, and Deinterlacer::FilteredPasses is a second filter
    // behind it. test_steady_run.cpp holds the measurement.
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

    // The same question at this run's own spread.
    bool agrees(uint16_t a, uint16_t b) const;

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
    uint16_t pairSpread_;
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
