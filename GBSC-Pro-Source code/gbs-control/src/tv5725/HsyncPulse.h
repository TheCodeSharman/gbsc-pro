#ifndef TV5725_HSYNC_PULSE_H_
#define TV5725_HSYNC_PULSE_H_

// The shape of the source's hsync pulse, which is what places picture inside
// the line.
//
// **THE LINE COUNT IS NOT HERE**, and the reason is the order: the scan mode is
// judged from the count and the reference sampling clock follows the scan mode,
// so the count has to be read BEFORE the clock this duty is counted against is
// in force. Two facts, two moments. docs/video-source-acquisition.md
//
// **THE SYNC LOW TIME IS A FRACTION OF THE LINE, NOT THE REGISTER'S COUNT.**
// STATUS_SYNC_PROC_HLOW_LEN counts ADC samples, so its value means nothing
// without the divider it was counted against -- and the divider moves on every
// solve, which would strand a reading taken before one. Every consumer wants
// the ratio anyway. docs/scaler-geometry-model.md

#include <stdint.h>

namespace Tv5725 {

class HsyncPulse {
public:
    HsyncPulse();
    HsyncPulse(float syncDuty, bool syncAtHead);

    float syncDuty() const;

    // Whether the hsync pulse is positive-going, which is what says which end
    // of the line the sync interval sits at.
    bool syncAtHead() const;

private:
    float syncDuty_;
    bool syncAtHead_;
};

}  // namespace Tv5725

#endif  // TV5725_HSYNC_PULSE_H_
