#include "HsyncPulse.h"

namespace Tv5725 {

HsyncPulse::HsyncPulse() : syncDuty_(0.0f), positive_(false) {}

HsyncPulse::HsyncPulse(float syncDuty, bool positive)
    : syncDuty_(syncDuty), positive_(positive)
{
}

float HsyncPulse::syncDuty() const { return syncDuty_; }

bool HsyncPulse::positive() const { return positive_; }

bool HsyncPulse::isPulse() const
{
    const float perThousand = syncDuty_ * 1000.0f;
    return perThousand >= (float)PulseFloorPerThousand
        && perThousand <= (float)PulseCeilingPerThousand;
}

}  // namespace Tv5725
