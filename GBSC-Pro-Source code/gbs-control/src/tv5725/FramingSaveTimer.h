#ifndef TV5725_FRAMING_SAVE_TIMER_H
#define TV5725_FRAMING_SAVE_TIMER_H

#include <stdint.h>

namespace Tv5725 {

// When the framing table has held still long enough to be worth writing.
//
// The save is automatic, so anything that moves the framing and leaves it there
// persists it as that source's remembered framing. The inhibit is the opt-out a
// caller needs to disturb the framing without that happening.
class FramingSaveTimer {
public:
    void inhibit(bool on);
    bool inhibited() const;

    // Poll. True at most once per revision, and never while inhibited.
    bool due(uint16_t revision, uint32_t now, uint32_t quietMs);

    void markSaved(uint16_t revision);

private:
    uint16_t seen_ = 0;
    uint16_t saved_ = 0;
    uint32_t movedAt_ = 0;
    bool inhibited_ = false;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_SAVE_TIMER_H
