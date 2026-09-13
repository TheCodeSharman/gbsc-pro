#include "SourceReading.h"

namespace Tv5725 {

SourceReading::SourceReading() : syncDuty_(0.0f), syncAtHead_(false) {}

SourceReading::SourceReading(float syncDuty, bool syncAtHead)
    : syncDuty_(syncDuty), syncAtHead_(syncAtHead) {}

float SourceReading::syncDuty() const { return syncDuty_; }

bool SourceReading::syncAtHead() const { return syncAtHead_; }

}  // namespace Tv5725
