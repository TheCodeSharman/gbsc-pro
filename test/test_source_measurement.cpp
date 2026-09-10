// Host-compiled unit tests for src/tv5725/SourceMeasurement.h -- `make -C test source-measurement`.
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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"

using namespace Tv5725;

// The two the sketch supplies. getSourceFieldRate() spins on the board, which
// is why it is injected rather than called; tv5725Log() reaches the web console
// there and a buffer here, so the diagnostic is assertable.
// Counted because the cost is the point: this spins for vsync edges through
// FrameSync, up to 250 ms a pulse, so anything asking speculatively has to be
// able to use the answer.
static float g_fieldRate = 50.08f;
static unsigned g_fieldRateCalls = 0;
float getSourceFieldRate(boolean) { ++g_fieldRateCalls; return g_fieldRate; }

static std::string g_log;
void tv5725Log(const char *message) { g_log = message; }

static void seedSourceLines(uint16_t lines)
{
    Wire.reset();
    Wire.bank[0][0x1B] = (uint8_t)(lines & 0xFF);
    Wire.bank[0][0x1C] = (uint8_t)((lines >> 8) & 0x07);
    g_log.clear();
}

// The input formatter's own measurement of the frame, in half-lines, and the
// bit that says it completed. Call AFTER seedSourceLines(), which resets the bus
// -- left unseeded the witness reads nothing and judges nothing.
static void seedSourceHalfLines(uint16_t halfLines)
{
    Wire.bank[0][0x07] = (uint8_t)((halfLines & 0x7F) << 1);
    Wire.bank[0][0x08] = (uint8_t)((halfLines >> 7) & 0x0F);
    Wire.bank[0][0x00] |= 0x01;
}

// The bench: RiscPC at 320x256@50, VTOTAL 311, so 311 x 50 = 15550 lines/sec.
// PLLAD_MD 2553 and IF_HSYNC_RST 1276 are what the unit actually holds.
static const uint32_t BenchLineRate = 15550;
static const uint16_t BenchDivider = 2553;

TEST_CASE("the IF line follows the divider, because they are one quantity")
{
    // Measured on the unit: PLLAD_MD 2553, IF_HSYNC_RST 1276. The IF counts the
    // ADC line after decimation by two.
    CHECK(SourceMeasurement::ifLineFor(BenchDivider, true) == 1276);

    SUBCASE("and it follows a divider that changes") {
        // The whole point: an IF_HSYNC_RST that does not follow PLLAD_MD leaves
        // the IF counting to the end of a line that is not arriving.
        CHECK(SourceMeasurement::ifLineFor(1276, true) == 638);
        CHECK(SourceMeasurement::ifLineFor(512, true) == 256);
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
    uint16_t recommended = SourceMeasurement::recommendedDivider(BenchLineRate, 4, true);

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
    CHECK(SourceMeasurement::recommendedDivider(0, 4, true) == 0);
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
        CHECK(SourceMeasurement::recommendedDivider(BenchLine, Oversample, true) == 2250);
        CHECK(SourceMeasurement::recommendedDivider(31500, Oversample, true) == 1258);
    }

    SUBCASE("it samples every mode that gets scaled at all") {
        // It has to resolve the widest source the scaler ever sees, and anything
        // over 535 lines is trapped in RGBHV bypass and never reaches the sampler
        // (docs/rgbhv-bypass-trap.md), so 640x512 and 800x512 are the widest that
        // matter. Active samples = divider x 0.76 x 1.04, the window PanAndZoom
        // opens on the line.
        uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLine, Oversample, true);
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
        uint16_t chosen = SourceMeasurement::recommendedDivider(31500, Oversample, true);
        CHECK(chosen > (uint16_t)(SourceMeasurement::maxDivider(31500, Oversample) * 0.97f));
        CHECK(SourceMeasurement::withinLimit(chosen, 31500, Oversample));
        CHECK_FALSE(SourceMeasurement::withinLimit(1285 + 1, 31500, Oversample));
    }

    SUBCASE("and at the bench rate it now sits below every shipped table") {
        // 2250 against 2269..2559. SourceMeasurement the line more coarsely is what
        // buys capturing the whole of it, and no table's divider does.
        uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLine, Oversample, true);
        CHECK(chosen < 2269);
        CHECK(SourceMeasurement::ifLineFor(chosen, true) <= VideoSourceLine::WriteLimitUnits);
        CHECK(SourceMeasurement::withinLimit(chosen, BenchLine, Oversample));
    }

    SUBCASE("a line rate nobody can measure yields nothing, not a guess") {
        // A divider written from a zero measurement is how the screen goes
        // green. SourceMeasurement has no business inventing one.
        CHECK(SourceMeasurement::recommendedDivider(0, Oversample, true) == 0);
    }
}

