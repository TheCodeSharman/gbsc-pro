#include "SamplingClock.h"

#include "Adc.h"
#include "InputFormatter.h"

namespace Tv5725 {

const uint16_t SamplingClock::RecommendedPercent;
const uint16_t SamplingClock::DoubledLineSampleLimit;

uint16_t SamplingClock::recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                           bool lineDoubled, uint16_t dividerCeiling)
{
    if (lineRateHz == 0)
        return 0;

    // ADC samples to one IF unit, which is what turns the line counter's own
    // bound into a bound on the divider.
    const uint16_t samplesPerUnit = lineDoubled ? 2 : 1;
    const uint32_t counterBound =
        (uint32_t)InputFormatter::LineCounterMax * samplesPerUnit;

    uint32_t chosen = 0;
    for (uint8_t ratio = oversample < 1 ? 1 : oversample; ratio != 0;
         ratio = (uint8_t)(ratio / 2)) {
        // The backoff belongs to the ADC's RATING and to nothing else: the
        // margin is for a mis-measured line rate putting the divider above what
        // the part is rated for. The bounds below are exact -- a hardware
        // counter and a solved raster -- so backing off from them throws away
        // samples for a jitter that cannot reach them.
        uint32_t ceiling = Adc::maxCkoFor(ratio) / lineRateHz;
        if (ceiling > Adc::DividerMax)
            ceiling = Adc::DividerMax;
        ceiling = (ceiling * RecommendedPercent) / 100;

        if (ceiling > counterBound)
            ceiling = counterBound;
        // The line must END before the capture path stops writing video, so
        // this bounds the LINE rather than the window: a window kept short of
        // the onset would still lose the picture past it.
        if (lineDoubled && ceiling > DoubledLineSampleLimit)
            ceiling = DoubledLineSampleLimit;
        // Applied per ratio rather than to the answer, so a line the output
        // cannot show is never the reason a ratio looks best: bounded, a lower
        // divider can afford an oversampling row the unbounded one could not.
        if (dividerCeiling != 0 && ceiling > dividerCeiling)
            ceiling = dividerCeiling;
        if (ceiling == 0)
            continue;

        // The kept count is the requirement and the oversampling is the bonus.
        // Nyquist is a property of the CONVERSION rate -- the ADC converts
        // `ratio` samples for every one the decimator passes, and a doubled
        // line's own decimation is a second factor of two -- so oversampling
        // buys freedom from aliasing and buys no resolution at all. Where it is
        // free it is taken, because the ratios are walked highest first and only
        // a STRICTLY larger ceiling displaces one.
        //
        // Where it is not free it is declined, and the asymmetry is why: a
        // source that could have been oversampled and was not aliases the top of
        // its band, while a source starved of samples to pay for oversampling
        // loses resolution outright and no filtering brings it back. 1080p is
        // the case -- 2006 kept unoversampled against 1160 kept at two times,
        // for a line neither configuration can carry whole.
        // ../../../docs/sampling-table.md
        if (ceiling > chosen)
            chosen = ceiling;
    }

    // Even, so the line counter divides exactly. An odd divider leaves the IF
    // half a sample out from the line the ADC is delivering.
    return (uint16_t)(chosen & ~1u);
}

}  // namespace Tv5725
