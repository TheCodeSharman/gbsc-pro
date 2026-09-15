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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "DebugPinStub.h"
#include "MeasuredSource.h"

using namespace Tv5725;

// The two the sketch supplies. getSourceFieldRate() spins on the board, which
// is why it is injected rather than called; tv5725Log() reaches the web console
// there and a buffer here, so the diagnostic is assertable.
// Counted because the cost is the point: this spins for vsync edges through
// FrameSync, up to 250 ms a pulse, so anything asking speculatively has to be
// able to use the answer.
static float g_fieldRate = 50.08f;
static unsigned g_fieldRateCalls = 0;
uint32_t debugPinPulseTicks() { ++g_fieldRateCalls; return ticksForHz(g_fieldRate); }

static std::string g_log;
void tv5725Log(const char *message) { g_log = message; }

// What VideoPath does on a solve, which SourceMeasurement no longer does for
// itself: choose a divider within the three blocks' bounds, then hold it.
static bool solveSampling(SourceMeasurement &sampling, uint32_t lineRateHz,
                          uint8_t oversample)
{
    const uint16_t divider = SamplingClock::recommendedDivider(
        lineRateHz, oversample, sampling.lineDoubled());
    if (divider == 0)
        return false;
    sampling.holdDivider(divider);
    return true;
}

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

// What Mode Detect publishes about the source, in STATUS_00. Only an interlaced
// source can have its count doubled by the serrations, so a case about that
// fault has to say so. Call AFTER seedSourceLines(), which resets the bus.
static void seedInterlaced()
{
    Wire.bank[0][0x00] |= 0x20;   // STATUS_IF_INP_PAL_INT
}

// The input formatter's flag on its own line counter. A ONE-SIDED gate: it
// never sets on a healthy reading, so set means refuse.
static void seedCounterFlagged()
{
    Wire.bank[0][0x05] |= 0x04;   // STATUS_IF_HT_BAD
}

static void seedHPeriod(uint16_t hperiod)
{
    Wire.bank[0][0x06] = (uint8_t)(hperiod & 0xFF);
    Wire.bank[0][0x07] = (uint8_t)((hperiod >> 8) & 0x01);
}

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

// The scan type as a fresh measurement reads it, with the doubling held. The
// class takes the doubling from its own state rather than as an argument,
// because InputFormatter::applyLineDoubling() owns the registers it has to match.
static SourceMeasurement::ScanType scanTypeWithDoubling(uint16_t verticalPeriod,
                                                        bool lineDoubled)
{
    SourceMeasurement measurement;
    measurement.holdLineDoubling(lineDoubled);
    seedSourceHalfLines(verticalPeriod);
    return measurement.measureScanType();
}

// The bench: RiscPC at 320x256@50, VTOTAL 311, so 311 x 50 = 15550 lines/sec.
// PLLAD_MD 2553 and IF_HSYNC_RST 1276 are what the unit actually holds.
static const uint32_t BenchLineRate = 15550;
static const uint16_t BenchDivider = 2553;

TEST_CASE("the IF line follows the divider, because they are one quantity")
{
    // Measured on the unit: PLLAD_MD 2553, IF_HSYNC_RST 1276. The IF counts the
    // ADC line after decimation by two.
    CHECK(InputFormatter::lineCounterFor(BenchDivider, true) == 1276);

    SUBCASE("and it follows a divider that changes") {
        // The whole point: an IF_HSYNC_RST that does not follow PLLAD_MD leaves
        // the IF counting to the end of a line that is not arriving.
        CHECK(InputFormatter::lineCounterFor(1276, true) == 638);
        CHECK(InputFormatter::lineCounterFor(512, true) == 256);
    }
}

TEST_CASE("the ADC has a rated sampling ceiling and the divider must respect it")
{
    // DS-5725-3.2: "Maximum analog sampling rate up to 162MSPS". The rate the
    // part CONVERTS at is PLLAD_MD x line rate x the oversampling installed,
    // and Adc::postDividerFor() picks the crossover row from the first two of
    // those -- so the row reduces the oversampling as the clock rises, the
    // ceiling falls on the clock alone, and asking for a ratio the row refuses
    // does not move it.
    SUBCASE("the bench is inside the limit, but only just") {
        // 2553 x 15550 x 4. That is 98.0% of the 162 MSPS rating, and the row
        // at a 39.7 MHz clock does carry four times.
        CHECK(Adc::sampleRateHz(BenchDivider, BenchLineRate, 4) == 158796600u);
        CHECK(Adc::withinLimit(BenchDivider, BenchLineRate, 4));
    }

    SUBCASE("what caps a slow line is the 12-bit field, not the rating") {
        // 162 MSPS buys 10418 dividers at the bench line rate, and PLLAD_MD
        // holds twelve bits.
        CHECK(Adc::maxDivider(BenchLineRate, 4)
              == Adc::DividerMax);
    }

    SUBCASE("a divider the rating refuses is one whose CLOCK is over it") {
        // 2553 at 31.5 kHz is a clock of 80.4 MHz, and the row there carries no
        // oversampling at all -- so the part converts at 80.4 MSPS and is well
        // inside the rating. Reaching the rating takes a clock over 162 MHz.
        CHECK(Adc::withinLimit(BenchDivider, 31500, 4));
        CHECK_FALSE(Adc::withinLimit(
            Adc::DividerMax, 45000, 4));
    }

    SUBCASE("asking for oversampling the row refuses does not move the ceiling") {
        CHECK(Adc::maxDivider(31500, 4)
              == Adc::maxDivider(31500, 1));
        CHECK(Adc::maxDivider(63960, 4)
              == Adc::maxDivider(63960, 1));
        CHECK(Adc::maxDivider(63960, 4) == 2532);
    }
}

TEST_CASE("the recommended divider leaves margin under the ceiling")
{
    // A ceiling is not a target. The bench ran at 98% of rated and dropped
    // lock; the recommendation backs off so drift in the source line rate does
    // not cross the limit.
    uint16_t recommended = SamplingClock::recommendedDivider(BenchLineRate, 4, true);

    CHECK(recommended < Adc::maxDivider(BenchLineRate, 4));
    CHECK(Adc::withinLimit(recommended, BenchLineRate, 4));

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
    CHECK(Adc::maxDivider(1000, 1) == Adc::DividerMax);

    SUBCASE("and a line rate too fast for any divider reports zero, not one") {
        // Zero is "no divider works here", which a caller must handle. One
        // would look like a legal setting and produce a line one sample long.
        CHECK(Adc::maxDivider(200000000u, 4) == 0);
    }
}