TEST_CASE("the divider is capped so the whole line stays inside the write limit")
{
    // Past VideoSourceLine::WriteLimitUnits the capture path stops writing video, so
    // a line longer than that loses its tail whatever the ADC rating allows.
    // The divider is what decides the line length, which makes it the lever:
    // sample the line more coarsely and 2250 samples reach the end of it.
    // docs/capture-limits.md
    uint16_t chosen = SourceMeasurement::recommendedDivider(BenchLineRate, 4, true);

    CHECK(SourceMeasurement::ifLineFor(chosen, true) <= VideoSourceLine::WriteLimitUnits);

    SUBCASE("and the ADC rating still binds where it is the tighter of the two") {
        // 31.5 kHz has room for 1258 under the rating, well inside the limit.
        CHECK(SourceMeasurement::recommendedDivider(31500, 4, true) == 1258);
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
    CHECK(chosen == SourceMeasurement::recommendedDivider(BenchLineRate, 4, true));

    SUBCASE("the derived values come from the held divider") {
        CHECK(sampling.ifLine() == SourceMeasurement::ifLineFor(chosen, true));
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

TEST_CASE("a field rate that disagrees with the line count is refused")
{
    // A source locked at 311 lines / 50.08 Hz yields PLLAD_MD 2204 against the
    // 2548 due when the solve sees the ~57.9 Hz transient a preset load leaves,
    // which a bare 40..100 Hz check passes. The line count is reliable where the
    // period measurement is not, so it says which rate is plausible.
    // solveRaster() refuses the same way.
    // STATUS_SYNC_PROC_VTOTAL is zero based, so the frame is one line longer
    // than it counts and the line rate is the field rate times VTOTAL + 1. Two
    // instruments settle it: HPERIOD_IF reads this source at 15625 Hz, and
    // 15625/312 is the 50.08 the field rate measures where 15625/311 is 50.24,
    // which it does not.
    CHECK(SourceMeasurement::lineRateFrom(311, 50.08f) == 15624u);

    SUBCASE("the line count does not decide what the rate may be") {
        // 311 lines runs at 50 Hz here and 60 Hz elsewhere, and 262 the other
        // way about. Both are real sources, so both are measurements rather
        // than errors.
        CHECK(SourceMeasurement::lineRateFrom(311, 60.0f) != 0u);
        CHECK(SourceMeasurement::lineRateFrom(312, 50.0f) != 0u);
        CHECK(SourceMeasurement::lineRateFrom(262, 50.0f) != 0u);
        CHECK(SourceMeasurement::lineRateFrom(262, 59.94f) != 0u);
    }

    SUBCASE("the 97/98 a preset load leaves behind is refused outright") {
        // Documented as normal for a moment after a load, and the reason
        // solveRaster() defers rather than solving. Below SourceVerticalTotalMin, so
        // it never reaches the rate check at all.
        CHECK(SourceMeasurement::lineRateFrom(97, 50.0f) == 0u);
        CHECK(SourceMeasurement::lineRateFrom(98, 50.0f) == 0u);
    }

    SUBCASE("a field rate that is neither 50 nor 60 is still a field rate") {
        // 640x480@75 on the VGA input: 499 lines, measured 75.088 Hz. Rejecting
        // it leaves the previous mode's raster and clock in place, so the output
        // runs at a rate no display locks to and the screen goes blank.
        CHECK(SourceMeasurement::lineRateFrom(499, 75.088f) != 0u);
    }

    SUBCASE("a rate no video source runs at is still refused") {
        CHECK(SourceMeasurement::lineRateFrom(311, 4.0f) == 0u);
        CHECK(SourceMeasurement::lineRateFrom(311, 400.0f) == 0u);
    }
}

TEST_CASE("a rate that moves while the line count does not is a settling reading")
{
    // The bench transient: locked at 311 lines / 50.08 Hz, a solve run across a
    // preset load reads 57.9 Hz and sizes PLLAD_MD 2204 where 2548 is due. The
    // count is the reliable half, so a rate that moves alone is refused.
    const uint32_t held = SourceMeasurement::lineRateFrom(311, 50.08f);

    CHECK_FALSE(SourceMeasurement::rateFollowsCount(
        311, SourceMeasurement::lineRateFrom(311, 57.9f), 311, held));

    SUBCASE("a real mode change moves the count, so any rate is accepted") {
        CHECK(SourceMeasurement::rateFollowsCount(
            499, SourceMeasurement::lineRateFrom(499, 75.088f), 311, held));
    }

    SUBCASE("the same source drifting is not a change") {
        CHECK(SourceMeasurement::rateFollowsCount(
            311, SourceMeasurement::lineRateFrom(311, 50.5f), 311, held));
    }

    SUBCASE("nothing held yet cannot contradict anything") {
        CHECK(SourceMeasurement::rateFollowsCount(311, held, 0, 0));
    }
}

// --- ADC samples to IF units, which is not always a halving ------------------

TEST_CASE("the IF line follows the decimation the scan mode applies")
{
    // IF_HS_DEC_FACTOR is the input formatter's horizontal decimation. The
    // line-doubled path halves, the progressive path does not -- and an IF
    // counter wrapping at half the samples the ADC delivers shows the picture
    // twice across the screen, the second copy colour-shifted.
    CHECK(SourceMeasurement::ifLineFor(2120, true) == 1060u);
    CHECK(SourceMeasurement::ifLineFor(2120, false) == 2120u);
}

TEST_CASE("the divider ceiling follows the decimation too")
{
    // The write limit is in IF units, so the divider that lands the line end on
    // it is twice the limit when halving and equal to it when not. A ceiling
    // computed for the wrong one captures past where the part stops writing.
    const uint16_t doubled = SourceMeasurement::recommendedDivider(15574u, 4, true);
    const uint16_t progressive = SourceMeasurement::recommendedDivider(37469u, 4, false);

    CHECK(SourceMeasurement::ifLineFor(doubled, true) <= VideoSourceLine::WriteLimitUnits);
    CHECK(SourceMeasurement::ifLineFor(progressive, false) <= VideoSourceLine::WriteLimitUnits);
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
    CHECK(measurement.lineRateHz() == 15624u);

    SUBCASE("and it holds both inputs, because nothing downstream can say which was wrong") {
        CHECK(measurement.fieldRateHz() > 50.0f);
        CHECK(measurement.fieldRateHz() < 50.1f);
    }

    SUBCASE("the diagnostic names both inputs and the result") {
        CHECK(g_log == "sampling: 311 lines x 50.08 Hz -> line rate 15624");
    }
}

TEST_CASE("a rate that moves without the line count is refused, and says so")
{
    // A field rate measured while the source is still settling passes a plain
    // bounds check comfortably -- 57.9 Hz against a real 50.08 -- and the
    // divider comes out proportionally wrong. It is the line count holding
    // still that gives it away, so a good reading has to be held first.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement;
    REQUIRE(measurement.measureLineRate());

    g_fieldRate = 57.9f;
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

// --- was the divider actually latched? ---------------------------------------

TEST_CASE("a latched divider is the one the sync processor counts")
{
    // STATUS_SYNC_PROC_HTOTAL counts real ADC clocks per line, so with the PLL
    // locked at the ratio the divider asked for it EQUALS the divider. That
    // makes it the only witness on the board to a divider that was written but
    // never loaded -- PLLAD_MD reads back the new value either way.
    CHECK(SourceMeasurement::dividerLatched(2250, 2250));

    SUBCASE("and it wobbles by a sample either way") {
        CHECK(SourceMeasurement::dividerLatched(2252, 2250));
        CHECK(SourceMeasurement::dividerLatched(2248, 2250));
        CHECK_FALSE(SourceMeasurement::dividerLatched(2253, 2250));
        CHECK_FALSE(SourceMeasurement::dividerLatched(2247, 2250));
    }

    SUBCASE("an unlocked sync processor reads steady and wrong") {
        // 2558 against 2553 held over 22 samples while SP_VTOTAL sat at 97.
        CHECK_FALSE(SourceMeasurement::dividerLatched(2558, 2553));
    }

    SUBCASE("and a PLL locked to every other hsync counts twice the line") {
        CHECK_FALSE(SourceMeasurement::dividerLatched(2249, 1124));
    }

    SUBCASE("a divider of zero was never latched, whatever the count reads") {
        CHECK_FALSE(SourceMeasurement::dividerLatched(0, 0));
    }

    SUBCASE("the tolerance is the caller's, because the question differs") {
        // A phase sweep asks whether it is worth running at all, and answers it
        // eight samples wide.
        CHECK(SourceMeasurement::dividerLatched(2558, 2553, 8));
    }
}

TEST_CASE("a near-integer multiple of the divider is a PLL counting several lines")
{
    // The trap's signature: the count is twice the divider because the PLL
    // locked to every other hsync, so one line is counted per two sent.
    CHECK(SourceMeasurement::linesPerCount(2249, 1124) == 2);
    CHECK(SourceMeasurement::linesPerCount(4500, 1125) == 4);

    SUBCASE("a latched divider is not a multiple of itself") {
        CHECK(SourceMeasurement::linesPerCount(2250, 2250) == 0);
    }

    SUBCASE("an unlocked sync processor is not one either") {
        // 2558 against 2553 is a five-sample offset, which is arithmetically
        // incapable of looking like a multiple: the nearest is 5106.
        CHECK(SourceMeasurement::linesPerCount(2558, 2553) == 0);

        // And a reading that is simply unrelated to the divider -- 2400 with
        // the sync processor unconfigured -- is unrelated to every multiple.
        CHECK(SourceMeasurement::linesPerCount(2400, 2553) == 0);
    }

    SUBCASE("beyond the multiples any offset can be made to fit one") {
        CHECK(SourceMeasurement::linesPerCount(5620, 1124) == 0);
    }

    SUBCASE("and nothing is a multiple of nothing") {
        CHECK(SourceMeasurement::linesPerCount(0, 1124) == 0);
        CHECK(SourceMeasurement::linesPerCount(2249, 0) == 0);
        CHECK(SourceMeasurement::linesPerCount(0, 0) == 0);
    }
}

// --- escaping a divider the source cannot lock to ----------------------------

// The three source-side reads at once, plus the divider held over from the
// mode before. seedSourceLines() resets the bus, so the order matters.
static void seedSource(SourceMeasurement &sampling, uint16_t lines,
                       uint16_t lineSamples, uint16_t divider)
{
    seedSourceLines(lines);
    Wire.bank[0][0x17] = (uint8_t)(lineSamples & 0xFF);
    Wire.bank[0][0x18] = (uint8_t)((lineSamples >> 8) & 0x0F);
    Wire.bank[5][0x12] = (uint8_t)(divider & 0xFF);
    Wire.bank[5][0x13] = (uint8_t)((divider >> 8) & 0x0F);
    sampling.holdDivider(divider);
}

TEST_CASE("the multiple tolerates the jitter of every line it counts")
{
    // One counted line carries the jitter of k source lines, so the window
    // scales with k rather than being the latch check's fixed two samples.
    // Measured: 2251 against a divider of 1124, where twice is 2248.
    CHECK(SourceMeasurement::linesPerCount(2251, 1124) == 2);
    CHECK(SourceMeasurement::linesPerCount(2247, 1124) == 2);

    SUBCASE("and widening it does not reach the readings that are not multiples") {
        // 2558 against 2553 is 2548 away from twice the divider, so no
        // plausible widening makes it one.
        CHECK(SourceMeasurement::linesPerCount(2558, 2553) == 0);
        CHECK(SourceMeasurement::linesPerCount(2400, 2553) == 0);
    }
}

TEST_CASE("line doubling is decided by the source line count")
{
    // Line doubling exists so there are enough lines for the rest of the chain
    // to reach the output resolution. That is a question about how many lines
    // arrive, not how fast they arrive.
    //
    // Measured over the RISC PC's modes: 261, 311 and 363 total lines are
    // captured doubled; 448, 524, 533 and 627 are not. The boundary sits in
    // that gap. It is reproduced rather than derived, so that moving it is a
    // deliberate change with its own acceptance test.
    CHECK(SourceMeasurement::lineDoublingFor(261) == true);
    CHECK(SourceMeasurement::lineDoublingFor(311) == true);
    CHECK(SourceMeasurement::lineDoublingFor(363) == true);
    CHECK(SourceMeasurement::lineDoublingFor(448) == false);
    CHECK(SourceMeasurement::lineDoublingFor(524) == false);
    CHECK(SourceMeasurement::lineDoublingFor(533) == false);
    CHECK(SourceMeasurement::lineDoublingFor(627) == false);

    // An interlaced PAL frame is 625 lines, which is plenty. What it needs is
    // DEINTERLACING, which is a separate register and a separate decision.
    CHECK(SourceMeasurement::lineDoublingFor(625) == false);

    // No measurement yet. The default is the one a low-line-count source needs,
    // because that is the source a wrong guess leaves without enough lines.
    CHECK(SourceMeasurement::lineDoublingFor(0) == true);
}

// What an output frame of this many lines can display, which is the question
// the doubling asks: the part cannot minify, so this is the ceiling.
static uint16_t showableIn(uint16_t frameLines)
{
    return Tv5725::AxisVertical.maximumCapture(frameLines, 0);
}

TEST_CASE("a source is not doubled into an output that cannot show the result")
{
    // Doubling turns a 311-line source into 624 units, and the part cannot
    // minify: an output with less room than that shows the top of the doubled
    // frame and nothing else, with the control dead in both directions. So the
    // question is not only how many lines arrive, but how many can be shown.
    CHECK(SourceMeasurement::lineDoublingFor(311, showableIn(1125)) == true);  // 1080p
    CHECK(SourceMeasurement::lineDoublingFor(311, showableIn(750)) == true);   // 720p
    CHECK(SourceMeasurement::lineDoublingFor(311, showableIn(525)) == false);  // 480p
    CHECK(SourceMeasurement::lineDoublingFor(311, showableIn(625)) == false);  // 576p

    SUBCASE("a shorter source still doubles into the same output") {
        // 288 lines doubled is 578, which a 625-line frame holds.
        CHECK(SourceMeasurement::lineDoublingFor(288, showableIn(625)) == true);
    }

    SUBCASE("no output raster asks the source alone") {
        // Bypass, and every caller that has not solved a raster yet.
        CHECK(SourceMeasurement::lineDoublingFor(311, 0) == true);
        CHECK(SourceMeasurement::lineDoublingFor(524, 0) == false);
    }
}

TEST_CASE("only a rate a display accepts may be bypassed")
{
    // Bypass hands the source's own timing to the encoder, so it works only
    // where the DISPLAY can show that timing. Refusing falls back to the
    // scaling path, which shows any rate; accepting wrongly puts torn,
    // sheared content on the panel that reads as a broken scaler.
    // docs/rgbhv-bypass-trap.md
    SourceMeasurement measurement;

    SUBCASE("nothing measured yet cannot be bypassed") {
        CHECK_FALSE(measurement.rateCanBypass());
    }

    SUBCASE("a 15.6 kHz line cannot") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(measurement.measureLineRate());
        CHECK_FALSE(measurement.rateCanBypass());
    }

    SUBCASE("the 31.4 kHz VGA line can") {
        // 640x480@60, VTOTAL 524. Measured locking.
        seedSourceLines(524);
        g_fieldRate = 60.0f;
        CHECK(measurement.measureLineRate());
        CHECK(measurement.rateCanBypass());
    }

    SUBCASE("26.6 kHz can, which is under the VGA line") {
        // 640x512@50, VTOTAL 533. Measured locking, which is why the floor is
        // bracketed rather than taken from the VGA standard.
        seedSourceLines(533);
        g_fieldRate = 50.0f;
        CHECK(measurement.measureLineRate());
        CHECK(measurement.rateCanBypass());
    }

    SUBCASE("21.8 kHz cannot, measured") {
        // 640x352@60, VTOTAL 363. Measured: the sink reports no signal, and
        // this rate clears LowLineRateBelowHz -- so that constant is not the
        // one to ask.
        seedSourceLines(363);
        g_fieldRate = 60.0f;
        CHECK(measurement.measureLineRate());
        CHECK_FALSE(measurement.lowLineRate());
        CHECK_FALSE(measurement.rateCanBypass());
    }
}

TEST_CASE("a 15 kHz line is recognised by its rate, not by a standard's number")
{
    // SP_H_PULSE_IGNOR and the coast window both key on a line whose vertical
    // interval carries equalisation and serration pulses. That is a property of
    // the rate, and a source is filed under a standard whose number does not
    // carry it: a scaled RGBHV source runs a 15 kHz line and is filed as 480p,
    // because that is the branch it borrows.
    SourceMeasurement measurement;

    SUBCASE("nothing measured yet is not a low line rate") {
        CHECK_FALSE(measurement.lowLineRate());
    }

    SUBCASE("a 15.6 kHz line is one") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(measurement.measureLineRate());
        CHECK(measurement.lowLineRate());
    }

    SUBCASE("576p at twice the rate is not") {
        seedSourceLines(625);
        g_fieldRate = 50.0f;
        CHECK(measurement.measureLineRate());
        CHECK_FALSE(measurement.lowLineRate());
    }

    SUBCASE("it survives a sync loss, because that is when its readers run") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(measurement.measureLineRate());
        seedSourceLines(0);
        CHECK_FALSE(measurement.measureLineRate());
        CHECK(measurement.lowLineRate());
    }

    SUBCASE("the rate it answers from is the one reported, across that loss") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(measurement.measureLineRate());
        seedSourceLines(0);
        CHECK_FALSE(measurement.measureLineRate());
        CHECK(measurement.heldLineRateHz() == 15624u);
    }
}

