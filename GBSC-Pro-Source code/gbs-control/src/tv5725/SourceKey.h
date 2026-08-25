#ifndef TV5725_SOURCE_KEY_H_
#define TV5725_SOURCE_KEY_H_

// What identifies a source, so a framing can be kept against it and stored
// against it. The line count and a bucketed field rate, because those are what
// this chip can see: it locks to sync edges and cannot know the pixel clock, so
// two modes differing only in that are one source here and one entry.
// docs/framing-presets.md

#include <stdint.h>

namespace Tv5725 {

// Wide enough that the measured rate's jitter never crosses one, narrow enough
// that no two standards share one.
extern const float RateBucketHz;

class SourceKey {
public:
    SourceKey();
    SourceKey(uint16_t sourceLines, float fieldRateHz);

    // A count or a rate outside what any source runs identifies nothing, and
    // two of those are not each other: a settling source passes through counts
    // inside no standard at all.
    bool valid() const;

    uint16_t lines() const;
    uint16_t rateBucket() const;

    bool operator==(const SourceKey &other) const;
    bool operator!=(const SourceKey &other) const;

private:
    uint16_t lines_;
    uint16_t rateBucket_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_KEY_H_
