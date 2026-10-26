#ifndef TV5725_BUS_HISTOGRAM_H_
#define TV5725_BUS_HISTOGRAM_H_

#include <stdint.h>

namespace Tv5725 {

// What a test-bus pair reads, summarised as it is sampled.
//
// Most of the sync processor's pairs are live wires rather than latched
// values, so a single read of one samples the wire's state and says nothing
// about the source. Only a DISTRIBUTION taken over many reads distinguishes
// two states, and only the device can take one: an HTTP read answers at tens
// of hertz at best.
//
// The slot table keeps the first values seen rather than the most frequent,
// which is why overflowed() is reported beside it -- a partial table that did
// not say so would read as the whole distribution.
class BusHistogram {
public:
    // How many distinct values the table holds.
    static const uint8_t Slots = 8;

    BusHistogram();

    void clear();
    void add(uint16_t value);

    uint32_t count() const;
    uint16_t lowest() const;
    uint16_t highest() const;

    // The integer mean. Zero where nothing has been added, there being no
    // value to report rather than a mean of zero.
    uint16_t mean() const;

    // How many distinct values the table holds, which is at most Slots.
    uint8_t distinct() const;

    // Whether more distinct values arrived than the table holds, so the ones
    // below are a sample of the distribution rather than all of it.
    bool overflowed() const;

    uint16_t valueAt(uint8_t slot) const;
    uint32_t countAt(uint8_t slot) const;

private:
    uint32_t count_;
    uint64_t total_;
    uint16_t lowest_;
    uint16_t highest_;
    uint8_t distinct_;
    bool overflowed_;
    uint16_t value_[Slots];
    uint32_t seen_[Slots];
};

}  // namespace Tv5725

#endif  // TV5725_BUS_HISTOGRAM_H_
