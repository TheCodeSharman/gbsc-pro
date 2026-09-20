#ifndef TV5725_SAMPLING_CLOCK_H_
#define TV5725_SAMPLING_CLOCK_H_

#include <stdint.h>

namespace Tv5725 {

// Chooses how finely the incoming line is sampled.
//
// Three blocks bound the divider and none of them owns the answer: the ADC's
// rating and the width of PLLAD_MD, the input formatter's line counter, and how
// much of a line the capture path writes. So the choice is made here and handed
// out, rather than belonging to any one of them.
class SamplingClock {
public:
    // How far under the ADC's rating to sit, in percent. The margin is for
    // measurement jitter, not for the part: the line rate comes from a measured
    // field rate, so a low reading puts a ceiling divider above the rating. If
    // an unlock is reproduced at 98%, this is the one constant to move.
    static const uint16_t RecommendedPercent = 98;

    // The longest line, in ADC samples, that a doubled mode may ask for. Past
    // the onset below the capture path holds dark green, and picture reaching
    // it is destroyed rather than overlaid, so the divider is held under it and
    // the whole line arrives.
    //
    // The onset measures 2236..2256, the spread being where the band's edge is
    // read on a photographed gradient rather than any movement of it. This sits
    // under the low end because the onset is one board at one divider: the
    // margin is for that, not for the part.
    //
    // Measured only on a doubled line because only there can the divider reach
    // it -- an undoubled line of 1561 samples shows no band.
    // ../../../docs/investigations/tail-green.md
    static const uint16_t DoubledLineSampleLimit = 2200;

    // The divider to write at a MODE CHANGE: the samples per line the scan mode
    // says the source is short of, under RecommendedPercent of the crossover
    // row's ceiling, inside what the line counter can hold, and even so the
    // line counter divides exactly rather than truncating half a sample away.
    // A zoom must never move it -- that would resample the picture the user is
    // watching.
    //
    // One rule for both scan modes: the kept count is maximised and a ratio is
    // taken only where it costs none of it. The doubled line used to be chosen
    // on what it spent of the ADC's rating instead, which stood in for the tail
    // band until the band was measured; with DoubledLineSampleLimit holding the
    // line off it, preferring the spend buys a ratio by halving the count.
    // ../../../docs/sampling-table.md
    //
    // Returns 0 for an unmeasurable line rate rather than a default. A divider
    // written from a measurement that did not happen takes the sync processor
    // with it, leaving no picture to diagnose from.
    // **A TEARING CEILING MUST NOT BE REINSTATED.** The band it would keep the
    // divider below does not exist: HSCALE was swept across the corrupted state
    // and no value cleared it.
    // ../../../docs/investigations/hscale-tearing-characterisation.md
    // `dividerCeiling` is the largest divider whose LINE the output raster can
    // show, or 0 where the caller has no raster to bound it with. Samples past
    // that are cropped, clipped or minified away whichever mechanism handles
    // them -- the VDS magnifies and cannot minify -- while costing ADC clock a
    // lower divider would spend on oversampling instead. It is not circular:
    // the raster is the display clock over the output lines and the field rate,
    // and the display clock is the Si5351's, not the ADC PLL's.
    // ../../../docs/investigations/the-capture-may-not-outgrow-the-raster.md
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                       bool lineDoubled, uint16_t dividerCeiling = 0);
};

}  // namespace Tv5725

#endif  // TV5725_SAMPLING_CLOCK_H_