TEST_CASE("the reference divider is even, like every divider the solver picks")
{
    // recommendedDivider() masks the low bit because an odd divider leaves the
    // input formatter half a sample out from the line the ADC delivers. The
    // reference is a divider like any other and has to obey it: measured on the
    // bench, an odd 1125 on a progressive source reads `524 lines x 0.00 Hz`
    // for as long as it is left there, while 1124 measures at once.
    //
    // WriteLimitUnits is 1125 and only the line-doubled reference doubles it,
    // so it is the progressive one that lands odd.
    CHECK((SourceMeasurement::referenceDivider(false) & 1u) == 0);
    CHECK((SourceMeasurement::referenceDivider(true) & 1u) == 0);

    SUBCASE("and it is still the write limit, rounded down to reach it") {
        CHECK(SourceMeasurement::referenceDivider(false) <= VideoSourceLine::WriteLimitUnits);
        CHECK(SourceMeasurement::referenceDivider(true) <= 2 * VideoSourceLine::WriteLimitUnits);
        CHECK(SourceMeasurement::referenceDivider(false) >= VideoSourceLine::WriteLimitUnits - 1);
    }
}

// HPERIOD_IF is counted against the chip's own 27 MHz, so it reads the source's
// line rate directly and needs no vsync spin. It also rails. Three things
// separate a good reading from a bad one: a settled reading repeats to within a
// count, a value can imply a field rate no source runs at, and STATUS_IF_HT_BAD
// sets somewhere in the window. The third is the only one that catches a STUCK
// register, because the first two are passed by a value that never moves.
// docs/investigations/hperiod-if-railing.md
TEST_CASE("a run of agreeing HPERIOD_IF readings gives the source's line rate")
{
    const uint16_t settled[] = {431, 431, 430};
    CHECK(SourceMeasurement::lineRateFromHPeriod(settled, 3, 311, false) == 15625u);

    const uint16_t progressive[] = {165, 165, 165};
    CHECK(SourceMeasurement::lineRateFromHPeriod(progressive, 3, 679, false) == 40662u);

    const uint16_t seventy[] = {308, 307, 308};
    CHECK(SourceMeasurement::lineRateFromHPeriod(seventy, 3, 311, false) == 21844u);
}

