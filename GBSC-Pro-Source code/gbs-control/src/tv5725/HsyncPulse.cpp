#include "HsyncPulse.h"

namespace Tv5725 {

HsyncPulse::HsyncPulse() : syncDuty_(0.0f), syncAtHead_(false) {}

HsyncPulse::HsyncPulse(float syncDuty, bool syncAtHead)
    : syncDuty_(syncDuty), syncAtHead_(syncAtHead) {}

float HsyncPulse::syncDuty() const { return syncDuty_; }

bool HsyncPulse::syncAtHead() const { return syncAtHead_; }

}  // namespace Tv5725
