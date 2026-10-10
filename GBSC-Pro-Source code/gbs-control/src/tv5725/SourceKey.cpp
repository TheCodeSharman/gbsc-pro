#include "SourceKey.h"

#include <math.h>

#include "VideoSignal.h"

namespace Tv5725 {

namespace {

// Mirrors VideoSignal::ratesAgree(), which asks the same question of the line
// rate rather than the field rate.
bool ratesWithinTolerance(float a, float b)
{
    const float larger = a > b ? a : b;
    const float smaller = a > b ? b : a;
    if (smaller <= 0.0f)
        return false;
    return (larger - smaller) * 1000.0f <= (float)SourceIdentityPerThousand * smaller;
}

}  // namespace

SourceKey::SourceKey()
    : lines_(0), rateHz_(0.0f), syncWidth_(0.0f),
      hsyncPolarity_(Undetermined), vsyncPolarity_(Undetermined) {}

SourceKey::SourceKey(uint16_t sourceLines, float fieldRateHz, float syncWidth,
                     Polarity hsyncPolarity, Polarity vsyncPolarity)
    : lines_(0), rateHz_(0.0f), syncWidth_(syncWidth),
      hsyncPolarity_(hsyncPolarity), vsyncPolarity_(vsyncPolarity)
{
    if (!VideoSignal::isVideo(sourceLines, fieldRateHz))
        return;
    lines_ = sourceLines;
    rateHz_ = floorf(fieldRateHz * (float)RateStepsPerHz + 0.5f)
              / (float)RateStepsPerHz;
}

bool SourceKey::valid() const { return lines_ != 0 && rateHz_ > 0.0f; }

uint16_t SourceKey::lines() const { return lines_; }

float SourceKey::rateHz() const { return rateHz_; }

float SourceKey::syncWidth() const { return syncWidth_; }

SourceKey::Polarity SourceKey::hsyncPolarity() const { return hsyncPolarity_; }

SourceKey::Polarity SourceKey::vsyncPolarity() const { return vsyncPolarity_; }

namespace {

const float Unreachable = 1.0e9f;

float fractionOfTolerance(float apart, float allowed)
{
    if (allowed <= 0.0f)
        return apart == 0.0f ? 0.0f : Unreachable;
    return apart / allowed;
}

}  // namespace

float SourceKey::distanceTo(const SourceKey &other) const
{
    if (!valid() || !other.valid() || lines_ != other.lines_
        || hsyncPolarity_ != other.hsyncPolarity_
        || vsyncPolarity_ != other.vsyncPolarity_)
        return Unreachable;

    const float smaller = rateHz_ < other.rateHz_ ? rateHz_ : other.rateHz_;
    const float rateApart = fabsf(rateHz_ - other.rateHz_) * 1000.0f;
    const float rateAllowed = (float)SourceIdentityPerThousand * smaller;

    return fractionOfTolerance(rateApart, rateAllowed)
         + fractionOfTolerance(fabsf(syncWidth_ - other.syncWidth_),
                               SyncWidthIdentity);
}

bool SourceKey::operator==(const SourceKey &other) const
{
    return valid() && other.valid()
        && lines_ == other.lines_ && ratesWithinTolerance(rateHz_, other.rateHz_)
        && fabsf(syncWidth_ - other.syncWidth_) <= SyncWidthIdentity
        && hsyncPolarity_ == other.hsyncPolarity_
        && vsyncPolarity_ == other.vsyncPolarity_;
}

bool SourceKey::operator!=(const SourceKey &other) const
{
    return !(*this == other);
}

}  // namespace Tv5725
