// Host-compiled unit tests for src/tv5725/SourceMeasurement.h -- `make -C test sampling`.
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

#include <string>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"

using namespace Tv5725;

// The two the sketch supplies. getSourceFieldRate() spins on the board, which
// is why it is injected rather than called; tv5725Log() reaches the web console
// there and a buffer here, so the diagnostic is assertable.
static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }

static std::string g_log;
void tv5725Log(const char *message) { g_log = message; }

static void seedSourceLines(uint16_t lines)
{
    Wire.reset();
    Wire.bank[0][0x1B] = (uint8_t)(lines & 0xFF);
    Wire.bank[0][0x1C] = (uint8_t)((lines >> 8) & 0x07);
    g_log.clear();
}

// The bench: RiscPC at 320x256@50, VTOTAL 311, so 311 x 50 = 15550 lines/sec.
// PLLAD_MD 2553 and IF_HSYNC_RST 1276 are what the unit actually holds.
static const uint32_t BenchLineRate = 15550;
static const uint16_t BenchDivider = 2553;

TEST_CASE("the IF line follows the divider, because they are one quantity")
{
    // Measured on the unit: PLLAD_MD 2553, IF_HSYNC_RST 1276. The IF counts the
    // ADC line after decimation by two.
    CHECK(SourceMeasurement::ifLineFor(BenchDivider) == 1276);

    SUBCASE("and it follows a divider that changes") {
        // The whole point: an IF_HSYNC_RST that does not follow PLLAD_MD leaves
        // the IF counting to the end of a line that is not arriving.
        CHECK(SourceMeasurement::ifLineFor(1276) == 638);
        CHECK(SourceMeasurement::ifLineFor(512) == 256);
    }
}

TEST_CASE("the ADC has a rated sampling ceiling and the divider must respect it")
{
    // DS-5725-3.2: "Maximum analog sampling rate up to 162MSPS". The sample
    // clock is PLLAD_MD x line rate, and oversampling multiplies it.
    SUBCASE("the bench is inside the limit, but only just") {
        // 2553 x 15550 x 4. That is 98.0% of the 162 MSPS rating.
        CHECK(SourceMeasurement::sampleRateHz(BenchDivider, BenchLineRate, 4) == 158796600u);
        CHECK(SourceMeasurement::withinLimit(BenchDivider, BenchLineRate, 4));
    }

    SUBCASE("98% of rated is not margin, and the ceiling says so") {
        // 162e6 / (15550 x 4) = 2604. The unit sits at 2553, which is 98% of
        // the largest divider that fits at all -- somebody maximised it, and a
        // brief PLL unlock was seen on the bench.
        CHECK(SourceMeasurement::maxDivider(BenchLineRate, 4) == 2604);
    }

    SUBCASE("a higher line rate needs a smaller divider, which is why presets differ") {
        // 31.5 kHz at this divider would be 322 MSPS, twice the rating. Nothing
        // is wrong with the preset tables carrying different values; what is
        // wrong is that the value is inherited rather than derived.
        CHECK_FALSE(SourceMeasurement::withinLimit(BenchDivider, 31500, 4));
        CHECK(SourceMeasurement::maxDivider(31500, 4) == 1285);
    }

    SUBCASE("oversampling counts, because it multiplies the ADC clock") {
        // At 31.5 kHz, halving the oversample doubles the ceiling: 1285 -> 2571.
        // The bench's own line rate is too slow to show this -- at x1 and x2 the
        // arithmetic wants 10418 and 5209, and both are past the 12-bit field.
        CHECK(SourceMeasurement::maxDivider(31500, 4) == 1285);
        CHECK(SourceMeasurement::maxDivider(31500, 2) == 2571);
        CHECK(SourceMeasurement::maxDivider(31500, 1) == SourceMeasurement::DividerMax);
    }
}

TEST_CASE("the recommended divider leaves margin under the ceiling")
{
    // A ceiling is not a target. The bench ran at 98% of rated and dropped
    // lock; the recommendation backs off so drift in the source line rate does
    // not cross the limit.
    uint16_t recommended = SourceMeasurement::recommendedDivider(BenchLineRate, 4);

    CHECK(recommended < SourceMeasurement::maxDivider(BenchLineRate, 4));
    CHECK(SourceMeasurement::withinLimit(recommended, BenchLineRate, 4));

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
    CHECK(SourceMeasurement::maxDivider(1000, 1) == SourceMeasurement::DividerMax);

    SUBCASE("and a line rate too fast for any divider reports zero, not one") {
        // Zero is "no divider works here", which a caller must handle. One
        // would look like a legal setting and produce a line one sample long.
        CHECK(SourceMeasurement::maxDivider(200000000u, 4) == 0);
    }
}