TEST_CASE("readings that disagree are refused, which is what railing looks like")
{
    const uint16_t railed[] = {511, 255, 16, 509};
    CHECK(SourceMeasurement::lineRateFromHPeriod(railed, 4, 311, false) == 0u);

    const uint16_t noisy[] = {511, 510, 429, 436};
    CHECK(SourceMeasurement::lineRateFromHPeriod(noisy, 4, 311, false) == 0u);
}

TEST_CASE("a steady reading implying a field rate no source runs at is refused")
{
    // 50 against a 524-line source is 132 kHz, a 252 Hz field rate. Perfectly
    // steady, so agreement alone cannot reject it.
    const uint16_t stableWrong[] = {50, 50, 50, 50};
    CHECK(SourceMeasurement::lineRateFromHPeriod(stableWrong, 4, 524, false) == 0u);
}

TEST_CASE("one reading is not a run, and a line count that is not a source is refused")
{
    const uint16_t one[] = {431};
    CHECK(SourceMeasurement::lineRateFromHPeriod(one, 1, 311, false) == 0u);

    const uint16_t settled[] = {431, 431, 431};
    CHECK(SourceMeasurement::lineRateFromHPeriod(settled, 3, 0, false) == 0u);
}

// HPERIOD_IF is seeded the way the chip presents it: segment 0, register 0x06,
// nine bits.
static void seedHPeriod(uint16_t hperiod)
{
    Wire.bank[0][0x06] = (uint8_t)(hperiod & 0xFF);
    Wire.bank[0][0x07] = (uint8_t)((hperiod >> 8) & 0x01);
}

