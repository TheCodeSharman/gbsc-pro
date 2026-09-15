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

    // The divider to write at a MODE CHANGE: under the rating by
    // RecommendedPercent, inside what the line counter can hold, inside the
    // capture write limit, and even so the line counter divides exactly rather
    // than truncating half a sample away. A zoom must never move it -- that
    // would resample the picture the user is watching.
    //
    // `maxIfLineUnits` is the longest line one window may span end to end.
    // Zero falls back to the write limit, for a caller with no measurement to
    // compute it from. ../../../docs/capture-limits.md
    //
    // Returns 0 for an unmeasurable line rate rather than a default. A divider
    // written from a measurement that did not happen takes the sync processor
    // with it, leaving no picture to diagnose from.
    // **A TEARING CEILING MUST NOT BE REINSTATED.** The band it would keep the
    // divider below does not exist: HSCALE was swept across the corrupted state
    // and no value cleared it. The only ceiling applied here is the capture
    // write limit, which bounds the LINE rather than the tearing.
    // ../../../docs/investigations/hscale-tearing-characterisation.md
    // ../../../docs/capture-limits.md
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                       bool lineDoubled,
                                       uint16_t maxIfLineUnits = 0);
};

}  // namespace Tv5725

#endif  // TV5725_SAMPLING_CLOCK_H_