TEST_CASE("a line rate of zero cannot be divided by")
{
    // getSourceFieldRate() returns 0 when there is no lock, and that reaches
    // here as a line rate. Dividing by it is the only way this arithmetic can
    // fault the firmware.
    CHECK(SourceMeasurement::maxDivider(0, 4) == 0);
    CHECK(SourceMeasurement::recommendedDivider(0, 4) == 0);
    CHECK_FALSE(SourceMeasurement::withinLimit(BenchDivider, 0, 4));
}

TEST_CASE("an oversample ratio of zero is treated as one")
{
    // ADC_CLK_ICLK1X/2X are read off the chip, and a dropped read arrives as 0.
    // Treating that as "no oversampling" keeps the ceiling honest; treating it
    // as a divisor would make the limit infinite.
    CHECK(SourceMeasurement::maxDivider(BenchLineRate, 0)
          == SourceMeasurement::maxDivider(BenchLineRate, 1));
}

// --- choosing the divider for a mode ------------------------------------------

TEST_CASE("the divider is chosen at a mode change, under the ADC ceiling")
{
    // On a mode change, never on a zoom -- the same rule PB_FETCH_NUM follows.
    // A mode change rebuilds the picture anyway; a write during a zoom
    // reprograms the sampling under a picture the user is watching.
    const uint32_t BenchLine = 15550;   // VTOTAL 311 at 50 Hz
    const uint8_t Oversample = 4;

    SUBCASE("whichever of the two ceilings is tighter is what binds") {
        // 15550 Hz: 162 MSPS / (15550 x 4) = 2604, 98% of that is 2550, and the
        // write limit takes it down to 2250. 31500 Hz: room for 1285, 98% is
        // 1258, and the write limit is nowhere near it.
        CHECK(SourceMeasurement::recommendedDivider(BenchLine, Oversample) == 2250);
        CHECK(SourceMeasurement::recommendedDivider(31500, Oversample) == 1258);
    }

    SUBCASE("it samples every mode that gets scaled at all") {
        // It has to resolve the widest source the scaler ever sees, and anything
        // over 535 lines is trapped in RGBHV bypass and never reaches the sampler
        // (docs/rgbhv-bypass-trap.md), so 640x512 and 800x512 are the widest that
        // matter. Active samples = divider x 0.76 x 1.04, the window PanAndZoom
        // opens on the line.
        uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLine, Oversample);
        uint16_t active = (uint16_t)(chosen * 0.76f * 1.04f);
        CHECK(active >= 800);                  // 1:1 on the widest scaled mode
        CHECK(active >= 2 * 640);              // and 2x on the common one
    }

    SUBCASE("the backoff under the rating is 2 percent, not a derating") {
        // 98 rather than 85. The twelve tables ran 2269..2559 on this part for
        // years, 87..98% of the 162 MSPS rating; computing 85% of the ceiling
        // throws away 13% of the horizontal input samples for headroom nothing
        // asked for. The 2% is jitter: the line rate comes from a MEASURED
        // field rate, and a reading that comes in low would put a divider
        // computed at the ceiling over the rating.
        //
        // Read at 31.5 kHz, the rate where the rating is the binding ceiling.
        uint16_t chosen = SourceMeasurement::recommendedDivider(31500, Oversample);
        CHECK(chosen > (uint16_t)(SourceMeasurement::maxDivider(31500, Oversample) * 0.97f));
        CHECK(SourceMeasurement::withinLimit(chosen, 31500, Oversample));
        CHECK_FALSE(SourceMeasurement::withinLimit(1285 + 1, 31500, Oversample));
    }

    SUBCASE("and at the bench rate it now sits below every shipped table") {
        // 2250 against 2269..2559. SourceMeasurement the line more coarsely is what
        // buys capturing the whole of it, and no table's divider does.
        uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLine, Oversample);
        CHECK(chosen < 2269);
        CHECK(SourceMeasurement::ifLineFor(chosen) <= InputLine::WriteLimitUnits);
        CHECK(SourceMeasurement::withinLimit(chosen, BenchLine, Oversample));
    }

    SUBCASE("a line rate nobody can measure yields nothing, not a guess") {
        // A divider written from a zero measurement is how the screen goes
        // green. SourceMeasurement has no business inventing one.
        CHECK(SourceMeasurement::recommendedDivider(0, Oversample) == 0);
    }
}