TEST_CASE("the field rate is derived over the same frame the line rate assumed")
{
    // Both paths have to agree what the frame is. HPERIOD_IF gives the line
    // rate and the field rate is derived back out of it, so dividing by the
    // zero-based count where lineRateFrom() multiplies by count + 1 puts the two
    // a line apart: 15625/311 is 50.24 against the 50.08 the source runs at.
    // That number is what choice.resolve() picks the output mode with.
    seedSourceLines(311);
    seedHPeriod(431);

    SourceMeasurement sampling;
    REQUIRE(sampling.measureLineRate());

    CHECK(sampling.lineRateHz() == 15625u);
    CHECK(sampling.fieldRateHz() > 50.0f);
    CHECK(sampling.fieldRateHz() < 50.1f);
}

TEST_CASE("a believable HPERIOD_IF run measures the line rate without a vsync spin")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRateCalls = 0;

    REQUIRE(sampling.measureLineRate());
    CHECK(sampling.lineRateHz() == 15625u);

    // The cost is the point: nothing spun for a vsync edge.
    CHECK(g_fieldRateCalls == 0);
}

TEST_CASE("a refused HPERIOD_IF run falls back to the field rate")
{
    SourceMeasurement sampling;
    seedSourceLines(524);
    // 50 against 524 lines is 132 kHz, a 252 Hz field rate: the railing's
    // stable form, which no amount of agreement can reject.
    seedHPeriod(50);
    g_fieldRate = 60.0f;
    g_fieldRateCalls = 0;

    REQUIRE(sampling.measureLineRate());
    CHECK(g_fieldRateCalls > 0);
    // 525 x 60: VTOTAL is zero based and 640x480@60 is a 525-line frame, so
    // the count of 524 is the standard's own number less one.
    CHECK(sampling.lineRateHz() == 31500u);
}

