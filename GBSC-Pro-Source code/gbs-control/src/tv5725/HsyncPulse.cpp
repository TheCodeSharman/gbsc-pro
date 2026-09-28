#include "HsyncPulse.h"

namespace Tv5725 {

HsyncPulse::HsyncPulse() : syncDuty_(0.0f) {}

HsyncPulse::HsyncPulse(float syncDuty) : syncDuty_(syncDuty) {}

float HsyncPulse::syncDuty() const { return syncDuty_; }

bool HsyncPulse::isPulse() const
{
    const float perThousand = syncDuty_ * 1000.0f;
    return perThousand >= (float)PulseFloorPerThousand
        && perThousand <= (float)PulseCeilingPerThousand;
}

}  // namespace Tv5725