TEST_CASE("the divider is capped so the whole line stays inside the write limit")
{
    // Past InputLine::WriteLimitUnits the capture path stops writing video, so
    // a line longer than that loses its tail whatever the ADC rating allows.
    // The divider is what decides the line length, which makes it the lever:
    // sample the line more coarsely and 2250 samples reach the end of it.
    // docs/capture-limits.md
    uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLineRate, 4);

    CHECK(SourceMeasurement::ifLineFor(chosen) <= InputLine::WriteLimitUnits);

    SUBCASE("and the ADC rating still binds where it is the tighter of the two") {
        // 31.5 kHz has room for 1258 under the rating, well inside the limit.
        CHECK(SourceMeasurement::recommendedDivider(31500, 4) == 1258);
    }

    SUBCASE("it is still even, so the IF line divides exactly") {
        CHECK(chosen % 2 == 0);
    }
}

TEST_CASE("the sync processor's retime window is the divider a third time")
{
    // SP_RT_HS_SP is the third register holding this quantity: the stop of the
    // sync processor's retiming window, in the same ADC samples PLLAD_MD divides
    // the line into. Measured on the unit, which holds 2553 and 2374.
    CHECK(SourceMeasurement::retimeStopFor(BenchDivider) == 2374);

    SUBCASE("and it follows a divider that changes") {
        // 2212 is what recommendedDivider() asks for at the bench line rate.
        // Leaving 2374 behind would put the stop past the end of the line.
        CHECK(SourceMeasurement::retimeStopFor(2212) == 2057);
        CHECK(SourceMeasurement::retimeStopFor(1276) == 1186);
    }

    SUBCASE("it is integer arithmetic, and agrees with the float it replaces") {
        // The sketch wrote `PLLAD_MD::read() * 0.93f` and truncated. The ESP8266
        // has no FPU and this runs on every solve; the two must not disagree by
        // a sample, so every legal divider is checked rather than a sample of
        // them.
        for (uint32_t d = 0; d <= SourceMeasurement::DividerMax; d++) {
            CHECK(SourceMeasurement::retimeStopFor((uint16_t)d) == (uint16_t)(d * 0.93f));
        }
    }
}

// --- the divider as STATE, not as a register read back -------------------
//
// The registers cannot be the source of truth here: PLLAD_MD is loaded into the
// ADC PLL by a rising edge on PLLAD_LAT, so between a write and that latch a
// read-back reports what was WRITTEN, never what the chip is DOING. PLLAD_MD
// reading 2210 against a PLL running 2553, with IF_HSYNC_RST sized for 2210, is
// a solid green screen and every register self-consistent. So SourceMeasurement holds the
// number it chose and hands it to whoever needs it.

TEST_CASE("a solved divider is held, and every register follows from it")
{
    Wire.reset();
    SourceMeasurement sampling;

    // Nothing chosen yet is a state a caller must be able to see, not a zero it
    // silently writes.
    CHECK_FALSE(sampling.usable());

    REQUIRE(sampling.solve(BenchLineRate, 4));
    CHECK(sampling.usable());

    const uint16_t chosen = sampling.divider();
    CHECK(chosen == SourceMeasurement::recommendedDivider(BenchLineRate, 4));

    SUBCASE("the derived values come from the held divider") {
        CHECK(sampling.ifLine() == SourceMeasurement::ifLineFor(chosen));
        CHECK(sampling.retimeStop() == SourceMeasurement::retimeStopFor(chosen));
    }
}

TEST_CASE("an unmeasurable line rate leaves the previous choice alone")
{
    Wire.reset();
    SourceMeasurement sampling;
    REQUIRE(sampling.solve(BenchLineRate, 4));
    const uint16_t chosen = sampling.divider();

    // getSourceFieldRate() reports 0 with no lock, and that reaches here. A
    // divider written from a measurement that did not happen is how the screen
    // goes green -- and it takes the sync processor with it, so there is no
    // picture left to diagnose from.
    CHECK_FALSE(sampling.solve(0, 4));
    CHECK(sampling.divider() == chosen);
    CHECK(sampling.usable());
}

TEST_CASE("adopt() is how a path that does not compute says so out loud")
{
    Wire.reset();
    SourceMeasurement sampling;

    // A custom preset is a register dump replayed off the filesystem and bypass
    // routes around the VDS, so neither has a computed divider and both must
    // INHERIT one. The point is that they name the act, where a silent read-back
    // inside readRasters() would be the same inheritance unaccounted for.
    Wire.bank[5][0x12] = 0xF9;   // 2553, as the bench table ships
    Wire.bank[5][0x13] = 0x09;

    sampling.adopt();
    CHECK(sampling.usable());
    CHECK(sampling.divider() == 2553);
}