TEST_CASE("a line rate of zero cannot be divided by")
{
    // getSourceFieldRate() returns 0 when there is no lock, and that reaches
    // here as a line rate. Dividing by it is the only way this arithmetic can
    // fault the firmware.
    CHECK(Adc::maxDivider(0, 4) == 0);
    CHECK(SamplingClock::recommendedDivider(0, 4, true) == 0);
    CHECK_FALSE(Adc::withinLimit(BenchDivider, 0, 4));
}

TEST_CASE("an oversample ratio of zero is treated as one")
{
    // ADC_CLK_ICLK1X/2X are read off the chip, and a dropped read arrives as 0.
    // Treating that as "no oversampling" keeps the ceiling honest; treating it
    // as a divisor would make the limit infinite.
    CHECK(Adc::maxDivider(BenchLineRate, 0)
          == Adc::maxDivider(BenchLineRate, 1));
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
        // The write limit is the tighter one at every line rate a source on
        // this board runs: 162 MSPS spends 4095 dividers at 39.6 kHz and the
        // doubled write limit is 2250, so the rating only reaches the answer
        // above about 72 kHz.
        CHECK(SamplingClock::recommendedDivider(BenchLine, Oversample, true) == 2250);
        CHECK(SamplingClock::recommendedDivider(31500, Oversample, true) == 2250);
        CHECK(SamplingClock::recommendedDivider(90000, Oversample, true) == 1764);
    }

    SUBCASE("it samples every mode that gets scaled at all") {
        // It has to resolve the widest source the scaler ever sees, and anything
        // over 535 lines is trapped in RGBHV bypass and never reaches the sampler
        // (docs/rgbhv-bypass-trap.md), so 640x512 and 800x512 are the widest that
        // matter. Active samples = divider x 0.76 x 1.04, the window PanAndZoom
        // opens on the line.
        uint16_t chosen = SamplingClock::recommendedDivider(BenchLine, Oversample, true);
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
        // Read at 90 kHz, above where the write limit stops being the
        // tighter of the two and the rating is what answers.
        uint16_t chosen = SamplingClock::recommendedDivider(90000, Oversample, true);
        CHECK(chosen > (uint16_t)(Adc::maxDivider(90000, Oversample) * 0.97f));
        CHECK(Adc::withinLimit(chosen, 90000, Oversample));
        CHECK_FALSE(Adc::withinLimit(
            Adc::maxDivider(90000, Oversample) + 1, 90000, Oversample));
    }

    SUBCASE("and at the bench rate it now sits below every shipped table") {
        // 2250 against 2269..2559. SourceMeasurement the line more coarsely is what
        // buys capturing the whole of it, and no table's divider does.
        uint16_t chosen = SamplingClock::recommendedDivider(BenchLine, Oversample, true);
        CHECK(chosen < 2269);
        CHECK(InputFormatter::lineCounterFor(chosen, true) <= VideoSourceLine::WriteLimitUnits);
        CHECK(Adc::withinLimit(chosen, BenchLine, Oversample));
    }

    SUBCASE("a line rate nobody can measure yields nothing, not a guess") {
        // A divider written from a zero measurement is how the screen goes
        // green. SourceMeasurement has no business inventing one.
        CHECK(SamplingClock::recommendedDivider(0, Oversample, true) == 0);
    }
}

