#include "RateAgreement.h"

namespace Clock {

constexpr float RateAgreement::RelativeTolerance;
constexpr float RateAgreement::AbsoluteToleranceHz;

bool RateAgreement::agree(float oneHz, float otherHz)
{
    const float smaller = oneHz < otherHz ? oneHz : otherHz;
    if (!(smaller > 0.0f))
        return false;

    const float difference = oneHz > otherHz ? oneHz - otherHz : otherHz - oneHz;
    const float relative = difference / smaller;
    if (relative != relative)
        return false;

    return difference <= AbsoluteToleranceHz && relative <= RelativeTolerance;
}

}  // namespace Clock
