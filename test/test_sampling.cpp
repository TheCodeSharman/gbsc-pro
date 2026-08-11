// Host-compiled unit tests for src/tv5725/Sampling.h -- `make -C test sampling`.
//
// Pure arithmetic over the ADC front end: how finely the incoming line is
// sampled, and what the IF's own line counter must be set to as a result.
//
// Those are ONE quantity in more than one register, and moving one without the
// others is what a fault here looks like: halving the divider alone leaves
// IF_HSYNC_RST describing a line twice as long as the one arriving, which is a
// solid green display with sync still stable.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Sampling.h"

using namespace Tv5725;

// The bench: RiscPC at 320x256@50, VTOTAL 311, so 311 x 50 = 15550 lines/sec.
// PLLAD_MD 2553 and IF_HSYNC_RST 1276 are what the unit actually holds.
static const uint32_t BenchLineRate = 15550;
static const uint16_t BenchDivider = 2553;

TEST_CASE("the IF line follows the divider, because they are one quantity")
{
    // Measured on the unit: PLLAD_MD 2553, IF_HSYNC_RST 1276. The IF counts the
    // ADC line after decimation by two.
    CHECK(Sampling::ifLineFor(BenchDivider) == 1276);

    SUBCASE("and it follows a divider that changes") {
        // The whole point: an IF_HSYNC_RST that does not follow PLLAD_MD leaves
        // the IF counting to the end of a line that is not arriving.
        CHECK(Sampling::ifLineFor(1276) == 638);
        CHECK(Sampling::ifLineFor(512) == 256);
    }
}

TEST_CASE("the ADC has a rated sampling ceiling and the divider must respect it")
{
    // DS-5725-3.2: "Maximum analog sampling rate up to 162MSPS". The sample
    // clock is PLLAD_MD x line rate, and oversampling multiplies it.
    SUBCASE("the bench is inside the limit, but only just") {
        // 2553 x 15550 x 4. That is 98.0% of the 162 MSPS rating.
        CHECK(Sampling::sampleRateHz(BenchDivider, BenchLineRate, 4) == 158796600u);
        CHECK(Sampling::withinLimit(BenchDivider, BenchLineRate, 4));
    }

    SUBCASE("98% of rated is not margin, and the ceiling says so") {
        // 162e6 / (15550 x 4) = 2604. The unit sits at 2553, which is 98% of
        // the largest divider that fits at all -- somebody maximised it, and a
        // brief PLL unlock was seen on the bench.
        CHECK(Sampling::maxDivider(BenchLineRate, 4) == 2604);
    }

    SUBCASE("a higher line rate needs a smaller divider, which is why presets differ") {
        // 31.5 kHz at this divider would be 322 MSPS, twice the rating. Nothing
        // is wrong with the preset tables carrying different values; what is
        // wrong is that the value is inherited rather than derived.
        CHECK_FALSE(Sampling::withinLimit(BenchDivider, 31500, 4));
        CHECK(Sampling::maxDivider(31500, 4) == 1285);
    }

    SUBCASE("oversampling counts, because it multiplies the ADC clock") {
        // At 31.5 kHz, halving the oversample doubles the ceiling: 1285 -> 2571.
        // The bench's own line rate is too slow to show this -- at x1 and x2 the
        // arithmetic wants 10418 and 5209, and both are past the 12-bit field.
        CHECK(Sampling::maxDivider(31500, 4) == 1285);
        CHECK(Sampling::maxDivider(31500, 2) == 2571);
        CHECK(Sampling::maxDivider(31500, 1) == Sampling::DividerMax);
    }
}

TEST_CASE("the recommended divider leaves margin under the ceiling")
{
    // A ceiling is not a target. The bench ran at 98% of rated and dropped
    // lock; the recommendation backs off so drift in the source line rate does
    // not cross the limit.
    uint16_t recommended = Sampling::recommendedDivider(BenchLineRate, 4);

    CHECK(recommended < Sampling::maxDivider(BenchLineRate, 4));
    CHECK(Sampling::withinLimit(recommended, BenchLineRate, 4));

    SUBCASE("and it is even, so the IF line divides exactly") {
        // ifLineFor truncates. An odd divider puts the IF half a sample out
        // from the line the ADC is actually delivering.
        CHECK(recommended % 2 == 0);
    }
}