TEST_CASE("the divider is capped so the whole line stays inside the write limit")
{
    // Past VideoSourceLine::WriteLimitUnits the capture path stops writing video, so
    // a line longer than that loses its tail whatever the ADC rating allows.
    // The divider is what decides the line length, which makes it the lever:
    // sample the line more coarsely and 2250 samples reach the end of it.
    // docs/capture-limits.md
    uint16_t chosen = SamplingClock::recommendedDivider(BenchLineRate, 4, true);

    CHECK(InputFormatter::lineCounterFor(chosen, true) <= VideoSourceLine::WriteLimitUnits);

    SUBCASE("and the ADC rating still binds where it is the tighter of the two") {
        // 90 kHz has room for 1764 under the rating, inside the write limit.
        CHECK(SamplingClock::recommendedDivider(90000, 4, true) == 1764);
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
    CHECK(SyncProcessor::retimeStopFor(BenchDivider) == 2374);

    SUBCASE("and it follows a divider that changes") {
        // 2212 is what recommendedDivider() asks for at the bench line rate.
        // Leaving 2374 behind would put the stop past the end of the line.
        CHECK(SyncProcessor::retimeStopFor(2212) == 2057);
        CHECK(SyncProcessor::retimeStopFor(1276) == 1186);
    }

    SUBCASE("it is integer arithmetic, and agrees with the float it replaces") {
        // The sketch wrote `PLLAD_MD::read() * 0.93f` and truncated. The ESP8266
        // has no FPU and this runs on every solve; the two must not disagree by
        // a sample, so every legal divider is checked rather than a sample of
        // them.
        for (uint32_t d = 0; d <= Adc::DividerMax; d++) {
            CHECK(SyncProcessor::retimeStopFor((uint16_t)d) == (uint16_t)(d * 0.93f));
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

    REQUIRE(solveSampling(sampling, BenchLineRate, 4));
    CHECK(sampling.usable());

    const uint16_t chosen = sampling.divider();
    CHECK(chosen == SamplingClock::recommendedDivider(BenchLineRate, 4, true));

    SUBCASE("the derived values come from the held divider") {
        CHECK(sampling.ifLine() == InputFormatter::lineCounterFor(chosen, true));
        CHECK(sampling.retimeStop() == SyncProcessor::retimeStopFor(chosen));
    }
}

TEST_CASE("an unmeasurable line rate leaves the previous choice alone")
{
    Wire.reset();
    SourceMeasurement sampling;
    REQUIRE(solveSampling(sampling, BenchLineRate, 4));
    const uint16_t chosen = sampling.divider();

    // getSourceFieldRate() reports 0 with no lock, and that reaches here. A
    // divider written from a measurement that did not happen is how the screen
    // goes green -- and it takes the sync processor with it, so there is no
    // picture left to diagnose from.
    CHECK_FALSE(solveSampling(sampling, 0, 4));
    CHECK(sampling.divider() == chosen);
    CHECK(sampling.usable());
}

// A rate that moved while the line count did not is a reading taken while the
// source was still settling, not a new mode. The bench transient: locked at 311
// lines / 50.08 Hz, a solve run across a preset load reads 57.9 Hz and sizes
// PLLAD_MD 2204 where 2548 is due. The count is the reliable half.
TEST_CASE("a rate that moves while the line count does not is a settling reading")
{
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    SourceMeasurement measurement;
    REQUIRE(rateMeasured(measurePastGate(measurement)));

    SUBCASE("nothing held yet cannot contradict anything") {
        SourceMeasurement fresh;
        g_fieldRate = 57.9f;
        CHECK(rateMeasured(measurePastGate(fresh)));
    }

    SUBCASE("a real mode change moves the count, so any rate is accepted") {
        seedSourceLines(499);
        g_fieldRate = 75.088f;
        CHECK(rateMeasured(measurePastGate(measurement)));
    }

    SUBCASE("the same source drifting is not a change") {
        g_fieldRate = 50.5f;
        CHECK(rateMeasured(measurePastGate(measurement)));
    }

    SUBCASE("but a rate that moved alone is refused") {
        g_fieldRate = 57.9f;
        CHECK(measurement.measure() == SourceMeasurement::Unmeasurable);
    }
}

// --- ADC samples to IF units, which is not always a halving ------------------

TEST_CASE("the IF line follows the decimation the scan mode applies")
{
    // IF_HS_DEC_FACTOR is the input formatter's horizontal decimation. The
    // line-doubled path halves, the progressive path does not -- and an IF
    // counter wrapping at half the samples the ADC delivers shows the picture
    // twice across the screen, the second copy colour-shifted.
    CHECK(InputFormatter::lineCounterFor(2120, true) == 1060u);
    CHECK(InputFormatter::lineCounterFor(2120, false) == 2120u);
}

TEST_CASE("the divider ceiling follows the decimation too")
{
    // The write limit is in IF units, so the divider that lands the line end on
    // it is twice the limit when halving and equal to it when not. A ceiling
    // computed for the wrong one captures past where the part stops writing.
    const uint16_t doubled = SamplingClock::recommendedDivider(15574u, 4, true);
    const uint16_t progressive = SamplingClock::recommendedDivider(37469u, 4, false);

    CHECK(InputFormatter::lineCounterFor(doubled, true) <= VideoSourceLine::WriteLimitUnits);
    CHECK(InputFormatter::lineCounterFor(progressive, false) <= VideoSourceLine::WriteLimitUnits);
}

// --- the line rate, measured off the chip ------------------------------------


TEST_CASE("the line rate is measured rather than handed in")
{
    // Field rate x source lines, the two quantities the divider is a function
    // of, both read where they live rather than passed down from the sketch.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement;
    CHECK(rateMeasured(measurePastGate(measurement)));
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
    REQUIRE(rateMeasured(measurePastGate(measurement)));

    g_fieldRate = 57.9f;
    CHECK(measurement.measure() == SourceMeasurement::Unmeasurable);

    // The refusal is the return value, not a zeroed rate: the last rate that
    // passed the cross-check stands, so a reader through a sync loss gets the
    // last one believed.
    CHECK(measurement.lineRateHz() == 15624u);

    SUBCASE("and reports what it saw, both halves") {
        CHECK(measurement.sourceLines() == 311);
        CHECK(g_log == "sampling: 311 lines x 57.90 Hz -> line rate 0");
    }

    SUBCASE("an unmeasurable field rate is refused the same way") {
        g_fieldRate = 0.0f;
        CHECK(measurement.measure() == SourceMeasurement::Unmeasurable);
        CHECK(measurement.lineRateHz() == 15624u);
    }

    SUBCASE("so is the 97 lines a preset load leaves behind") {
        seedSourceLines(97);
        g_fieldRate = 50.08f;
        CHECK_FALSE(rateMeasured(measurePastGate(measurement)));
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

    g_fieldRateCalls = 0;
    for (uint8_t i = 1; i < SourceMeasurement::SteadySamples; ++i) {
        CAPTURE(i);
        CHECK(measurement.measure() == SourceMeasurement::NotSteady);
    }
    CHECK(g_fieldRateCalls == 0);
    CHECK(measurement.measure() != SourceMeasurement::NotSteady);

    SUBCASE("and stays steady while the count does") {
        CHECK(measurement.measure() != SourceMeasurement::NotSteady);
    }
}

TEST_CASE("a count that moves starts the run again")
{
    seedSourceLines(311);
    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    // Mid-change. A one-sample blip is normal; what matters is that it does not
    // report steady on the strength of the run before it.
    seedSourceLines(97);
    CHECK(measurement.measure() == SourceMeasurement::NotSteady);
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
    REQUIRE(measurePastGate(measurement) == SourceMeasurement::Settling);

    SUBCASE("a rate that lands somewhere else has not repeated either") {
        g_fieldRate = 49.92f;
        CHECK(measurement.measure() == SourceMeasurement::Settling);
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
            CHECK(measurement.measure() == SourceMeasurement::Settling);
        }
        g_fieldRate = 49.5f;
        CHECK(measurement.measure() == SourceMeasurement::Measured);
    }

    SUBCASE("the same rate twice is the source holding still") {
        CHECK(measurement.measure() == SourceMeasurement::Measured);
    }

    SUBCASE("and the reading noise of a settled source is not a change") {
        // Two readings of one field period at the ESP's clock, so they differ
        // in the last place. The band is a tenth of a percent: ten times that
        // noise, and a third of the smallest settling error seen.
        g_fieldRate = 50.26f * 1.0005f;
        CHECK(measurement.measure() == SourceMeasurement::Measured);
    }
}

TEST_CASE("a mode change abandons the field rate it had agreed on")
{
    // The rate is about to move, so a reading from the mode before it must not
    // be the one the next reading agrees with.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) == SourceMeasurement::Settling);

    measurement.modeChanged();

    CHECK(measurePastGate(measurement) == SourceMeasurement::Settling);
}

TEST_CASE("a count outside what any source runs never settles")
{
    // 97 and 98 are what a preset load leaves behind, and they are steady --
    // steadiness alone would call that settled and solve against it.
    seedSourceLines(97);
    SourceMeasurement measurement;

    for (uint8_t i = 0; i < 3 * SourceMeasurement::SteadySamples; ++i)
        CHECK(measurement.measure() == SourceMeasurement::NotSteady);
}

TEST_CASE("a mode change abandons the run rather than counting through it")
{
    seedSourceLines(311);
    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    measurement.modeChanged();
    CHECK(measurement.measure() == SourceMeasurement::NotSteady);
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
    // The trap's signature: the sync processor reports a count too low and the
    // samples per line too high, because the PLL locked to every other hsync
    // and counts one line per two sent. The count alone cannot show it -- 155
    // is simply not a source -- so the sample count against the divider is what
    // recovers the real 310.
    SourceMeasurement sampling;

    seedSource(sampling, 155, 2249, 1124);
    CHECK(sampling.readSourceLines() == 310);

    SUBCASE("and four lines to a count likewise") {
        // STATUS_SYNC_PROC_HTOTAL is 12 bits, so four lines to a count is only
        // reachable on a divider small enough for the product to fit.
        seedSource(sampling, 78, 4000, 1000);
        CHECK(sampling.readSourceLines() == 312);
    }

    SUBCASE("a count that is already a source is taken as it stands") {
        seedSource(sampling, 311, 2553, 2553);
        CHECK(sampling.readSourceLines() == 311);
    }

    SUBCASE("an unlocked sync processor is not a multiple of anything") {
        // 2558 against 2553 is a five-sample offset, arithmetically incapable
        // of looking like a multiple: the nearest is 5106. So nothing corrects
        // the count and it comes back as read.
        seedSource(sampling, 97, 2558, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }

    SUBCASE("nor is a reading simply unrelated to the divider") {
        // 2400 with the sync processor unconfigured.
        seedSource(sampling, 97, 2400, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }

    SUBCASE("beyond the multiples any offset can be made to fit one") {
        seedSource(sampling, 155, 5620, 1124);
        CHECK(sampling.readSourceLines() == 155);
    }

    SUBCASE("and nothing is a multiple of nothing") {
        seedSource(sampling, 155, 0, 1124);
        CHECK(sampling.readSourceLines() == 155);
        seedSource(sampling, 155, 2249, 0);
        CHECK(sampling.readSourceLines() == 155);
    }
}

// --- escaping a divider the source cannot lock to ----------------------------

TEST_CASE("the multiple tolerates the jitter of every line it counts")
{
    // One counted line carries the jitter of k source lines, so the window
    // scales with k rather than being the latch check's fixed two samples.
    // Measured: 2251 against a divider of 1124, where twice is 2248.
    SourceMeasurement sampling;

    seedSource(sampling, 155, 2251, 1124);
    CHECK(sampling.readSourceLines() == 310);

    seedSource(sampling, 155, 2247, 1124);
    CHECK(sampling.readSourceLines() == 310);

    SUBCASE("and widening it does not reach the readings that are not multiples") {
        seedSource(sampling, 97, 2558, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }
}

// What an output frame of this many lines can display, which is the question
// the doubling asks: the part cannot minify, so this is the ceiling.
static uint16_t showableIn(uint16_t frameLines)
{
    return Tv5725::AxisVertical.maximumCapture(frameLines, 0);
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
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK(measurement.lowLineRate());
    }

    SUBCASE("576p at twice the rate is not") {
        seedSourceLines(625);
        g_fieldRate = 50.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK_FALSE(measurement.lowLineRate());
    }

    SUBCASE("it survives a sync loss, because that is when its readers run") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        seedSourceLines(0);
        CHECK_FALSE(rateMeasured(measurePastGate(measurement)));
        CHECK(measurement.lowLineRate());
    }

    SUBCASE("the rate it answers from is the one reported, across that loss") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        seedSourceLines(0);
        CHECK_FALSE(rateMeasured(measurePastGate(measurement)));
        CHECK(measurement.lineRateHz() == 15624u);
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
    CHECK((referenceDividerFor(false) & 1u) == 0);
    CHECK((referenceDividerFor(true) & 1u) == 0);

    SUBCASE("and it is still the write limit, rounded down to reach it") {
        CHECK(referenceDividerFor(false) <= VideoSourceLine::WriteLimitUnits);
        CHECK(referenceDividerFor(true) <= 2 * VideoSourceLine::WriteLimitUnits);
        CHECK(referenceDividerFor(false) >= VideoSourceLine::WriteLimitUnits - 1);
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
    // 27 MHz / ((431 + 1) * 4) = 15625 on the bench source, and the run is
    // believed without a vsync spin.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 50.08f;
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 15625u);

    SUBCASE("a progressive 800x600 line likewise") {
        SourceMeasurement other;
        seedSourceLines(679);
        seedHPeriod(165);
        g_fieldRate = 59.8f;
        REQUIRE(rateMeasured(measurePastGate(other)));
        CHECK(other.lineRateHz() == 40662u);
    }

    SUBCASE("and a 70 Hz one") {
        SourceMeasurement other;
        seedSourceLines(311);
        seedHPeriod(308);
        g_fieldRate = 70.0f;
        REQUIRE(rateMeasured(measurePastGate(other)));
        CHECK(other.lineRateHz() == 21844u);
    }
}

TEST_CASE("readings that disagree are refused, which is what railing looks like")
{
    // The railed form is noisy rather than stuck, so the window sees several
    // values. Refused, the field rate answers instead -- which is the point:
    // it is measured a different way and does not rail with the counter.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(511);
    Wire.drift(0x00, 0x06);   // HPERIOD_IF advances on every read
    g_fieldRate = 50.08f;
    g_fieldRateCalls = 0;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    CHECK(sampling.lineRateHz() == 15624u);
}

// HPERIOD_IF is seeded the way the chip presents it: segment 0, register 0x06,
// nine bits.
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
    REQUIRE(rateMeasured(measurePastGate(sampling)));

    CHECK(sampling.lineRateHz() == 15625u);
    CHECK(sampling.fieldRateHz() > 50.0f);
    CHECK(sampling.fieldRateHz() < 50.1f);
}

TEST_CASE("a corroborated HPERIOD_IF run measures the line rate without a vsync spin")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 50.08f;

    // The first reading has nothing held to check it against, so it is paid
    // for once.
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 15625u);

    g_fieldRateCalls = 0;
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 15625u);

    // The cost is the point: the held rate corroborates the reading, so nothing
    // spun for a vsync edge.
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

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    // 525 x 60: VTOTAL is zero based and 640x480@60 is a 525-line frame, so
    // the count of 524 is the standard's own number less one.
    CHECK(sampling.lineRateHz() == 31500u);
}

