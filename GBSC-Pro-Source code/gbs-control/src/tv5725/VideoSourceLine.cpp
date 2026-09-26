#include "VideoSourceLine.h"

#include <math.h>

namespace Tv5725 {

const uint16_t VideoSourceLine::DoubledHeadBlankingUnits;
const uint16_t VideoSourceLine::SeparatorOriginPerMille;
const uint16_t VideoSourceLine::SeparatorFrameLeadLines;

VideoSourceLine::VideoSourceLine(uint16_t units)
    : units_(units), syncUnits_(0), headBlankingUnits_(0),
      originLeadUnits_(0) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits), headBlankingUnits_(0),
      originLeadUnits_(0) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits,
                                 uint16_t headBlankingUnits,
                                 uint16_t originLeadUnits)
    : units_(units), syncUnits_(syncUnits),
      headBlankingUnits_(headBlankingUnits),
      originLeadUnits_(originLeadUnits) {}

VideoSourceLine VideoSourceLine::frame(uint16_t units)
{
    return VideoSourceLine(units);
}

VideoSourceLine VideoSourceLine::frame(uint16_t units, uint16_t originLeadUnits)
{
    return VideoSourceLine(units, 0, 0, originLeadUnits);
}

VideoSourceLine VideoSourceLine::forDuty(uint16_t units, const HsyncPulse &pulse,
                                         bool lineDoubled, bool separated)
{
    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. HsyncPulse's ceiling keeps it
    // under a sixth of the line, so what is left is always the greater part.
    const uint16_t lead = separated
        ? (uint16_t)lrintf(units * (float)SeparatorOriginPerMille / 1000.0f)
        : 0;
    return VideoSourceLine(units, (uint16_t)ceilf(units * pulse.syncDuty()),
                           lineDoubled ? DoubledHeadBlankingUnits : 0, lead);
}

uint16_t VideoSourceLine::units() const { return units_; }

uint16_t VideoSourceLine::syncUnits() const { return syncUnits_; }

uint16_t VideoSourceLine::originLeadUnits() const { return originLeadUnits_; }

uint16_t VideoSourceLine::headBlankingUnits() const { return headBlankingUnits_; }

}  // namespace Tv5725