TEST_CASE("a stuck reading is refused even where it implies a plausible rate")
{
    // 511 on a 311-line source is 13183 Hz, a 42.38 Hz field rate -- inside any
    // band that admits a 24 or 25 Hz source, so plausibility cannot reject it.
    // Agreement cannot either: a STUCK register repeats perfectly while a live
    // one varies, so the run test prefers the fault to the truth.
    //
    // What separates them is STATUS_IF_HT_BAD, measured 15 of 20 samples on a
    // live instance and 0 of 20 once cleared. Left accepted, the raster is
    // solved 2264 wide where 1916 is due and the sink reports 42 Hz.
    const uint16_t stuck[] = {511, 511, 511};
    CHECK(SourceMeasurement::lineRateFromHPeriod(stuck, 3, 311, true) == 0u);
}

TEST_CASE("a flagged window is refused however good the readings look")
{
    // The flag is about the counter, not the number: it never set on a healthy
    // reading across 91 recorded samples plus 20 measured, so a window carrying
    // it is not a window to take a rate from.
    const uint16_t settled[] = {431, 431, 430};
    CHECK(SourceMeasurement::lineRateFromHPeriod(settled, 3, 311, true) == 0u);
}

TEST_CASE("a reading implying a line no television generates is refused")
{
    // The railed values sit below the floor: 511 is 13.2 kHz and 510 is 13.2,
    // against 15625 for the 431 this source is due. Nothing legitimate is lost
    // -- the slowest line here is 15.625 kHz.
    const uint16_t slow[] = {511, 511, 511};
    CHECK(SourceMeasurement::lineRateFromHPeriod(slow, 3, 311, false) == 0u);

    const uint16_t justUnder[] = {510, 510, 509};
    CHECK(SourceMeasurement::lineRateFromHPeriod(justUnder, 3, 311, false) == 0u);

    const uint16_t bench[] = {431, 431, 430};
    CHECK(SourceMeasurement::lineRateFromHPeriod(bench, 3, 311, false) == 15625u);
}

