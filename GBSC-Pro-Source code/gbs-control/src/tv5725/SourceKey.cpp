#include "SourceKey.h"

#include <math.h>

#include "VideoSignal.h"

namespace Tv5725 {

const uint16_t RateTolerancePerMille = VideoSignal::RateTolerancePerMille;

// Identity may be wider than movement but never narrower. See SourceKey.h.
static_assert(RateTolerancePerMille >= VideoSignal::RateTolerancePerMille,
              "a rate change that moves the key must also arm a mode change");

namespace {

// Mirrors VideoSignal::ratesAgree(), which asks the same question of the line
// rate rather than the field rate.
bool ratesWithinTolerance(float a, float b)
{
    const float larger = a > b ? a : b;
    const float smaller = a > b ? b : a;
    if (smaller <= 0.0f)
        return false;
    return (larger - smaller) * 1000.0f <= (float)RateTolerancePerMille * smaller;
}

}  // namespace

SourceKey::SourceKey() : lines_(0), rateHz_(0.0f) {}

SourceKey::SourceKey(uint16_t sourceLines, float fieldRateHz)
    : lines_(0), rateHz_(0.0f)
{
    if (!VideoSignal::isVideo(sourceLines, fieldRateHz))
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
        && lines_ == other.lines_ && ratesWithinTolerance(rateHz_, other.rateHz_);
}

bool SourceKey::operator!=(const SourceKey &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
