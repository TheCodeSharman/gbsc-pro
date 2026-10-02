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
    explicit HsyncPulse(float syncDuty);

    float syncDuty() const;

    // Whether this duty is a sync pulse at all.
    //
    // STATUS_SYNC_PROC_HLOW_LEN counts the LOW time, so until the polarity
    // correction has reached the counter a high-active source reports the line
    // MINUS the pulse -- measured at 800x600 as 0.878 where the mode's duty is
    // 0.1212. Taken as a duty that complement reaches SourceTiming's match,
    // which no published raster answers, and the capture window's placement.
    //
    // The band is what a sync pulse is across every mode this board sees, and
    // it is the ONLY bound: nothing substitutes a value for a reading outside
    // it, because a guess that happens to suit the bench source is invisible on
    // every other mode.
    //
    // A READING OUTSIDE IT STALLS ACQUISITION rather than losing the origin
    // alone: SourceMeasurement::takeDuty() keeps dutyMeasured_ false and the
    // source never leaves Settling. So the floor has to clear the narrowest
    // raster a source may publish, not the narrowest the bench happens to send.
    bool isPulse() const;

    // CVT reduced blanking fixes hsync at 32 pixels inside 160 of blanking, so
    // its duty is 32 / (active + 160) -- 1.5% at 1920 active and 1.2% at 2560.
    // CEA-861's narrowest is 1920x1080@24 at 1.6%, and 720p60 is 2.4%. A floor
    // of 4.1% refused every one of them.
    static const uint16_t PulseFloorPerThousand = 10;
    static const uint16_t PulseCeilingPerThousand = 152;

private:
    float syncDuty_;
};

}  // namespace Tv5725

#endif  // TV5725_HSYNC_PULSE_H_