TEST_CASE("a flagged window is refused however good the readings look")
{
    // The flag is about the counter, not the number: it never set on a healthy
    // reading across 91 recorded samples plus 20 measured, so a window carrying
    // it is not a window to take a rate from -- even one reading 431, which is
    // exactly what this source is due.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    seedCounterFlagged();
    g_fieldRate = 50.08f;
    g_fieldRateCalls = 0;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    // The field rate's answer, not the counter's 15625.
    CHECK(sampling.lineRateHz() == 15624u);
}

// The window's three tests all pass a value that is railed AND plausible: 272
// on a 311-line source is 24725 Hz, a 79.25 Hz field rate, above the floor and
// inside the band, and a stuck register repeats perfectly. The held rate is
// what rejects it -- and at adoption there is nothing held, or the count moved,
// so there is nothing to reject it with. The field rate is measured a different
// way and does not rail with it.
// docs/investigations/hperiod-if-railing.md
TEST_CASE("a railed reading is refused where the field rate contradicts it")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(272);
    g_fieldRate = 50.08f;

    REQUIRE(rateMeasured(measurePastGate(sampling)));

    // 50.08 x 312, which is what the source runs at, not the 24725 the counter
    // states.
    CHECK(sampling.lineRateHz() == 15624u);
}

TEST_CASE("a railed reading never becomes the held rate")
{
    // rateFollowsCount() refuses a rate that moved at an unchanged count, but
    // only HeldRateRejectionLimit times over -- a source that genuinely changes
    // rate without changing its count must not hold the mode change open for
    // ever. Railed, that escape hatch is what lets the bad value in, and once
    // held it rejects every correct reading against itself.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 50.08f;
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    REQUIRE(sampling.lineRateHz() == 15625u);

    seedHPeriod(272);
    for (unsigned i = 0; i < 2u * SourceMeasurement::HeldRateRejectionLimit; ++i)
        sampling.measure();

    CHECK(sampling.lineRateHz() == 15624u);
}

