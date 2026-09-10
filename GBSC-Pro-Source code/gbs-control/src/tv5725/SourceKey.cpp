#include "SourceKey.h"

#include <math.h>

#include "SourceMeasurement.h"

namespace Tv5725 {

const float RateToleranceHz = 2.0f;

SourceKey::SourceKey() : lines_(0), rateHz_(0.0f) {}

SourceKey::SourceKey(uint16_t sourceLines, float fieldRateHz)
    : lines_(0), rateHz_(0.0f)
{
    // The one owner of both bounds already, on the count and the rate together.
    if (SourceMeasurement::lineRateFrom(sourceLines, fieldRateHz) == 0)
        return;
    lines_ = sourceLines;
    rateHz_ = fieldRateHz;
}

bool SourceKey::valid() const { return lines_ != 0 && rateHz_ > 0.0f; }

uint16_t SourceKey::lines() const { return lines_; }

float SourceKey::rateHz() const { return rateHz_; }

bool SourceKey::operator==(const SourceKey &other) const
{
    return valid() && other.valid()
        && lines_ == other.lines_
        && fabsf(rateHz_ - other.rateHz_) <= RateToleranceHz;
}

bool SourceKey::operator!=(const SourceKey &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