// --- the line rate has to be SETTLED, not merely in range --------------------

TEST_CASE("a field rate that disagrees with the line count is refused")
{
    // A source locked at 311 lines / 50.08 Hz yields PLLAD_MD 2204 against the
    // 2548 due when the solve sees the ~57.9 Hz transient a preset load leaves,
    // which a bare 40..100 Hz check passes. The line count is reliable where the
    // period measurement is not, so it says which rate is plausible.
    // solveRaster() refuses the same way.
    CHECK(SourceMeasurement::lineRateFrom(311, 50.08f) == 15574u);

    SUBCASE("the bench transient that actually caused it") {
        CHECK(SourceMeasurement::lineRateFrom(311, 57.9f) == 0u);
    }

    SUBCASE("a PAL-like line count wants a PAL-like rate") {
        CHECK(SourceMeasurement::lineRateFrom(311, 60.0f) == 0u);
        CHECK(SourceMeasurement::lineRateFrom(312, 50.0f) != 0u);
    }

    SUBCASE("an NTSC-like line count wants an NTSC-like rate") {
        CHECK(SourceMeasurement::lineRateFrom(262, 50.0f) == 0u);
        CHECK(SourceMeasurement::lineRateFrom(262, 59.94f) != 0u);
    }

    SUBCASE("the 97/98 a preset load leaves behind is refused outright") {
        // Documented as normal for a moment after a load, and the reason
        // solveRaster() defers rather than solving. Below SourceVerticalTotalMin, so
        // it never reaches the rate check at all.
        CHECK(SourceMeasurement::lineRateFrom(97, 50.0f) == 0u);
        CHECK(SourceMeasurement::lineRateFrom(98, 50.0f) == 0u);
    }

    SUBCASE("2% either side is accepted, beyond it is not") {
        CHECK(SourceMeasurement::lineRateFrom(311, 50.9f) != 0u);
        CHECK(SourceMeasurement::lineRateFrom(311, 51.5f) == 0u);
    }
}

// --- the line rate, measured off the chip ------------------------------------

TEST_CASE("the line rate is measured rather than handed in")
{
    // Field rate x source lines, the two quantities the divider is a function
    // of, both read where they live rather than passed down from the sketch.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement;
    CHECK(measurement.measureLineRate());
    CHECK(measurement.sourceLines() == 311);
    CHECK(measurement.lineRateHz() == 15574u);

    SUBCASE("and it holds both inputs, because nothing downstream can say which was wrong") {
        CHECK(measurement.fieldRateHz() > 50.0f);
        CHECK(measurement.fieldRateHz() < 50.1f);
    }

    SUBCASE("the diagnostic names both inputs and the result") {
        CHECK(g_log == "sampling: 311 lines x 50.08 Hz -> line rate 15574");
    }
}

TEST_CASE("a rate that disagrees with the line count is refused, and says so")
{
    // A field rate measured while the source is still settling passes a plain
    // bounds check comfortably -- 57.9 Hz against a real 50.08 -- and the
    // divider comes out proportionally wrong.
    seedSourceLines(311);
    g_fieldRate = 57.9f;

    SourceMeasurement measurement;
    CHECK_FALSE(measurement.measureLineRate());
    CHECK(measurement.lineRateHz() == 0u);

    SUBCASE("and reports what it saw, both halves") {
        CHECK(measurement.sourceLines() == 311);
        CHECK(g_log == "sampling: 311 lines x 57.90 Hz -> line rate 0");
    }

    SUBCASE("an unmeasurable field rate is refused the same way") {
        g_fieldRate = 0.0f;
        CHECK_FALSE(measurement.measureLineRate());
        CHECK(measurement.lineRateHz() == 0u);
    }

    SUBCASE("so is the 97 lines a preset load leaves behind") {
        seedSourceLines(97);
        g_fieldRate = 50.08f;
        CHECK_FALSE(measurement.measureLineRate());
    }
}

// --- the cheap gate before the expensive measurement -------------------------