TEST_CASE("a reading nothing can corroborate is refused, not adopted")
{
    // The spin reports 0 with no lock, and the counter rails to a value that is
    // wrong and stable -- so a reading taken while nothing is held and nothing
    // can speak to it is the one case the window's tests cannot judge at all.
    // Refusing costs a pass, which the next one retries. Adopting costs until
    // HeldRateRejectionLimit refusals drain, because the bad value then rejects
    // every correct reading against itself.
    //
    // Nothing is lost by requiring it: the route the counter falls back to is
    // the field rate, so a source it can never measure never acquired anyway.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 0.0f;

    CHECK_FALSE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 0u);
}

TEST_CASE("a refusal does not spend the rejection budget")
{
    // HeldRateRejectionLimit exists so a source that genuinely changed rate at
    // an unchanged count cannot hold the mode change open for ever. A reading
    // that was never measured is not such a source, and counting it there
    // spends the escape hatch on nothing.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 50.08f;
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    REQUIRE(sampling.lineRateHz() == 15625u);

    g_fieldRate = 0.0f;
    seedHPeriod(272);
    for (unsigned i = 0; i < 2u * SourceMeasurement::HeldRateRejectionLimit; ++i)
        CHECK_FALSE(rateMeasured(measurePastGate(sampling)));

    CHECK(sampling.lineRateHz() == 15625u);
}

TEST_CASE("a reading implying a line no television generates is refused")
{
    // The railed values sit below the floor: 511 on a 311-line source is
    // 13.2 kHz against the 15625 the 431 it is due gives. Nothing legitimate is
    // lost -- the slowest line here is 15.625 kHz. Refused, the field rate is
    // what answers instead.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(511);
    g_fieldRate = 50.08f;
    g_fieldRateCalls = 0;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    CHECK(sampling.lineRateHz() == 15624u);

    SUBCASE("and one just under the floor likewise") {
        SourceMeasurement other;
        seedSourceLines(311);
        seedHPeriod(510);
        g_fieldRateCalls = 0;
        REQUIRE(rateMeasured(measurePastGate(other)));
        CHECK(g_fieldRateCalls > 0);
    }
}

TEST_CASE("the line rate comes off HPERIOD_IF against the chip's own 27 MHz")
{
    // 27 MHz / ((431 + 1) * 4) = 15625, and it is the chip's own clock rather
    // than the ADC's, so the reading does not move with PLLAD_MD. Believed
    // without a vsync spin, which is the whole reason the counter is preferred.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    g_fieldRate = 50.08f;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 15625u);
}

// The wait in front of a preset load. A load is expensive and a source
// mid-change gives a count that is wrong and steady for a few samples, so the
// run is long rather than the four SteadySamples an idle pass uses.

// Telling the source's lines from the serrations either side of its vertical
// interval. The sync processor counts through the coast, so a coast that does
// not cover the equalisation pulses counts them as lines; the input formatter
// measures the same frame in half-lines by a route the coast cannot double.
// docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md

