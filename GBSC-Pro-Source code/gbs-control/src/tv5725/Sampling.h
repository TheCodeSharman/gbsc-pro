#ifndef TV5725_SAMPLING_H_
#define TV5725_SAMPLING_H_

// How finely the incoming line is sampled, and what the input formatter's line
// counter must be set to as a result.
//
// **PLLAD_MD and IF_HSYNC_RST are ONE quantity written to TWO registers**, and
// every fault here has been something moving one without the other. The sketch
// writes PLLAD_MD from six places; the invariant IF_HSYNC_RST = PLLAD_MD / 2
// is maintained in exactly one (gbs-control.ino:8524). Halving the divider by
// hand on 2026-08-09 left the IF counting to the end of a line twice as long as
// the one arriving and the display went solid green -- CLAUDE.md records that
// as "sync stability does not mean the divider is right", which is true, and
// the missing second write is why.
//
// That is the same defect that IF_LINE_ST/SP had until the engine took both
// ends of it, and the same fix: one owner, recomputed on every solve.
//
// Static because there is no state -- a divider is a function of the line rate
// and nothing else. See CODING_STYLE.md: a class for the namespace and the
// grouping, not for ceremony.
//
// **What this CANNOT decide.** The sample clock also wants to avoid beating
// with the source's pixel clock, and that clock is unknowable: the chip sees
// sync edges, not pixels, so 320x256 and 640x256 are indistinguishable
// (CLAUDE.md). TestPat.bas exists partly for this -- its one-pixel grating is
// the only instrument for it, and "moire, beating or a flat grey wash" is the
// reading. So this computes a CEILING and a starting point; the last word is
// the user's, from the screen.

#include <stdint.h>

namespace Tv5725 {

class Sampling {
public:
    // DS-5725-3.2, front page: "Maximum analog sampling rate up to 162MSPS".
    static const uint32_t MaxSampleRateHz = 162000000u;

    // PLLAD_MD is twelve bits.
    static const uint16_t DividerMax = 4095;

    // How far under the rating to sit, in percent. The bench ran at 98% and a
    // brief PLL unlock was seen; a ceiling is not a target.
    static const uint16_t RecommendedPercent = 85;

    // The divider above which the memory-bus beat becomes reachable by zooming.
    //
    // **DO NOT ADD A CEILING ON PLLAD_MD.** Memory::fetchFor sizes the fetch
    // from the capture width, which cancels HSCALE out of the beat quantity, so
    // there is no tearing band left to keep the divider below -- the bench ran
    // 2553 clean at four HSCALE values.
    // docs/investigations/hscale-tearing-characterisation.md has the measurement.

    // The IF counts the ADC line after decimation by two. Measured: PLLAD_MD
    // 2553 against IF_HSYNC_RST 1276.
    static uint16_t ifLineFor(uint16_t divider);

    // What the ADC is actually asked to do, in samples per second.
    static uint32_t sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                 uint8_t oversample);

    static bool withinLimit(uint16_t divider, uint32_t lineRateHz,
                            uint8_t oversample);

    // The largest divider this line rate can carry, or 0 if none can -- which
    // is a case the caller must handle rather than a value it can use. A line
    // rate of 0 (no lock) is also 0.
    static uint16_t maxDivider(uint32_t lineRateHz, uint8_t oversample);

    // Where to start: under the ceiling by RecommendedPercent, and even, so
    // ifLineFor() divides exactly rather than truncating half a sample away.
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample);
};

}  // namespace Tv5725

#endif  // TV5725_SAMPLING_H_
