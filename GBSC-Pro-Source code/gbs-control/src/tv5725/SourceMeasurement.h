#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

// How finely the incoming line is sampled, and what the input formatter's line
// counter must be set to as a result.
//
// **PLLAD_MD, IF_HSYNC_RST and SP_RT_HS_SP are ONE quantity in THREE
// registers.** All three come off one held value; moving one without the others
// is what a fault here looks like.
//
// **THE REGISTER IS NOT THE SOURCE OF TRUTH.** PLLAD_MD is loaded into the ADC
// PLL by a rising edge on PLLAD_LAT, so between the write and the latch read()
// returns the NEW value while the ADC still clocks at the OLD one -- a solid
// green screen behind self-consistent registers, with nothing to diagnose from.
// STATUS_SYNC_PROC_HTOTAL is the one witness, counting real ADC clocks per line.
//
// So the divider is HELD and handed out, never read back. Beating against the
// source's pixel clock is not decidable here -- the chip sees sync edges, not
// pixels. This computes a ceiling and a starting point; the last word is the
// user's, from the screen.

#include <stdint.h>

namespace Tv5725 {

class SourceMeasurement {
public:
    // DS-5725-3.2, front page: "Maximum analog sampling rate up to 162MSPS".
    static const uint32_t MaxSampleRateHz = 162000000u;

    // PLLAD_MD is twelve bits.
    static const uint16_t DividerMax = 4095;

    // How far under the rating to sit, in percent. The margin is for
    // measurement jitter, not for the part: the line rate comes from a measured
    // field rate, so a low reading puts a ceiling divider above the rating. If
    // an unlock is reproduced at 98%, this is the one constant to move.
    static const uint16_t RecommendedPercent = 98;

    // The source's line rate in Hz, or 0 when the measurement is not SETTLED.
    //
    // A divider solved at the wrong rate is out by the ratio of the rates, and
    // a field rate read while the source is still settling after a preset load
    // is wrong by tens of percent. The sync processor's LINE COUNT is reliable
    // where the period measurement is not, so the count says which rate is
    // plausible and the measurement need only agree within 2%. Returning 0
    // rather than a guess lets solve() decline.
    static uint32_t lineRateFrom(uint16_t sourceLines, float fieldRateHz);

    // How far along the line the sync processor stops retiming hsync, in
    // percent. Upstream's, carried verbatim -- see retimeStopFor().
    static const uint16_t RetimeStopPercent = 93;

    // **A TEARING CEILING MUST NOT BE REINSTATED.** The band it would keep the
    // divider below does not exist: HSCALE was swept across the corrupted state
    // and no value cleared it.
    // docs/investigations/hscale-tearing-characterisation.md has the measurement.
    //
    // The ceiling recommendedDivider() does apply is the capture write limit,
    // which bounds the LINE rather than the tearing. docs/capture-limits.md

    // The IF counts the ADC line after decimation by two. Measured: PLLAD_MD
    // 2553 against IF_HSYNC_RST 1276.
    static uint16_t ifLineFor(uint16_t divider);

    // The same quantity in a THIRD register: SP_RT_HS_SP counts in ADC samples,
    // so a divider that moves without it leaves the sync processor retiming a
    // line that is not arriving. The 93% is upstream's, unexplained and
    // unmeasured here; what matters is that it follows the divider.
    static uint16_t retimeStopFor(uint16_t divider);

    // What the ADC is actually asked to do, in samples per second.
    static uint32_t sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                 uint8_t oversample);

    static bool withinLimit(uint16_t divider, uint32_t lineRateHz,
                            uint8_t oversample);

    // The largest divider this line rate can carry, or 0 if none can -- which
    // is a case the caller must handle rather than a value it can use. A line
    // rate of 0 (no lock) is also 0.
    static uint16_t maxDivider(uint32_t lineRateHz, uint8_t oversample);

    // The divider to write at a MODE CHANGE: under the ADC rating by
    // RecommendedPercent, and even, so ifLineFor() divides exactly rather than
    // truncating half a sample away. A zoom must never move it -- that would
    // resample the picture the user is watching.
    //
    // Returns 0 for an unmeasurable line rate rather than a default. A divider
    // written from a measurement that did not happen takes the sync processor
    // with it, leaving no picture to diagnose from.
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample);

    // Nothing chosen yet. A caller must be able to SEE that rather than get a
    // zero it would go on to write.
    SourceMeasurement();

    // Choose one for this line rate. False and NO state change when the rate is
    // unmeasurable, so the previous choice survives a dropped measurement --
    // the safe direction to be wrong in, since the sync watcher re-solves once
    // the source settles.
    bool solve(uint32_t lineRateHz, uint8_t oversample);

    // Take what is already in PLLAD_MD, for the two paths that compute none: a
    // CUSTOM PRESET, whose saved bytes carry a divider nothing derived, and
    // BYPASS. Both inherit by definition -- what changes is that they say so.
    void adopt();

    // The source, as the sync processor counts it. These are the only reads of
    // STATUS_SYNC_PROC_* anywhere: nothing else on the board can supply them,
    // and every other quantity the engine needs it computed itself.
    static uint16_t measureSourceLines();
    static uint16_t measureHsyncLow();

    bool usable() const;
    uint16_t divider() const;
    uint16_t ifLine() const;
    uint16_t retimeStop() const;

    // All three registers, from the one held value.
    //
    // **THIS DOES NOT LATCH, AND THE CALLER MUST RUN BEFORE ONE THAT DOES.**

private:
    uint16_t divider_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