TEST_CASE("a field count against the frame in half-lines is the source's lines")
{
    // 310 against a 624 half-line frame is the source's own count, nearer half
    // the witness than the whole of it.
    SourceMeasurement measurement;
    seedSourceLines(310);
    seedSourceHalfLines(624);
    seedInterlaced();

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a count as large as the half-line total is the serrations")
{
    SourceMeasurement measurement;
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();

    CHECK(measurePastGate(measurement) == SourceMeasurement::Serrations);
}

TEST_CASE("a progressive source whose frame the witness counts in lines is not serrations")
{
    // 480p on YPbPr, measured on the bench: SP_VTOTAL 524 with VPERIOD_IF 524,
    // steady, STATUS_IF_VT_OK set. The witness is not reporting half-lines
    // here, so the count sits exactly ON it -- which reads identically to a
    // doubled count. Judged on the witness alone this source is rejected on
    // every pass, no solve ever runs, and the sync pads stay blanked for ever.
    //
    // The source not being interlaced is what separates them, and it is
    // measured rather than inferred.
    SourceMeasurement measurement;
    seedSourceLines(524);
    seedSourceHalfLines(524);

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a progressive source cannot have counted the serrations")
{
    // A progressive source has no field and frame to differ, so nothing can
    // double its count however the witness reads.
    SourceMeasurement measurement;
    seedSourceLines(607);
    seedSourceHalfLines(624);

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a half-line total that measures nothing refuses to judge the count")
{
    // VPERIOD_IF is debris on a separate-sync source, where it reads values
    // like 20 against a true 311. Judged against that, any count at all looks
    // nearer the total than half of it.
    SourceMeasurement measurement;
    seedSourceLines(311);
    seedSourceHalfLines(20);
    seedInterlaced();

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a serration count never goes steady, however still it holds")
{
    // The coast is not covering the equalisation pulses, so the sync processor
    // counts them and reports about twice the source. It holds that value
    // perfectly, which is exactly what a steadiness run on its own cannot see.
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();
    SourceMeasurement measurement;

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples * 3; ++i) {
        CAPTURE(i);
        CHECK_FALSE(rateMeasured(measurement.measure()));
    }
}

TEST_CASE("a field count goes steady with the witness live")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    CHECK(measurePastGate(measurement) != SourceMeasurement::NotSteady);
}

TEST_CASE("the reason a serration count was refused is available to the caller")
{
    // The engine cannot tell "not settled yet" from "settled on the wrong
    // count" by the return value alone, and only the second is worth acting on.
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();
    SourceMeasurement measurement;

    CHECK(measurePastGate(measurement) == SourceMeasurement::Serrations);
}

TEST_CASE("a count still gathering samples is not reported as serrations")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement;

    CHECK(measurement.measure() == SourceMeasurement::NotSteady);
}

TEST_CASE("a good count clears a serration verdict")
{
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();
    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) == SourceMeasurement::Serrations);

    seedSourceLines(310);
    seedSourceHalfLines(624);
    seedInterlaced();

    // A completed run at the new count is what clears it. The verdict stands
    // while the run is still re-gathering, because that is the state the coast
    // was widened for and one good sample does not undo it.
    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        reading = measurement.measure();

    CHECK(reading != SourceMeasurement::Serrations);
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

TEST_CASE("the reference puts the chip on a divider this class chose")
{
    // A count taken through the previous mode's divider is not the source's, so
    // the reference goes on BEFORE anything measures.
    Wire.reset();
    SourceMeasurement sampling;
    sampling.holdLineDoubling(false);
    sampling.holdDivider(1234);

    sampling.applyReferenceSampling();

    CHECK(sampling.divider() == referenceDividerFor(false));
    CHECK(dividerInForce() == referenceDividerFor(false));
    CHECK(lineCounterInForce() == sampling.ifLine());
    CHECK(retimeStopInForce() == sampling.retimeStop());
}

// The reference is a FIXED state, so it does not follow whatever oversampling
// the output mode happens to be running: it asks for the most the clock allows
// and takes what it gets, the same way on every source.
TEST_CASE("the reference samples at what the clock allows, not at the mode's choice")
{
    Wire.reset();
    SourceMeasurement sampling;
    sampling.holdLineDoubling(false);

    sampling.applyReferenceSampling();

    CHECK(Adc::oversampleInForce() ==
          Adc::oversampleFor(Adc::PLLAD_KS::read(), Adc::OversampleAsClockAllows));
}

TEST_CASE("the reference for a line-doubled source is its own")
{
    // The capture write limit doubles with the line doubler, so the reference
    // is a function of the scan mode and not a constant.
    Wire.reset();
    SourceMeasurement sampling;
    sampling.holdLineDoubling(true);

    sampling.applyReferenceSampling();

    CHECK(sampling.divider() == referenceDividerFor(true));
    CHECK(sampling.divider() != referenceDividerFor(false));
}

// The estimate the reference is sized from comes off the steadiness run, not off
// a single read, so a case that moves it has to complete a run at the new count.
static void settleAt(SourceMeasurement &sampling, uint16_t lines)
{
    seedSourceLines(lines);
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        sampling.measure();
}

TEST_CASE("a reference already in force is not written again")
{
    // It re-latches the ADC PLL, which is a relock nothing asked for, and this
    // runs on every pass of a mode change that has not settled yet.
    Wire.reset();
    SourceMeasurement sampling;
    settleAt(sampling, 311);
    sampling.applyReferenceSampling();
    REQUIRE(Wire.touched[5][0x12]);

    Wire.reset();
    settleAt(sampling, 311);
    sampling.applyReferenceSampling();

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
    sampling.applyReferenceSampling();
    const uint16_t divider = sampling.divider();

    Wire.reset();
    settleAt(sampling, 311);
    sampling.applyReferenceSampling();

    CHECK(sampling.divider() == divider);   // the reference itself has not moved
    CHECK(Wire.touched[5][0x12]);           // and it was written anyway
}

