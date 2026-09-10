#ifndef TV5725_SOURCE_KEY_H_
#define TV5725_SOURCE_KEY_H_

// What identifies a source, so a framing can be kept against it and stored
// against it. The line count and the field rate, because those are what
// this chip can see: it locks to sync edges and cannot know the pixel clock, so
// two modes differing only in that are one source here and one entry.
// docs/framing-presets.md

#include <stdint.h>

namespace Tv5725 {

// How far two field-rate readings may sit apart and still be the same source.
//
// **A BUCKET CANNOT DO THIS JOB.** Quantising the rate puts a boundary
// somewhere, and a standard whose rate lands near one has its identity flip
// under ordinary jitter -- DMT's 800x600@60 is 60.317 Hz, 0.18 Hz from the edge
// of a one-hertz bucket, and the bench read that source at 60.32 and 60.72
// within a session. A tolerance has no boundary to land near.
//
// Wide enough for that jitter, narrow enough that no two standards sharing a
// line count come within it: the closest such pair is ten hertz apart.
extern const float RateToleranceHz;

class SourceKey {
public:
    SourceKey();
    SourceKey(uint16_t sourceLines, float fieldRateHz);

    // A count or a rate outside what any source runs identifies nothing, and
    // two of those are not each other: a settling source passes through counts
    // inside no standard at all.
    bool valid() const;

    uint16_t lines() const;
    // The measured rate. Rounded on the way to flash, because the stored line
    // is text and a tenth of a hertz identifies nothing the tolerance does not.
    float rateHz() const;

    bool operator==(const SourceKey &other) const;
    bool operator!=(const SourceKey &other) const;

private:
    uint16_t lines_;
    float rateHz_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_KEY_H_