TEST_CASE("the line rate comes off HPERIOD_IF against the chip's own 27 MHz")
{
    CHECK(SourceMeasurement::lineRateForHPeriod(431) == 15625u);
}

// The wait in front of a preset load. A load is expensive and a source
// mid-change gives a count that is wrong and steady for a few samples, so the
// run is long rather than the four SteadySamples an idle pass uses.

TEST_CASE("a count that holds across the run comes back")
{
    seedSourceLines(627);

    CHECK(SourceMeasurement::countHeldStill(627) == 627);
}

TEST_CASE("a count that moves during the run refuses")
{
    // 0 rather than a flag: no count is a count no source runs at, so the
    // caller cannot use it by accident.
    seedSourceLines(627);
    Wire.drift(0x00, 0x1C);

    CHECK(SourceMeasurement::countHeldStill(627) == 0);
}

TEST_CASE("a count within the agreement window still holds")
{
    seedSourceLines(629);

    CHECK(SourceMeasurement::countHeldStill(627) == 629);
}

TEST_CASE("a count outside the agreement window does not")
{
    seedSourceLines(631);

    CHECK(SourceMeasurement::countHeldStill(627) == 0);
}

// Telling the source's lines from the serrations either side of its vertical
// interval. The sync processor counts through the coast, so a coast that does
// not cover the equalisation pulses counts them as lines; the input formatter
// measures the same frame in half-lines by a route the coast cannot double.
// docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md

TEST_CASE("a field count against the frame in half-lines is the source's lines")
{
    CHECK_FALSE(SourceMeasurement::countIsSerrations(310, 624));
}

TEST_CASE("a count as large as the half-line total is the serrations")
{
    CHECK(SourceMeasurement::countIsSerrations(607, 624));
}

TEST_CASE("a half-line total that measures nothing refuses to judge the count")
{
    // VPERIOD_IF is debris on a separate-sync source, where it reads values
    // like 20 against a true 311. Judged against that, any count at all looks
    // nearer the total than half of it.
    CHECK_FALSE(SourceMeasurement::countIsSerrations(311, 20));
}


TEST_CASE("a serration count never goes steady, however still it holds")
{
    // The coast is not covering the equalisation pulses, so the sync processor
    // counts them and reports about twice the source. It holds that value
    // perfectly, which is exactly what a steadiness run on its own cannot see.
    seedSourceLines(607);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples * 3; ++i) {
        CAPTURE(i);
        CHECK_FALSE(measurement.sampleSteady());
    }
}

TEST_CASE("a field count goes steady with the witness live")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    for (uint8_t i = 1; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();

    CHECK(measurement.sampleSteady());
}

TEST_CASE("the reason a serration count was refused is available to the caller")
{
    // The engine cannot tell "not settled yet" from "settled on the wrong
    // count" by the return value alone, and only the second is worth acting on.
    seedSourceLines(607);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();

    CHECK(measurement.countWasSerrations());
}