TEST_CASE("a divider is clamped into the field rather than wrapping")
{
    // PLLAD_MD is 12 bits. A line rate low enough to permit a bigger divider
    // than the register can hold must not wrap to a tiny one.
    CHECK(Sampling::maxDivider(1000, 1) == Sampling::DividerMax);

    SUBCASE("and a line rate too fast for any divider reports zero, not one") {
        // Zero is "no divider works here", which a caller must handle. One
        // would look like a legal setting and produce a line one sample long.
        CHECK(Sampling::maxDivider(200000000u, 4) == 0);
    }
}

TEST_CASE("a line rate of zero cannot be divided by")
{
    // getSourceFieldRate() returns 0 when there is no lock, and that reaches
    // here as a line rate. Dividing by it is the only way this arithmetic can
    // fault the firmware.
    CHECK(Sampling::maxDivider(0, 4) == 0);
    CHECK(Sampling::recommendedDivider(0, 4) == 0);
    CHECK_FALSE(Sampling::withinLimit(BenchDivider, 0, 4));
}

TEST_CASE("an oversample ratio of zero is treated as one")
{
    // ADC_CLK_ICLK1X/2X are read off the chip, and a dropped read arrives as 0.
    // Treating that as "no oversampling" keeps the ceiling honest; treating it
    // as a divisor would make the limit infinite.
    CHECK(Sampling::maxDivider(BenchLineRate, 0)
          == Sampling::maxDivider(BenchLineRate, 1));
}

// --- choosing the divider for a mode ------------------------------------------

TEST_CASE("the divider is chosen at a mode change, under the ADC ceiling")
{
    // On a mode change, never on a zoom -- the same rule PB_FETCH_NUM follows.
    // A mode change rebuilds the picture anyway; a write during a zoom
    // reprograms the sampling under a picture the user is watching.
    const uint32_t BenchLine = 15550;   // VTOTAL 311 at 50 Hz
    const uint8_t Oversample = 4;

    SUBCASE("the ADC rating is what binds, at every line rate") {
        // 15550 Hz: 162 MSPS / (15550 x 4) = 2604, and 85% of that is 2212.
        // 31500 Hz: room for 1285, and 85% is 1092.
        CHECK(Sampling::recommendedDivider(BenchLine, Oversample) == 2212);
        CHECK(Sampling::recommendedDivider(31500, Oversample) == 1092);
    }

    SUBCASE("it samples every mode that gets scaled at all") {
        // It has to resolve the widest source the scaler ever sees, and anything
        // over 535 lines is trapped in RGBHV bypass and never reaches the sampler
        // (docs/rgbhv-bypass-trap.md), so 640x512 and 800x512 are the widest that
        // matter. Active samples = divider x 0.76 x 1.04, the window PanAndZoom
        // opens on the line.
        uint16_t chosen = Sampling::recommendedDivider(BenchLine, Oversample);
        uint16_t active = (uint16_t)(chosen * 0.76f * 1.04f);
        CHECK(active >= 800);                  // 1:1 on the widest scaled mode
        CHECK(active >= 2 * 640);              // and 2x on the common one
    }

    SUBCASE("the bench divider is above what the rating recommends") {
        // The unit runs PLLAD_MD 2553 against a 2212 recommendation -- 98% of the
        // 162 MSPS rating rather than 85%, measured 2026-08-11. Recorded because
        // wiring this class in would LOWER the divider, and that is a real
        // trade against sampling headroom rather than a free fix.
        CHECK(Sampling::recommendedDivider(BenchLine, Oversample) < 2553);
        CHECK(Sampling::withinLimit(2553, BenchLine, Oversample));
        CHECK_FALSE(Sampling::withinLimit(2604 + 1, BenchLine, Oversample));
    }

    SUBCASE("a line rate nobody can measure yields nothing, not a guess") {
        // A divider written from a zero measurement is how the screen goes
        // green. Sampling has no business inventing one.
        CHECK(Sampling::recommendedDivider(0, Oversample) == 0);
    }
}
