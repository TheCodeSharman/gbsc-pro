#include "SourceKey.h"

#include <math.h>

#include "SourceMeasurement.h"

namespace Tv5725 {

const float RateBucketHz = 1.0f;

SourceKey::SourceKey() : lines_(0), rateBucket_(0) {}

SourceKey::SourceKey(uint16_t sourceLines, float fieldRateHz)
    : lines_(0), rateBucket_(0)
{
    // The one owner of both bounds already, on the count and the rate together.
    if (SourceMeasurement::lineRateFrom(sourceLines, fieldRateHz) == 0)
        return;
    lines_ = sourceLines;
    rateBucket_ = (uint16_t)lrintf(fieldRateHz / RateBucketHz);
}

bool SourceKey::valid() const { return lines_ != 0 && rateBucket_ != 0; }

uint16_t SourceKey::lines() const { return lines_; }

uint16_t SourceKey::rateBucket() const { return rateBucket_; }

bool SourceKey::operator==(const SourceKey &other) const
{
    return valid() && other.valid()
        && lines_ == other.lines_ && rateBucket_ == other.rateBucket_;
}

bool SourceKey::operator!=(const SourceKey &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