TEST_CASE("the divider is bounded so one window can span the whole line")
{
    // The capture path writes CaptureWidthLimitUnits from wherever the window
    // starts, so a line sampled finely enough that capturable() runs past that
    // has ends no single window can hold at once -- the user zooms out and the
    // picture stops growing short of the line. The divider is the only lever.
    // docs/capture-limits.md
    const uint32_t VesaLine = 37879;     // 800x600@60
    const uint16_t Framable = VideoSourceLine::framableIfLine(
        165.0f / 1350.0f, VideoSourceLine::CaptureLagUnits, true, false);

    SUBCASE("which is more samples than holding the whole line under the write limit") {
        CHECK(SamplingClock::recommendedDivider(VesaLine, 1, false, Framable)
              > SamplingClock::recommendedDivider(VesaLine, 1, false));
    }

    SUBCASE("and the line it chooses is the one that fits") {
        CHECK(SamplingClock::recommendedDivider(VesaLine, 1, false, Framable) == 1250);
    }

    SUBCASE("no bound offered keeps the whole line inside the write limit") {
        CHECK(SamplingClock::recommendedDivider(VesaLine, 1, false, 0)
              == SamplingClock::recommendedDivider(VesaLine, 1, false));
    }

    SUBCASE("the IF's 11-bit geometry registers are a wall above both") {
        // IF_HSYNC_RST, IF_HB_ST2 and IF_HB_SP2 are all [10:0]. PLLAD_MD 2094
        // was accepted, latched and read back correctly at
        // STATUS_SYNC_PROC_HTOTAL while IF_HSYNC_RST held 46 -- 2094 modulo
        // 2048 -- with the picture destroyed and nothing reporting a fault.
        const uint16_t chosen =
            SamplingClock::recommendedDivider(20000, 1, false, 3000);
        CHECK(InputFormatter::lineCounterFor(chosen, false)
              <= InputFormatter::LineCounterMax);
    }
}

TEST_CASE("the sampling budget is spent at the rate the ADC actually converts at")
{
    // Adc::oversampleFor() reduces a request the crossover row cannot give, and
    // the row is chosen from the divider's own clock -- so budgeting for the
    // oversampling ASKED FOR caps the divider for a load the part is never
    // asked to take. 162 MSPS reserved for four times puts that clock at
    // 40.5 MHz, one step over postDividerFor()'s 40 MHz row, where two is what
    // installs and the part converts at half the rating it reserved.

    SUBCASE("an oversampling the row will refuse does not cap the divider") {
        CHECK(Adc::maxDivider(31500, 4)
              == Adc::maxDivider(31500, 1));
    }

    SUBCASE("and the converted rate stays inside the rating at every line rate") {
        for (uint32_t rate = 15000; rate <= 70000; rate += 500) {
            for (uint8_t wanted = 1; wanted <= 8; wanted = (uint8_t)(wanted * 2)) {
                const uint16_t divider = Adc::maxDivider(rate, wanted);
                const uint8_t installed = Adc::oversampleFor(
                    Adc::postDividerFor((uint32_t)divider * rate), wanted);
                REQUIRE(Adc::sampleRateHz(divider, rate, installed)
                        <= Adc::MaxSampleRateHz);
            }
        }
    }
}

// --- the scan type ----------------------------------------------------------
//
// An interlaced field carries a half line, and VPERIOD_IF is the only count on
// the board with the resolution to hold one -- but only where the input
// formatter doubles the line, which is what puts the count in half lines. So
// the parity that means interlaced INVERTS with line doubling, and neither
// parity nor a table of broadcast totals answers on its own.
//
// Every value below is measured, one machine and one cable, with only the
// mode's line rate and its interlace flag moving.
// docs/investigations/interlaced-source-measurement.md

TEST_CASE("the scan type is the half line in VPERIOD_IF")
{
    SUBCASE("a doubled count carries the half line as an even period") {
        // RiscPC 320x256@50 and 640x200@60, composite sync, 519 and 534 samples.
        CHECK(scanTypeWithDoubling(623, true)
              == SourceMeasurement::ScanProgressive);
        CHECK(scanTypeWithDoubling(624, true)
              == SourceMeasurement::ScanInterlaced);
        CHECK(scanTypeWithDoubling(523, true)
              == SourceMeasurement::ScanProgressive);
        CHECK(scanTypeWithDoubling(524, true)
              == SourceMeasurement::ScanInterlaced);
    }

    SUBCASE("an undoubled count carries it as an odd one") {
        // RiscPC 640x480@60, 31690 Hz, IF_HS_DEC_FACTOR 0, 526 and 529 samples.
        CHECK(scanTypeWithDoubling(524, false)
              == SourceMeasurement::ScanProgressive);
        CHECK(scanTypeWithDoubling(525, false)
              == SourceMeasurement::ScanInterlaced);
    }
}

// A Wii reads VPERIOD_IF 524 at 480i AND at 480p, so no table of totals and no
// parity alone separates them. The doubling does, and it is held state.
TEST_CASE("the two scan types of one source can share a period")
{
    CHECK(scanTypeWithDoubling(524, true)
          == SourceMeasurement::ScanInterlaced);
    CHECK(scanTypeWithDoubling(524, false)
          == SourceMeasurement::ScanProgressive);
}

// The separate-sync path leaves debris here rather than a period -- 33 to 101
// on the bench source, with STATUS_IF_VT_OK reading 0 beside it.
TEST_CASE("a period too short to be a vertical one answers nothing")
{
    CHECK(scanTypeWithDoubling(57, true)
          == SourceMeasurement::ScanUnknown);
    CHECK(scanTypeWithDoubling(101, true)
          == SourceMeasurement::ScanUnknown);
    CHECK(scanTypeWithDoubling(0, false)
          == SourceMeasurement::ScanUnknown);
}

TEST_CASE("the scan type of the held source uses the doubling in force")
{
    // One period, two answers: 524 is a doubled interlaced field and an
    // undoubled progressive frame, which is why the Wii reads 524 at 480i and
    // at 480p alike.
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedSourceHalfLines(524);

    sampling.holdLineDoubling(true);
    CHECK(sampling.measureScanType() == SourceMeasurement::ScanInterlaced);

    sampling.holdLineDoubling(false);
    CHECK(sampling.measureScanType() == SourceMeasurement::ScanProgressive);
}

// --- an interlaced count never holds still, and that IS the measurement ------
//
// An interlaced field carries a half line, so the sync processor's count
// alternates by one and four consecutive identical samples never arrive. A Wii
// at 480i therefore never reached `acquired`: no solve ran, the output clock
// was never seeded, and the picture rolled while every register read correct.
//
// The alternation is not noise to be tolerated. It is the only scan-type signal
// that survives separate sync, where VPERIOD_IF holds debris -- measured at two
// rasters, 311/312 and 261/262, against a progressive count that never moves at
// either. docs/investigations/interlaced-source-measurement.md

