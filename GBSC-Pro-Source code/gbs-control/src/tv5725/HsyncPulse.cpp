#include "HsyncPulse.h"

namespace Tv5725 {

HsyncPulse::HsyncPulse() : syncDuty_(0.0f) {}

HsyncPulse::HsyncPulse(float syncDuty) : syncDuty_(syncDuty) {}

float HsyncPulse::syncDuty() const { return syncDuty_; }

bool HsyncPulse::isPulse() const
{
    const float perMille = syncDuty_ * 1000.0f;
    return perMille >= (float)PulseFloorPerMille
        && perMille <= (float)PulseCeilingPerMille;
}

}  // namespace Tv5725