TEST_CASE("the line count has to hold still before the field rate is worth paying for")
{
    // ONE register read per sample, so a caller can ask on every pass.
    // measureLineRate() cannot be asked speculatively: getSourceFieldRate()
    // spins for up to 250 ms a pulse.
    seedSourceLines(311);
    SourceMeasurement measurement;

    for (uint8_t i = 1; i < SourceMeasurement::SteadySamples; ++i) {
        CAPTURE(i);
        CHECK_FALSE(measurement.sampleSteady());
    }
    CHECK(measurement.sampleSteady());

    SUBCASE("and stays steady while the count does") {
        CHECK(measurement.sampleSteady());
    }
}

TEST_CASE("a count that moves starts the run again")
{
    seedSourceLines(311);
    SourceMeasurement measurement;
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();
    REQUIRE(measurement.sampleSteady());

    // Mid-change. A one-sample blip is normal; what matters is that it does not
    // report steady on the strength of the run before it.
    seedSourceLines(97);
    CHECK_FALSE(measurement.sampleSteady());
}

TEST_CASE("the field rate has to REPEAT before anything is sized from it")
{
    // Measured on the bench: settled, getSourceFieldRate() returns 50.08 Hz on
    // eighty consecutive readings. Taken across a preset load it lands anywhere
    // in 49.92..50.26 -- 0.35% out, and lineRateFrom()'s 2% cross-check passes
    // every one of them, because that band is a gross-error net rather than a
    // precision gate.
    //
    // The raster is where it costs: horizontalTotal = clock / rate / lines, so
    // 0.35% off the rate is 0.35% off the line, and nothing re-solves it. Three
    // preset loads in four came out at 1923 or 1910 against the 1916 the same
    // engine computes once the source has settled.
    seedSourceLines(311);
    g_fieldRate = 50.26f;

    SourceMeasurement measurement;
    REQUIRE(measurement.measureLineRate());
    CHECK_FALSE(measurement.rateSettled());

    SUBCASE("a rate that lands somewhere else has not repeated either") {
        g_fieldRate = 49.92f;
        REQUIRE(measurement.measureLineRate());
        CHECK_FALSE(measurement.rateSettled());
    }

    SUBCASE("but a rate that never repeats is taken as it stands rather than "
            "holding the mode change open for ever") {
        // The capture is frozen while a mode change is outstanding, so a source
        // whose period genuinely wanders would sit on a frozen picture. A
        // raster a fraction of a percent wrong is the better failure.
        // Every reading a fifth of a percent from the last -- twice the
        // agreement band, and inside lineRateFrom()'s 2% of the nominal 50, so
        // each one is measurable and none of them agrees. One attempt is
        // already spent above.
        for (uint8_t i = 2; i < SourceMeasurement::RateAgreementAttempts; ++i) {
            CAPTURE(i);
            g_fieldRate = 50.0f + (float)i * 0.1f;
            REQUIRE(measurement.measureLineRate());
            CHECK_FALSE(measurement.rateSettled());
        }
        g_fieldRate = 49.5f;
        REQUIRE(measurement.measureLineRate());
        CHECK(measurement.rateSettled());
    }

    SUBCASE("the same rate twice is the source holding still") {
        REQUIRE(measurement.measureLineRate());
        CHECK(measurement.rateSettled());
    }

    SUBCASE("and the reading noise of a settled source is not a change") {
        // Two readings of one field period at the ESP's clock, so they differ
        // in the last place. The band is a tenth of a percent: ten times that
        // noise, and a third of the smallest settling error seen.
        g_fieldRate = 50.26f * 1.0005f;
        REQUIRE(measurement.measureLineRate());
        CHECK(measurement.rateSettled());
    }
}

TEST_CASE("a mode change abandons the field rate it had agreed on")
{
    // The rate is about to move, so a reading from the mode before it must not
    // be the one the next reading agrees with.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement;
    REQUIRE(measurement.measureLineRate());
    REQUIRE_FALSE(measurement.rateSettled());

    measurement.resetSteadiness();

    REQUIRE(measurement.measureLineRate());
    CHECK_FALSE(measurement.rateSettled());
}

TEST_CASE("a count outside what any source runs never settles")
{
    // 97 and 98 are what a preset load leaves behind, and they are steady --
    // steadiness alone would call that settled and solve against it.
    seedSourceLines(97);
    SourceMeasurement measurement;

    for (uint8_t i = 0; i < 3 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(measurement.sampleSteady());
}

TEST_CASE("a mode change abandons the run rather than counting through it")
{
    seedSourceLines(311);
    SourceMeasurement measurement;
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();
    REQUIRE(measurement.sampleSteady());

    measurement.resetSteadiness();
    CHECK_FALSE(measurement.sampleSteady());
}