static bool settleAlternating(SourceMeasurement &measurement, uint16_t low,
                              uint8_t samples)
{
    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t i = 0; i < samples; ++i) {
        seedSourceLines(i % 2 ? (uint16_t)(low + 1) : low);
        reading = measurement.measure();
    }
    return reading != SourceMeasurement::NotSteady;
}

TEST_CASE("a count alternating by one settles instead of running for ever")
{
    SourceMeasurement measurement;

    CHECK(settleAlternating(measurement, 259, 8));
}

TEST_CASE("the pair's higher count is the one settled on")
{
    // Both values undercount the true field -- 259.5 against 262.5 on a Wii at
    // 480i -- so the higher of the pair is the closer of the two.
    SourceMeasurement measurement;
    REQUIRE(settleAlternating(measurement, 259, 8));

    CHECK(measurement.steadyLines() == 260);
}

TEST_CASE("a count alternating by one reads as interlaced where the period cannot")
{
    // Separate sync: STATUS_IF_VT_OK is 0 and VPERIOD_IF holds debris, so
    // scanTypeFor() has nothing and the alternation is all there is.
    SourceMeasurement measurement;
    REQUIRE(settleAlternating(measurement, 311, 8));

    CHECK(measurement.measureScanType() == SourceMeasurement::ScanInterlaced);
}

TEST_CASE("a measured period still outranks the alternation")
{
    SourceMeasurement measurement;
    measurement.holdLineDoubling(true);
    REQUIRE(settleAlternating(measurement, 311, 8));

    // 623 doubled is progressive, whatever the count did.
    seedSourceHalfLines(623);
    CHECK(measurement.measureScanType() == SourceMeasurement::ScanProgressive);
}

TEST_CASE("a steady count claims nothing about the scan type on its own")
{
    // A Wii at PAL 576i holds a steady 310 while genuinely interlaced -- 1186
    // samples, zero changes -- so a count that does not alternate is not
    // evidence of a progressive source.
    seedSourceLines(310);
    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    CHECK(measurement.measureScanType() == SourceMeasurement::ScanUnknown);
}

TEST_CASE("a count that moves by more than one still starts the run again")
{
    seedSourceLines(311);
    SourceMeasurement measurement;
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    seedSourceLines(313);
    CHECK(measurement.measure() == SourceMeasurement::NotSteady);
}

// The input formatter's vertical measurement has one owner, because two blocks
// measure the frame and only this one is gated on the measurement completing.
TEST_CASE("the vertical period is zero until the measurement completes")
{
    seedSourceLines(311);
    seedSourceHalfLines(624);
    CHECK(SourceMeasurement::measureVerticalPeriod() == 624);

    SUBCASE("and a measurement that did not complete claims nothing") {
        // STATUS_IF_VT_OK clear. VPERIOD_IF is debris on a separate-sync
        // source, where it reads values like 20 against a true 311 -- so the
        // register's value is not the thing to judge it by.
        Wire.bank[0][0x00] &= (uint8_t)~0x01;
        CHECK(SourceMeasurement::measureVerticalPeriod() == 0);
    }
}

// --- one pass, every reading ------------------------------------------------

// The hsync pulse, as the sync processor reports it: the low time in ADC
// samples and the polarity. Call AFTER seedSourceLines(), which resets the bus.
static void seedHsync(uint16_t hlowLen, bool positive)
{
    Wire.bank[0][0x19] = (uint8_t)(hlowLen & 0xFF);
    Wire.bank[0][0x1A] = (uint8_t)((hlowLen >> 8) & 0x0F);
    if (positive)
        Wire.bank[0][0x16] |= 0x01;
}

// Every quantity the engine solves from is read in ONE pass, because a capture
// taken on one solve and a window taken on another describe different states
// and the discrepancy between them looks like a fault in the arithmetic.
TEST_CASE("one call measures the source, and every reading comes from that pass")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    seedHsync(181, false);
    sampling.holdDivider(BenchDivider);
    g_fieldRate = 50.08f;

    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && reading != SourceMeasurement::Measured; ++pass)
        reading = sampling.measure();

    REQUIRE(reading == SourceMeasurement::Measured);
    CHECK(sampling.sourceLines() == 311);
    CHECK(sampling.lineRateHz() == 15625u);
    CHECK(sampling.hsync().syncDuty() == doctest::Approx(181.0f / (float)BenchDivider));
    CHECK_FALSE(sampling.hsync().syncAtHead());
}

// The cheap gate is INSIDE the one call, so a caller that asks every pass does
// not pay the vsync spin until the count has settled.
TEST_CASE("a count still gathering samples costs no field rate measurement")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    sampling.holdDivider(BenchDivider);
    g_fieldRateCalls = 0;

    CHECK(sampling.measure() == SourceMeasurement::NotSteady);
    CHECK(g_fieldRateCalls == 0);
}

// The caller widens the coast on this, so it has to be distinguishable from a
// run that is merely still gathering.
TEST_CASE("a count that read the serrations is reported apart from an unsettled one")
{
    SourceMeasurement sampling;
    seedSourceLines(622);
    seedSourceHalfLines(622);
    seedInterlaced();
    seedHPeriod(431);
    sampling.holdDivider(BenchDivider);

    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && reading != SourceMeasurement::Serrations; ++pass)
        reading = sampling.measure();

    CHECK(reading == SourceMeasurement::Serrations);
}

// A rate is not worth sizing a raster from until it has repeated, and the
// caller treats that differently from a source it cannot read at all.
TEST_CASE("a rate that has not repeated yet is settling rather than measured")
{
    SourceMeasurement sampling;
    seedSourceLines(311);
    seedHPeriod(431);
    sampling.holdDivider(BenchDivider);
    g_fieldRate = 50.08f;

    SourceMeasurement::MeasurementStatus first = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && first == SourceMeasurement::NotSteady; ++pass)
        first = sampling.measure();

    CHECK(first == SourceMeasurement::Settling);
}