TEST_CASE("a count still gathering samples is not reported as serrations")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    measurement.sampleSteady();

    CHECK_FALSE(measurement.countWasSerrations());
}

TEST_CASE("a good count clears a serration verdict")
{
    seedSourceLines(607);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();
    REQUIRE(measurement.countWasSerrations());

    seedSourceLines(310);
    seedSourceHalfLines(624);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measurement.sampleSteady();

    CHECK_FALSE(measurement.countWasSerrations());
}

// --- putting the chip on the sampling clock ---------------------------------
//
// The divider, the IF line counter and the retime stop are ONE quantity in three
// registers, and this class holds it -- so it writes all three. Anything that
// wrote one of them alone would leave the input formatter describing a line the
// ADC is not delivering, which is a solid green display with sync still stable.

static uint16_t dividerInForce() { return (uint16_t)Wire.field(5, 0x12, 0, 12); }
static uint16_t lineCounterInForce() { return (uint16_t)Wire.field(1, 0x0E, 0, 11); }
static uint16_t retimeStopInForce() { return (uint16_t)Wire.field(5, 0x4B, 0, 12); }

TEST_CASE("applying the sampling writes all three registers of the one quantity")
{
    Wire.reset();
    SourceMeasurement sampling;
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    REQUIRE(sampling.measureLineRate());
    REQUIRE(sampling.solve(sampling.lineRateHz(), 4));

    sampling.applySampling(4);

    CHECK(dividerInForce() == sampling.divider());
    CHECK(lineCounterInForce() == sampling.ifLine());
    CHECK(retimeStopInForce() == sampling.retimeStop());
}

TEST_CASE("a measurement that solved nothing puts nothing on the chip")
{
    // Writing a divider of zero stops the ADC clocking the line at all, and
    // every register downstream is then sized for a line that never arrives.
    Wire.reset();
    SourceMeasurement sampling;

    REQUIRE_FALSE(sampling.usable());
    sampling.applySampling(4);

    CHECK_FALSE(Wire.touched[5][0x12]);
    CHECK_FALSE(Wire.touched[1][0x0E]);
}

TEST_CASE("the reference puts the chip on a divider this class chose")
{
    // A count taken through the previous mode's divider is not the source's, so
    // the reference goes on BEFORE anything measures.
    Wire.reset();
    SourceMeasurement sampling;
    sampling.holdLineDoubling(false);
    sampling.holdDivider(1234);

    sampling.applyReferenceSampling(4);

    CHECK(sampling.divider() == SourceMeasurement::referenceDivider(false));
    CHECK(dividerInForce() == SourceMeasurement::referenceDivider(false));
    CHECK(lineCounterInForce() == sampling.ifLine());
}

TEST_CASE("the reference for a line-doubled source is its own")
{
    // The capture write limit doubles with the line doubler, so the reference
    // is a function of the scan mode and not a constant.
    Wire.reset();
    SourceMeasurement sampling;
    sampling.holdLineDoubling(true);

    sampling.applyReferenceSampling(4);

    CHECK(sampling.divider() == SourceMeasurement::referenceDivider(true));
    CHECK(sampling.divider() != SourceMeasurement::referenceDivider(false));
}

// The estimate the reference is sized from comes off the steadiness run, not off
// a single read, so a case that moves it has to complete a run at the new count.
static void settleAt(SourceMeasurement &sampling, uint16_t lines)
{
    seedSourceLines(lines);
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        sampling.sampleSteady();
}

TEST_CASE("a reference already in force is not written again")
{
    // It re-latches the ADC PLL, which is a relock nothing asked for, and this
    // runs on every pass of a mode change that has not settled yet.
    Wire.reset();
    SourceMeasurement sampling;
    settleAt(sampling, 311);
    sampling.applyReferenceSampling(4);
    REQUIRE(Wire.touched[5][0x12]);

    Wire.reset();
    settleAt(sampling, 311);
    sampling.applyReferenceSampling(4);

    CHECK_FALSE(Wire.touched[5][0x12]);
}

TEST_CASE("a reference is re-applied when the estimate it was sized from moves")
{
    // PLLAD_KS is an octave of CKO, and CKO is the divider TIMES the rate -- so
    // a count caught mid-transition picks the wrong octave, and the reference
    // divider for a scan mode does not change when the count settles. A return
    // keyed on the divider alone leaves KS wrong with PLLAD_MD right, which is a
    // state nothing can measure its way out of.
    Wire.reset();
    SourceMeasurement sampling;
    settleAt(sampling, 700);
    sampling.applyReferenceSampling(4);
    const uint16_t divider = sampling.divider();

    Wire.reset();
    settleAt(sampling, 311);
    sampling.applyReferenceSampling(4);

    CHECK(sampling.divider() == divider);   // the reference itself has not moved
    CHECK(Wire.touched[5][0x12]);           // and it was written anyway
}
