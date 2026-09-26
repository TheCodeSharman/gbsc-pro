#include "VideoSourceLine.h"

#include <math.h>

namespace Tv5725 {

const uint16_t VideoSourceLine::DoubledHeadBlankingUnits;

VideoSourceLine::VideoSourceLine(uint16_t units)
    : units_(units), syncUnits_(0), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits,
                                 uint16_t headBlankingUnits, bool syncAtHead)
    : units_(units), syncUnits_(syncUnits),
      headBlankingUnits_(headBlankingUnits), syncAtHead_(syncAtHead) {}

VideoSourceLine VideoSourceLine::frame(uint16_t units)
{
    return VideoSourceLine(units);
}

VideoSourceLine VideoSourceLine::frame(uint16_t units, uint16_t vsyncUnits)
{
    return VideoSourceLine(units, vsyncUnits, 0, false);
}

VideoSourceLine VideoSourceLine::forDuty(uint16_t units, const HsyncPulse &pulse,
                                         bool lineDoubled)
{
    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. HsyncPulse's ceiling keeps it
    // under a sixth of the line, so what is left is always the greater part.
    return VideoSourceLine(units, (uint16_t)ceilf(units * pulse.syncDuty()),
                           lineDoubled ? DoubledHeadBlankingUnits : 0, true);
}

uint16_t VideoSourceLine::units() const { return units_; }

uint16_t VideoSourceLine::syncUnits() const { return syncUnits_; }

bool VideoSourceLine::syncAtHead() const { return syncAtHead_; }

uint16_t VideoSourceLine::headBlankingUnits() const { return headBlankingUnits_; }

}  // namespace Tv5725
