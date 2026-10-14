#include "HsyncPulse.h"

#include <math.h>

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

bool HsyncPulse::agrees(const HsyncPulse &other) const
{
    return fabsf(syncDuty_ - other.syncDuty_) <= SyncWidthIdentity;
}

}  // namespace Tv5725
