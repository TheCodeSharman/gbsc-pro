#include "SamplingClock.h"

#include "Adc.h"
#include "InputFormatter.h"
#include "VideoSourceLine.h"

namespace Tv5725 {

const uint16_t SamplingClock::RecommendedPercent;

namespace {

// A divider held under a bound expressed in IF units. Formed wide because the
// product can exceed what a divider is allowed to hold.
uint16_t capAgainst(uint16_t divider, uint32_t unitBound, uint16_t samplesPerUnit)
{
    const uint32_t allowed = unitBound * samplesPerUnit;
    if (allowed >= Adc::DividerMax)
        return divider;
    return divider > (uint16_t)allowed ? (uint16_t)allowed : divider;
}

}  // namespace

uint16_t SamplingClock::recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                       bool lineDoubled, uint16_t maxIfLineUnits)
{
    uint16_t ceiling = Adc::maxDivider(lineRateHz, oversample);
    if (ceiling == 0)
        return 0;

    uint16_t backed = (uint16_t)(((uint32_t)ceiling * RecommendedPercent) / 100);

    // ADC samples to one IF unit, which is what turns a bound on the IF line
    // into a bound on the divider.
    const uint16_t samplesPerUnit = lineDoubled ? 2 : 1;

    // The write bound: the capture path writes from wherever the window starts,
    // so a line whose capturable span runs past that has ends no single window
    // can hold at once. ../../../docs/investigations/tail-green.md
    uint16_t backedForWrite = capAgainst(
        backed,
        maxIfLineUnits > 0 ? maxIfLineUnits : VideoSourceLine::WriteLimitUnits,
        samplesPerUnit);

    // The wall above both, and it wraps rather than failing.
    backedForWrite = capAgainst(backedForWrite, InputFormatter::LineCounterMax,
                                samplesPerUnit);

    // Even, so the line counter divides exactly. An odd divider leaves the IF
    // half a sample out from the line the ADC is delivering.
    return (uint16_t)(backedForWrite & ~1u);
}

}  // namespace Tv5725
