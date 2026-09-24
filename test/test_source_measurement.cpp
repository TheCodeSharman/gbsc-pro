// Host-compiled unit tests for src/tv5725/SourceMeasurement.h -- `make -C test source-measurement`.
//
// Pure arithmetic over the ADC front end: how finely the incoming line is
// sampled, and what the IF's own line counter must be set to as a result.
//
// Those are ONE quantity in more than one register, and moving one without the
// others is what a fault here looks like: halving the divider alone leaves
// IF_HSYNC_RST describing a line twice as long as the one arriving, which is a
// solid green display with sync still stable.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <algorithm>
#include <vector>

#include <string>
#include <cstring>
#include <cstdio>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputWindow.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "DebugPinStub.h"
#include "MeasuredSource.h"

static Tv5725::InputFormatter inputFormatter;

using namespace Tv5725;

// The two the sketch supplies. getSourceFieldRate() spins on the board, which
// is why it is injected rather than called; tv5725Log() reaches the web console
// there and a buffer here, so the diagnostic is assertable.
// Counted because the cost is the point: this spins for vsync edges through
// FrameSync, up to 250 ms a pulse, so anything asking speculatively has to be
// able to use the answer.
static float g_fieldRate = 50.08f;
static unsigned g_fieldRateCalls = 0;
// Readings handed out one per call before g_fieldRate resumes, for a case about
// what one PASS samples rather than what one pass reads.
static std::vector<float> g_fieldRates;
uint32_t debugPinPulseTicks()
{
    ++g_fieldRateCalls;
    if (g_fieldRates.empty())
        return ticksForHz(g_fieldRate);
    const float hz = g_fieldRates.front();
    g_fieldRates.erase(g_fieldRates.begin());
    return ticksForHz(hz);
}

static std::string g_log;
static std::vector<std::string> g_lines;
void tv5725Log(const char *message) { g_log = message; g_lines.push_back(message); }

static bool loggedContaining(const std::string &part)
{
    for (size_t i = 0; i < g_lines.size(); ++i)
        if (g_lines[i].find(part) != std::string::npos)
            return true;
    return false;
}

static bool logged(const std::string &line)
{
    return std::find(g_lines.begin(), g_lines.end(), line) != g_lines.end();
}

// What VideoPath does on a solve, which SourceMeasurement no longer does for
// itself: choose a divider within the three blocks' bounds, then hold it.
static bool solveSampling(uint32_t lineRateHz, uint8_t oversample,
                          bool lineDoubled = true)
{
    const uint16_t divider = SamplingClock::recommendedDivider(
        lineRateHz, oversample, lineDoubled);
    if (divider == 0)
        return false;
    Adc::applyDivider(divider);
    return true;
}

// The counter agrees with the divider in force, which is a working chip: the
// duty divides HLOW_LEN by that divider, so a counter on another clock makes
// the ratio meaningless. A case about that disagreement seeds it with
// seedSource().
static void seedSourceLines(uint16_t lines)
{
    const uint16_t divider = Adc::dividerInForce();
    Wire.reset();
    Wire.bank[0][0x1B] = (uint8_t)(lines & 0xFF);
    Wire.bank[0][0x1C] = (uint8_t)((lines >> 8) & 0x07);
    Wire.bank[0][0x17] = (uint8_t)(divider & 0xFF);
    Wire.bank[0][0x18] = (uint8_t)((divider >> 8) & 0x0F);
    g_log.clear();
    g_lines.clear();
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


static void seedHPeriod(uint16_t hperiod)
{
    Wire.bank[0][0x06] = (uint8_t)(hperiod & 0xFF);
    Wire.bank[0][0x07] = (uint8_t)((hperiod >> 8) & 0x01);
}

// The three source-side reads at once, plus the divider held over from the
// mode before. seedSourceLines() resets the bus, so the order matters.
static void seedSource(uint16_t lines, uint16_t lineSamples, uint16_t divider)
{
    seedSourceLines(lines);
    Wire.bank[0][0x17] = (uint8_t)(lineSamples & 0xFF);
    Wire.bank[0][0x18] = (uint8_t)((lineSamples >> 8) & 0x0F);
    Adc::applyDivider(divider);
}

// The bench: RiscPC at 320x256@50, VTOTAL 311, so 311 x 50 = 15550 lines/sec.
// PLLAD_MD 2553 and IF_HSYNC_RST 1276 are what the unit actually holds.
static const uint32_t BenchLineRate = 15550;
static const uint16_t BenchDivider = 2553;

TEST_CASE("the ADC has a rated sampling ceiling and the divider must respect it")
{
    // DS-5725-3.2: "Maximum analog sampling rate up to 162MSPS". The rate the
    // part CONVERTS at is PLLAD_MD x line rate x the oversampling installed,
    // and Adc::postDividerFor() picks the crossover row from the first two of
    // those -- so the row reduces the oversampling as the clock rises, and a
    // ceiling is only meaningful alongside the ratio it was taken at.
    SUBCASE("the bench is inside the limit, but only just") {
        // 2553 x 15550 x 4. That is 98.0% of the 162 MSPS rating, and the row
        // at a 39.7 MHz clock does carry four times.
        CHECK(Adc::sampleRateHz(BenchDivider, BenchLineRate, 4) == 158796600u);
        CHECK(Adc::withinLimit(BenchDivider, BenchLineRate, 4));
    }

    SUBCASE("what caps a slow line is the crossover row, not the rating") {
        // 162 MSPS buys 10418 dividers at the bench line rate and PLLAD_MD
        // holds twelve bits, but four times oversampling needs CKO under
        // 40 MHz, which is 2572 of them.
        CHECK(Adc::maxDivider(BenchLineRate, 4)
              == Adc::maxCkoFor(4) / BenchLineRate);
        CHECK(Adc::maxDivider(BenchLineRate, 1) == Adc::DividerMax);
    }

    SUBCASE("a divider the rating refuses is one whose CLOCK is over it") {
        // 2553 at 31.5 kHz is a clock of 80.4 MHz, and the row there carries no
        // oversampling at all -- so the part converts at 80.4 MSPS and is well
        // inside the rating. Reaching the rating takes a clock over 162 MHz.
        CHECK(Adc::withinLimit(BenchDivider, 31500, 4));
        CHECK_FALSE(Adc::withinLimit(
            Adc::DividerMax, 45000, 4));
    }

    SUBCASE("a ceiling is the largest divider that installs the ratio asked for") {
        // Each row's ceiling is half the one above it, so the ceilings halve as
        // the ratio doubles and every row tops out at the same conversion rate.
        CHECK(Adc::maxDivider(31500, 4) * 2 <= Adc::maxDivider(31500, 2) + 1);
        CHECK(Adc::maxDivider(63960, 2) * 2 <= Adc::maxDivider(63960, 1) + 1);

        for (uint8_t ratio = 1; ratio <= 4; ratio = (uint8_t)(ratio * 2)) {
            const uint16_t ceiling = Adc::maxDivider(31500, ratio);
            CHECK(Adc::oversampleFor(
                      Adc::postDividerFor((uint32_t)ceiling * 31500), ratio) == ratio);
        }
    }
}

// A divider the OUTPUT cannot show buys nothing. The VDS magnifies and cannot
// minify, so samples beyond what the raster can display are cropped, clipped or
// thrown away whichever mechanism handles them -- while costing the ADC clock
// that a lower divider would spend on oversampling instead. Measured on the
// bench: 640x480@75 into a 1280 raster solved to a capture of 1448 against a
// window of 1176 and arrived corrupt; held at a divider whose capturable fits,
// the same framing is exact and the scale sits at unity.
// docs/investigations/the-capture-may-not-outgrow-the-raster.md
TEST_CASE("a divider whose line the output cannot show is not recommended")
{
    const uint32_t LineRate = 37500;          // 640x480 at 75 Hz

    const uint16_t unbounded = SamplingClock::recommendedDivider(LineRate, 4, false);
    const uint16_t bounded = SamplingClock::recommendedDivider(LineRate, 4, false, 1470);

    CHECK(bounded <= 1470);
    CHECK(bounded < unbounded);

    SUBCASE("and a ceiling of zero is no ceiling, so callers without a raster are unchanged") {
        CHECK(SamplingClock::recommendedDivider(LineRate, 4, false, 0) == unbounded);
    }

    SUBCASE("the divider stays even, so the line counter still divides exactly") {
        CHECK(bounded % 2 == 0);
    }

    SUBCASE("a ceiling above what the ADC affords does not raise the divider") {
        CHECK(SamplingClock::recommendedDivider(LineRate, 4, false, 4000) == unbounded);
    }
}


TEST_CASE("the divider spends the conversion budget the ADC is rated for")
{
    // The rating is a budget, and the crossover row fixes the exchange rate
    // between the two things it can be spent on: CKO under 40 MHz installs four
    // times oversampling, under 80 MHz installs two, and each row ceiling is
    // half the one above it -- so every row tops out at the same conversion
    // rate. A solve that leaves the part converting at half its rating has
    // given away density or filtering and bought nothing with either.
    struct Bench { uint32_t lineRate; bool doubled; };
    const Bench benches[] = {
        {BenchLineRate, true},      // the RiscPC at 320x256, line doubled
        {38135, false},             // the same machine at 800x600, undoubled
    };

    for (unsigned i = 0; i < sizeof(benches) / sizeof(benches[0]); ++i) {
        const uint32_t rate = benches[i].lineRate;
        const uint16_t chosen =
            SamplingClock::recommendedDivider(rate, 4, benches[i].doubled);
        REQUIRE(chosen != 0);

        const uint8_t installed =
            Adc::oversampleFor(Adc::postDividerFor((uint32_t)chosen * rate), 4);
        const uint32_t spent = Adc::sampleRateHz(chosen, rate, installed);

        CHECK(spent <= Adc::MaxSampleRateHz);
        // Unless a bound leaves it unspendable: a doubled line stops at the
        // tail band's onset, and the samples the rating would still afford
        // past it come back green.
        const bool bounded =
            chosen == SamplingClock::DoubledLineSampleLimit;
        CHECK((bounded || spent > (Adc::MaxSampleRateHz / 100) * 87));
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

    SUBCASE("the tail band is what binds a doubled line, not the ADC row") {
        // Four times oversampling needs CKO under 40 MHz and 98% of that row's
        // ceiling would give 2520 here, but the capture path stops writing
        // video part way along a doubled line and the rest of it comes back
        // dark green. Both rates land on the cap rather than on a row.
        CHECK(SamplingClock::recommendedDivider(BenchLine, Oversample, true)
              == SamplingClock::DoubledLineSampleLimit);
        CHECK(SamplingClock::recommendedDivider(31500, Oversample, true)
              == SamplingClock::DoubledLineSampleLimit);
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
        // Read at 800x600's undoubled line rate, the one rate on this bench
        // where the rating is what binds: a doubled line stops at the tail
        // band and a slow undoubled one at the eleven-bit line counter, and
        // either would report that bound in place of the backoff.
        const uint32_t Undoubled = 38135;
        uint16_t chosen = SamplingClock::recommendedDivider(Undoubled, Oversample, false);
        CHECK(chosen > (uint16_t)(Adc::maxDivider(Undoubled, Oversample) * 0.97f));
        CHECK(Adc::withinLimit(chosen, Undoubled, Oversample));
    }

    SUBCASE("and at the bench rate it stays inside every bound at once") {
        // The shipped tables ran 2269..2559 here, all twelve hard against the
        // four-times row. That is above the tail band's onset, so it is no
        // longer the target: what the divider must still clear is enough
        // density to carry the source, and the floor is Nyquist on its 512 px
        // line -- 2 IF units a pixel, which is 2048 ADC samples doubled.
        uint16_t chosen = SamplingClock::recommendedDivider(BenchLine, Oversample, true);
        CHECK(chosen >= 2048);
        CHECK(chosen <= SamplingClock::DoubledLineSampleLimit);
        CHECK(chosen / 2 <= InputFormatter::LineCounterMax);
        CHECK(Adc::withinLimit(chosen, BenchLine, Oversample));
    }

    SUBCASE("a line rate nobody can measure yields nothing, not a guess") {
        // A divider written from a zero measurement is how the screen goes
        // green. SourceMeasurement has no business inventing one.
        CHECK(SamplingClock::recommendedDivider(0, Oversample, true) == 0);
    }
}

TEST_CASE("a doubled line is held under the count that starts the tail band")
{
    // The capture path writes video for a fixed count of ADC samples from the
    // start of the line and dark green after it, and picture reaching the green
    // is destroyed rather than overlaid.
    //
    // Measured on the bench with the VDS line filter bypassed, which every
    // earlier reading of this band was confounded by: RiscPC 320x256@50 and
    // 320x250@50, both line doubled at PLLAD_MD 2504 and 2506, put the onset at
    // ADC sample 2236..2256. Two capture-window starts 24 units apart and two
    // magnifications place it at the same position in the LINE rather than at a
    // width from the window, and undoubled lines of 1217, 1447 and 1561 IF
    // units show no band anywhere -- which is what rules out a bound in IF
    // units and leaves a count only a doubled line's divider can reach.
    // docs/investigations/tail-green.md
    const uint16_t Onset = 2236;

    for (uint32_t rate : {15550u, 15625u, 15750u, 21000u})
        CHECK(SamplingClock::recommendedDivider(rate, 4, true) < Onset);
}

TEST_CASE("an undoubled line takes the most samples, and oversamples if that is free")
{
    // The kept count is what carries the source's pixels, and oversampling
    // carries none of them -- it buys freedom from aliasing, on a conversion
    // rate the decimator reduces again. So the samples are the objective and
    // the ratio is taken only where it costs none of them.
    // 2046 is the eleven-bit line counter, even: the exact bound, with no
    // backoff, because no measurement error can move a counter. What the ADC's
    // rating affords is what the backoff guards, and it is higher than this for
    // every rate here. The engine bounds the divider again by the raster, which
    // is not this function's business.
    struct Case { uint32_t rate; uint16_t divider; uint8_t ratio; };
    const Case cases[] = {
        {31468, 2046, 2},   // 480p: two times reaches the ceiling, so it is free
        {35156, 2046, 2},   // 800x600@56
        {37880, 2046, 2},   // 800x600@60
        {48360, 2046, 1},   // 1024x768@60: two times would cost samples
        {63981, 2046, 1},   // 1280x1024@60
        {67500, 2046, 1},   // 1920x1080@60
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const uint16_t chosen = SamplingClock::recommendedDivider(cases[i].rate, 4, false);
        CAPTURE(cases[i].rate);
        CHECK(chosen == cases[i].divider);
        CHECK(Adc::oversampleFor(Adc::postDividerFor((uint32_t)chosen * cases[i].rate), 4)
              == cases[i].ratio);
    }
}

TEST_CASE("a doubled source keeps the oversampling instead of the samples")
{
    // A doubled line is a short one -- the doubler only runs on a source with
    // lines to spare -- so it is already far past its own Nyquist, 245% at the
    // bench source's htotal. Samples beyond that carry nothing the source has,
    // and the crossover row's filtering is the better spend.
    const uint16_t chosen = SamplingClock::recommendedDivider(BenchLineRate, 4, true);
    const uint8_t installed =
        Adc::oversampleFor(Adc::postDividerFor((uint32_t)chosen * BenchLineRate), 4);

    CHECK(installed == 4);
    CHECK(InputFormatter::lineCounterFor(chosen, true)
          < InputFormatter::LineCounterMax);
}

TEST_CASE("the divider is capped so the line counter can hold it")
{
    // IF_HSYNC_RST is eleven bits, and a line past it WRAPS rather than
    // clamping -- the counter restarts mid-line and the picture repeats. The
    // divider is what decides the line length, which makes it the lever.
    uint16_t chosen = SamplingClock::recommendedDivider(BenchLineRate, 4, true);

    CHECK(InputFormatter::lineCounterFor(chosen, true) <= InputFormatter::LineCounterMax);

    SUBCASE("and the ADC rating still binds where it is the tighter of the two") {
        // 90 kHz has room for 1764 under the rating, inside the write limit.
        CHECK(SamplingClock::recommendedDivider(90000, 4, true) == 1764);
    }

    SUBCASE("it is still even, so the IF line divides exactly") {
        CHECK(chosen % 2 == 0);
    }

    SUBCASE("and an UNDOUBLED line is bounded the same way") {
        // IF_HSYNC_RST is PLLAD_MD halved only where the line is doubled. With
        // the doubler off an IF unit is one ADC sample, so the same 1125-unit
        // write limit bounds the divider at half the value -- and a divider
        // chosen for the doubled case strides the playback across a buffer that
        // only ever received 1125 units, which wraps every line sideways.
        // Measured on the unit at 576p: PLLAD_MD 1180, IF_HSYNC_RST 1180,
        // PB_CAP_OFFSET 296, and the card torn and duplicated.
        const uint16_t flat = SamplingClock::recommendedDivider(BenchLineRate, 4, false);
        CHECK(InputFormatter::lineCounterFor(flat, false)
              <= InputFormatter::LineCounterMax);
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

// A positive-going hsync reaches the counter as the line MINUS the pulse, so
// everything derived from the pulse width is the complement of what the source
// sends: measured on the bench at 800x600@60, HLOW 882 of HTOTAL 1003 against
// the 122 the mode states, and SourceTiming's duty match is 0.015 wide.
//
// Normalising inside measureDuty() alone cannot break that, because the rate
// has to be measured first and the rate measurement is what the wrong polarity
// defeats. The engine then never leaves the recovery ladder: measured with
// STATUS_SYNC_PROC_VTOTAL reading the source correctly throughout, the ADC PLL
// never locking, and the unmeasured-pass counter climbing without ever resetting.
TEST_CASE("the hsync polarity is normalised before the rate is measured")
{
    Adc::applyDivider(BenchDivider);
    seedSource(311, BenchDivider, BenchDivider);
    Wire.bank[0][0x16] = 0x03;   // STATUS_SYNC_PROC_HSPOL | HSACT -- positive, found

    SourceMeasurement measurement(inputFormatter);
    measurement.measureRate();

    CHECK(SyncProcessor::SP_HS_INV_REG::read() == 1);
}

TEST_CASE("a solved divider is held, and every register follows from it")
{
    Wire.reset();
    SourceMeasurement sampling(inputFormatter);

    // The reset state is the bring-up clock, so a solve has to REPLACE a
    // divider rather than fill an empty one -- and the bring-up value must not
    // be mistaken for something a measurement chose.
    Adc::applyResetParameters();
    CHECK(Adc::dividerInForce() == Adc::BringUpDivider);

    REQUIRE(solveSampling(BenchLineRate, 4));
    CHECK(Adc::dividerInForce() != Adc::BringUpDivider);

    const uint16_t chosen = Adc::dividerInForce();
    CHECK(chosen == SamplingClock::recommendedDivider(BenchLineRate, 4, true));
}

TEST_CASE("an unmeasurable line rate leaves the previous choice alone")
{
    Wire.reset();
    SourceMeasurement sampling(inputFormatter);
    REQUIRE(solveSampling(BenchLineRate, 4));
    const uint16_t chosen = Adc::dividerInForce();

    // getSourceFieldRate() reports 0 with no lock, and that reaches here. A
    // divider written from a measurement that did not happen is how the screen
    // goes green -- and it takes the sync processor with it, so there is no
    // picture left to diagnose from.
    CHECK_FALSE(solveSampling(0, 4));
    CHECK(Adc::dividerInForce() == chosen);
}

// A rate that moved while the line count did not is a reading taken while the
// source was still settling, not a new mode. The bench transient: locked at 311
// lines / 50.08 Hz, a solve run across a preset load reads 57.9 Hz and sizes
// PLLAD_MD 2204 where 2548 is due. The count is the reliable half.
TEST_CASE("a rate that moves while the line count does not is a settling reading")
{
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(rateMeasured(measurePastGate(measurement)));

    SUBCASE("nothing held yet cannot contradict anything") {
        SourceMeasurement fresh(inputFormatter);
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
        CHECK(measureOnce(measurement) == SourceMeasurement::Unmeasurable);
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

    CHECK(InputFormatter::lineCounterFor(doubled, true) <= InputFormatter::LineCounterMax);
    CHECK(InputFormatter::lineCounterFor(progressive, false) <= InputFormatter::LineCounterMax);
}

// --- the line rate, measured off the chip ------------------------------------


TEST_CASE("the line rate is measured rather than handed in")
{
    // Field rate x source lines, the two quantities the divider is a function
    // of, both read where they live rather than passed down from the sketch.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement(inputFormatter);
    CHECK(rateMeasured(measurePastGate(measurement)));
    CHECK(measurement.sourceLines() == 311);
    CHECK(measurement.lineRateHz() == 15624u);

    SUBCASE("and it holds both inputs, because nothing downstream can say which was wrong") {
        CHECK(measurement.fieldRateHz() > 50.0f);
        CHECK(measurement.fieldRateHz() < 50.1f);
    }

    SUBCASE("the diagnostic names both inputs and the result") {
        CHECK(logged("sampling: 311 lines x 50.08 Hz -> line rate 15624"));
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

    SourceMeasurement measurement(inputFormatter);
    REQUIRE(rateMeasured(measurePastGate(measurement)));

    g_fieldRate = 57.9f;
    CHECK(measureOnce(measurement) == SourceMeasurement::Unmeasurable);

    // The refusal is the return value, not a zeroed rate: the last rate that
    // passed the cross-check stands, so a reader through a sync loss gets the
    // last one believed.
    CHECK(measurement.lineRateHz() == 15624u);

    SUBCASE("and reports what it saw, both halves") {
        CHECK(measurement.sourceLines() == 311);
        CHECK(logged("sampling: 311 lines x 57.90 Hz -> line rate 0"));
    }

    SUBCASE("an unmeasurable field rate is refused the same way") {
        g_fieldRate = 0.0f;
        CHECK(measureOnce(measurement) == SourceMeasurement::Unmeasurable);
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
    SourceMeasurement measurement(inputFormatter);

    g_fieldRateCalls = 0;
    for (uint8_t i = 1; i < SourceMeasurement::SteadySamples; ++i) {
        CAPTURE(i);
        CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
    }
    CHECK(g_fieldRateCalls == 0);
    CHECK(measureOnce(measurement) != SourceMeasurement::NotSteady);

    SUBCASE("and stays steady while the count does") {
        CHECK(measureOnce(measurement) != SourceMeasurement::NotSteady);
    }
}

TEST_CASE("a count that moves starts the run again")
{
    seedSourceLines(311);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    // Mid-change. A one-sample blip is normal; what matters is that it does not
    // report steady on the strength of the run before it.
    seedSourceLines(97);
    CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
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
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.26f;

    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measureToFirstReading(measurement) == SourceMeasurement::Settling);

    SUBCASE("a rate that lands somewhere else has not repeated either") {
        g_fieldRate = 49.92f;
        CHECK(measureOnce(measurement) == SourceMeasurement::Settling);
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
            CHECK(measureOnce(measurement) == SourceMeasurement::Settling);
        }
        g_fieldRate = 49.5f;
        CHECK(measureOnce(measurement) == SourceMeasurement::Measured);
    }

    SUBCASE("the same rate twice is the source holding still") {
        CHECK(measureOnce(measurement) == SourceMeasurement::Measured);
    }

    SUBCASE("and the reading noise of a settled source is not a change") {
        // Two readings of one field period at the ESP's clock, so they differ
        // in the last place. The band is a tenth of a percent: ten times that
        // noise, and a third of the smallest settling error seen.
        g_fieldRate = 50.26f * 1.0005f;
        CHECK(measureOnce(measurement) == SourceMeasurement::Measured);
    }
}

TEST_CASE("no reading the key is cut from is one sample")
{
    // A single reading off the debug pin decides the whole number of hertz the
    // key carries and the raster is generated from, and the key cannot be
    // re-chosen afterwards: identity is wider than the rounding, so every later
    // correct reading compares equal and the raster stays where the outlier put
    // it. Measured at 320x256@50, a 51.1 Hz sample keys 51 and the raster
    // solves 1882 where 1920 is due.
    // docs/investigations/single-sample-rate-jitter.md
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);

    SourceMeasurement measurement(inputFormatter);

    SUBCASE("the reading the source settles on") {
        g_fieldRate = 50.08f;
        g_fieldRates = {51.14f, 50.08f, 50.08f};
        REQUIRE(measureToFirstReading(measurement) == SourceMeasurement::Settling);
    }

    SUBCASE("and the one taken as it stands once the attempts run out") {
        g_fieldRate = 50.26f;
        REQUIRE(measureToFirstReading(measurement) == SourceMeasurement::Settling);

        // Nothing agrees, so the attempts run out. One is already spent above.
        for (uint8_t i = 2; i < SourceMeasurement::RateAgreementAttempts; ++i) {
            CAPTURE(i);
            g_fieldRate = 50.0f + (float)i * 0.1f;
            REQUIRE(measureOnce(measurement) == SourceMeasurement::Settling);
        }

        g_fieldRate = 50.08f;
        g_fieldRates = {51.14f, 50.08f, 50.08f};
        REQUIRE(measureOnce(measurement) == SourceMeasurement::Measured);
    }

    CHECK(measurement.fieldRateHz() == doctest::Approx(50.08f).epsilon(0.001f));
}

TEST_CASE("a mode change abandons the field rate it had agreed on")
{
    // The rate is about to move, so a reading from the mode before it must not
    // be the one the next reading agrees with.
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measureToFirstReading(measurement) == SourceMeasurement::Settling);

    measurement.modeChanged();

    CHECK(measureToFirstReading(measurement) == SourceMeasurement::Settling);
}

TEST_CASE("a count outside what any source runs never settles")
{
    // 97 and 98 are what a preset load leaves behind, and they are steady --
    // steadiness alone would call that settled and solve against it.
    seedSourceLines(97);
    SourceMeasurement measurement(inputFormatter);

    for (uint8_t i = 0; i < 3 * SourceMeasurement::SteadySamples; ++i)
        CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
}

TEST_CASE("a mode change abandons the run rather than counting through it")
{
    seedSourceLines(311);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    measurement.modeChanged();
    CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
}

TEST_CASE("a near-integer multiple of the divider is a PLL counting several lines")
{
    // The trap's signature: the sync processor reports a count too low and the
    // samples per line too high, because the PLL locked to every other hsync
    // and counts one line per two sent. The count alone cannot show it -- 155
    // is simply not a source -- so the sample count against the divider is what
    // recovers the real 310.
    SourceMeasurement sampling(inputFormatter);

    seedSource(155, 2249, 1124);
    CHECK(sampling.readSourceLines() == 310);

    SUBCASE("and four lines to a count likewise") {
        // STATUS_SYNC_PROC_HTOTAL is 12 bits, so four lines to a count is only
        // reachable on a divider small enough for the product to fit.
        seedSource(78, 4000, 1000);
        CHECK(sampling.readSourceLines() == 312);
    }

    SUBCASE("a count that is already a source is taken as it stands") {
        seedSource(311, 2553, 2553);
        CHECK(sampling.readSourceLines() == 311);
    }

    SUBCASE("an unlocked sync processor is not a multiple of anything") {
        // 2558 against 2553 is a five-sample offset, arithmetically incapable
        // of looking like a multiple: the nearest is 5106. So nothing corrects
        // the count and it comes back as read.
        seedSource(97, 2558, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }

    SUBCASE("nor is a reading simply unrelated to the divider") {
        // 2400 with the sync processor unconfigured.
        seedSource(97, 2400, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }

    SUBCASE("beyond the multiples any offset can be made to fit one") {
        seedSource(155, 5620, 1124);
        CHECK(sampling.readSourceLines() == 155);
    }

    SUBCASE("and nothing is a multiple of nothing") {
        seedSource(155, 0, 1124);
        CHECK(sampling.readSourceLines() == 155);
        seedSource(155, 2249, 0);
        CHECK(sampling.readSourceLines() == 155);
    }
}

// --- escaping a divider the source cannot lock to ----------------------------

TEST_CASE("the multiple tolerates the jitter of every line it counts")
{
    // One counted line carries the jitter of k source lines, so the window
    // scales with k rather than being the latch check's fixed two samples.
    // Measured: 2251 against a divider of 1124, where twice is 2248.
    SourceMeasurement sampling(inputFormatter);

    seedSource(155, 2251, 1124);
    CHECK(sampling.readSourceLines() == 310);

    seedSource(155, 2247, 1124);
    CHECK(sampling.readSourceLines() == 310);

    SUBCASE("and widening it does not reach the readings that are not multiples") {
        seedSource(97, 2558, 2553);
        CHECK(sampling.readSourceLines() == 97);
    }
}

// What an output frame of this many lines can display, which is the question
// the doubling asks: the part cannot minify, so this is the ceiling.

TEST_CASE("a 15 kHz line is recognised by its rate, not by a standard's number")
{
    // SP_H_PULSE_IGNOR and the coast window both key on a line whose vertical
    // interval carries equalisation and serration pulses. That is a property of
    // the rate, and a source is filed under a standard whose number does not
    // carry it: a scaled RGBHV source runs a 15 kHz line and is filed as 480p,
    // because that is the branch it borrows.
    SourceMeasurement measurement(inputFormatter);

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

TEST_CASE("the field rate answers even where HPERIOD_IF is healthy and agrees")
{
    // Measured at 800x600@60: HPERIOD_IF reads 176, implying 38135 Hz where the
    // field rate gives 37878 -- and DMT states 37879. The two agree inside the
    // 2% the corroboration allowed, so the counter won, putting the source's
    // rate a whole hertz out: 60.72 where the field rate reads 60.32. The key
    // is rounded to a whole hertz and the raster is generated from it, so which
    // measurement happened to answer decided the framing.
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(627);
    g_fieldRate = 60.3165f;

    REQUIRE(rateMeasured(measurePastGate(sampling)));

    CHECK(sampling.lineRateHz() == 37878u);
}



TEST_CASE("a refused HPERIOD_IF run falls back to the field rate")
{
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(524);
    // 50 against 524 lines is 132 kHz, a 252 Hz field rate: the railing's
    // stable form, which no amount of agreement can reject.
    g_fieldRate = 60.0f;
    g_fieldRateCalls = 0;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    // 525 x 60: VTOTAL is zero based and 640x480@60 is a 525-line frame, so
    // the count of 524 is the standard's own number less one.
    CHECK(sampling.lineRateHz() == 31500u);
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
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    REQUIRE(rateMeasured(measurePastGate(sampling)));

    // 50.08 x 312, which is what the source runs at, not the 24725 the counter
    // states.
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
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(311);
    g_fieldRate = 0.0f;

    CHECK_FALSE(rateMeasured(measurePastGate(sampling)));
    CHECK(sampling.lineRateHz() == 0u);
}

TEST_CASE("a source that did not pulse is not timed three times over")
{
    // A sample that reports nothing waited out FS_SAMPLE_TIMEOUT_MS twice, so a
    // silent source costs half a second of loop() a pass. The median is worth
    // three timings of a source that is pulsing and none of one that is not:
    // the second and third have the same nothing to time.
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    REQUIRE(rateMeasured(measurePastGate(sampling)));

    g_fieldRate = 0.0f;
    g_fieldRateCalls = 0;
    CHECK_FALSE(rateMeasured(measureOnce(sampling)));

    // One sample, which times the pin again itself when the first reports none.
    CHECK(g_fieldRateCalls == 2);
}

TEST_CASE("a refusal does not spend the rejection budget")
{
    // HeldRateRejectionLimit exists so a source that genuinely changed rate at
    // an unchanged count cannot hold the mode change open for ever. A reading
    // that was never measured is not such a source, and counting it there
    // spends the escape hatch on nothing.
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    REQUIRE(sampling.lineRateHz() == 15624u);

    g_fieldRate = 0.0f;
    for (unsigned i = 0; i < 2u * SourceMeasurement::HeldRateRejectionLimit; ++i)
        CHECK_FALSE(rateMeasured(measurePastGate(sampling)));

    CHECK(sampling.lineRateHz() == 15624u);
}

TEST_CASE("a reading implying a line no television generates is refused")
{
    // The railed values sit below the floor: 511 on a 311-line source is
    // 13.2 kHz against the 15625 the 431 it is due gives. Nothing legitimate is
    // lost -- the slowest line here is 15.625 kHz. Refused, the field rate is
    // what answers instead.
    SourceMeasurement sampling(inputFormatter);
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    g_fieldRateCalls = 0;

    REQUIRE(rateMeasured(measurePastGate(sampling)));
    CHECK(g_fieldRateCalls > 0);
    CHECK(sampling.lineRateHz() == 15624u);

    SUBCASE("and one just under the floor likewise") {
        SourceMeasurement other(inputFormatter);
        seedSourceLines(311);
        g_fieldRateCalls = 0;
        REQUIRE(rateMeasured(measurePastGate(other)));
        CHECK(g_fieldRateCalls > 0);
    }
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
    SourceMeasurement measurement(inputFormatter);
    seedSourceLines(310);
    seedSourceHalfLines(624);
    seedInterlaced();

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a count as large as the half-line total is the serrations")
{
    SourceMeasurement measurement(inputFormatter);
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
    SourceMeasurement measurement(inputFormatter);
    seedSourceLines(524);
    seedSourceHalfLines(524);

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a progressive source cannot have counted the serrations")
{
    // A progressive source has no field and frame to differ, so nothing can
    // double its count however the witness reads.
    SourceMeasurement measurement(inputFormatter);
    seedSourceLines(607);
    seedSourceHalfLines(624);

    CHECK(measurePastGate(measurement) != SourceMeasurement::Serrations);
}

TEST_CASE("a half-line total that measures nothing refuses to judge the count")
{
    // VPERIOD_IF is debris on a separate-sync source, where it reads values
    // like 20 against a true 311. Judged against that, any count at all looks
    // nearer the total than half of it.
    SourceMeasurement measurement(inputFormatter);
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
    SourceMeasurement measurement(inputFormatter);

    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples * 3; ++i) {
        CAPTURE(i);
        CHECK_FALSE(rateMeasured(measureOnce(measurement)));
    }
}

TEST_CASE("a field count goes steady with the witness live")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement(inputFormatter);

    CHECK(measurePastGate(measurement) != SourceMeasurement::NotSteady);
}

TEST_CASE("the reason a serration count was refused is available to the caller")
{
    // The engine cannot tell "not settled yet" from "settled on the wrong
    // count" by the return value alone, and only the second is worth acting on.
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();
    SourceMeasurement measurement(inputFormatter);

    CHECK(measurePastGate(measurement) == SourceMeasurement::Serrations);
}

TEST_CASE("a count still gathering samples is not reported as serrations")
{
    seedSourceLines(310);
    seedSourceHalfLines(624);
    SourceMeasurement measurement(inputFormatter);

    CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
}

TEST_CASE("a good count clears a serration verdict")
{
    seedSourceLines(607);
    seedSourceHalfLines(624);
    seedInterlaced();
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) == SourceMeasurement::Serrations);

    seedSourceLines(310);
    seedSourceHalfLines(624);
    seedInterlaced();

    // A completed run at the new count is what clears it. The verdict stands
    // while the run is still re-gathering, because that is the state the coast
    // was widened for and one good sample does not undo it.
    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        reading = measureOnce(measurement);

    CHECK(reading != SourceMeasurement::Serrations);
}

TEST_CASE("a finer line never buys fewer samples than a coarser one")
{
    // The crossover row that installs four times oversampling tops out at a
    // lower divider than the row below it, so a rule that maximises the
    // conversion rate alone hands a high-resolution source FEWER samples per
    // line than a low-resolution one. That is backwards: density is what the
    // picture carries.
    const uint32_t SdLine = 15625;       // 320x256@50, line doubled
    const uint32_t VesaLine = 38135;     // 800x600@60, undoubled

    const uint16_t sd = SamplingClock::recommendedDivider(SdLine, 4, true);
    const uint16_t vesa = SamplingClock::recommendedDivider(VesaLine, 4, false);

    SUBCASE("the finer source gets at least as many samples per line") {
        CHECK(vesa >= sd / 2);
    }

    SUBCASE("and more than the four-times row alone would allow it") {
        CHECK((uint32_t)vesa * VesaLine > Adc::maxCkoFor(4));
    }

    SUBCASE("the IF's 11-bit geometry registers are a wall above both") {
        // IF_HSYNC_RST, IF_HB_ST2 and IF_HB_SP2 are all [10:0]. PLLAD_MD 2094
        // was accepted, latched and read back correctly at
        // STATUS_SYNC_PROC_HTOTAL while IF_HSYNC_RST held 46 -- 2094 modulo
        // 2048 -- with the picture destroyed and nothing reporting a fault.
        CHECK(InputFormatter::lineCounterFor(vesa, false)
              <= InputFormatter::LineCounterMax);
        CHECK(InputFormatter::lineCounterFor(sd, true)
              <= InputFormatter::LineCounterMax);
        CHECK(InputFormatter::lineCounterFor(
                  SamplingClock::recommendedDivider(20000, 1, false), false)
              <= InputFormatter::LineCounterMax);
    }
}

TEST_CASE("the sampling budget is spent at the rate the ADC actually converts at")
{
    // Adc::oversampleFor() reduces a request the crossover row cannot give, and
    // the row is chosen from the divider's own clock -- so a ceiling and the
    // ratio it was taken at are one answer. Asking at a ratio the row will not
    // install describes a configuration the part is never in.

    SUBCASE("a ceiling installs the ratio it was asked at") {
        const uint16_t ceiling = Adc::maxDivider(31500, 4);
        CHECK(Adc::oversampleFor(
                  Adc::postDividerFor((uint32_t)ceiling * 31500), 4) == 4);
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
// --- the composite count is short by the vertical sync, and VPERIOD restores it
//
// The sync processor counts hsync edges between vertical syncs, and an
// unserrated composite source sends none through the vertical pulse -- so the
// count comes up short by exactly that pulse and the same source reads a
// different length on each sync type. Measured on one machine and one cable,
// 320x256@50 reads 311 separate and 308 composite, 640x480@60 reads 524 and
// 522, and each stored a framing of its own against one source.
//
// VPERIOD_IF measures the whole frame, so the two reconcile: whichever of one
// or two puts VPERIOD a small non-negative distance above the counted frame is
// the valid reading, and that distance is what the counter lost. Confirmed
// across twelve modes at widths 2, 3, 4 and 6, with both factors appearing and
// every frame total exact.
// docs/investigations/the-risc-pc-composite-sync-is-not-serrated.md

TEST_CASE("the restored count holds through a torn vertical period")
{
    // VPERIOD_IF spans two registers and a counter can advance between the
    // byte fetches, so a reading tears: 800x600@60 on composite gave 1255 in
    // 539 of 721 samples from loop() with 615 and 1267 among the rest. A
    // reconciliation recomputed per sample flips the count between restored
    // and raw, which is further than a steady run tolerates and leaves the
    // source never settling.
    seedSourceLines(308);
    seedSourceHalfLines(623);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);
    REQUIRE(measurement.sourceLines() == 311);

    seedSourceHalfLines(615);
    for (uint8_t pass = 0; pass < 8; pass++)
        measureOnce(measurement);

    CHECK(measurement.sourceLines() == 311);
}

TEST_CASE("the composite count is restored to the frame the source sends")
{
    seedSourceLines(308);
    seedSourceHalfLines(623);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    CHECK(measurement.sourceLines() == 311);
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
        reading = measureOnce(measurement);
        // The scan decision keeps its own run, and on the unit both advance on
        // the same pass. A settling helper that drives only the solve leaves it
        // reporting a source nothing has sampled.
        measurement.measureScanType();
    }
    return reading != SourceMeasurement::NotSteady;
}

TEST_CASE("a count alternating by one settles instead of running for ever")
{
    SourceMeasurement measurement(inputFormatter);

    CHECK(settleAlternating(measurement, 259, 8));
}

TEST_CASE("the pair's higher count is the one settled on")
{
    // Both values undercount the true field -- 259.5 against 262.5 on a Wii at
    // 480i -- so the higher of the pair is the closer of the two.
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(settleAlternating(measurement, 259, 8));

    CHECK(measurement.steadyLines() == 260);
}

TEST_CASE("a count alternating by one reads as interlaced where the period cannot")
{
    // Separate sync: STATUS_IF_VT_OK is 0 and VPERIOD_IF holds debris, so
    // scanTypeFor() has nothing and the alternation is all there is.
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(settleAlternating(measurement, 311, 8));

    CHECK(measurement.measureScanType() == SourceMeasurement::ScanInterlaced);
}

TEST_CASE("the alternation outranks whatever the period holds")
{
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(settleAlternating(measurement, 311, 8));

    seedSourceHalfLines(623);
    CHECK(measurement.measureScanType() == SourceMeasurement::ScanInterlaced);
}

// **THIS IS THE COST OF THE RULE, AND IT IS A REAL SOURCE.** A Wii at PAL 576i
// holds a steady 310 while genuinely interlaced -- 1186 samples, zero changes
// -- so the alternation misses it and it is steered as progressive. The
// deinterlacer's manual preference is what covers it, and the trade is
// deliberate: the reverse error engages motion adaption on a progressive
// source, which corrupts the picture rather than combing it.
TEST_CASE("a steady count is taken as progressive even where the source is not")
{
    seedSourceLines(310);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    CHECK(measurement.measureScanType() == SourceMeasurement::ScanProgressive);
}

// RiscPC 800x600@60 on composite sync: the count settles at 623 and never
// alternates, while VPERIOD_IF reads 1255. Parity read that odd value as a half
// line and so as an interlaced field, and the motion-adaptive deinterlacer
// engaged on a progressive source -- measured 721 samples from loop(), with
// MAPDT_VT_SEL_PRGV 0 and WFF/RFF_ENABLE 1 against a clean 640x480 reading 524.
TEST_CASE("a settled count that never alternates is progressive whatever the period holds")
{
    seedSourceLines(623);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);
    seedSourceHalfLines(1255);

    CHECK(measurement.measureScanType() == SourceMeasurement::ScanProgressive);
}

TEST_CASE("a count that moves by more than one still starts the run again")
{
    seedSourceLines(311);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    seedSourceLines(313);
    CHECK(measureOnce(measurement) == SourceMeasurement::NotSteady);
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
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    seedHsync(181, false);
    g_fieldRate = 50.08f;

    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && reading != SourceMeasurement::Measured; ++pass)
        reading = measureOnce(sampling);

    REQUIRE(reading == SourceMeasurement::Measured);
    CHECK(sampling.sourceLines() == 311);
    CHECK(sampling.lineRateHz() == 15624u);
    CHECK(sampling.hsync().syncDuty() == doctest::Approx(181.0f / (float)BenchDivider));
    CHECK_FALSE(sampling.hsync().syncAtHead());
}

// STATUS_SYNC_PROC_HLOW_LEN IS THE LOW TIME, NOT THE PULSE. RD-5725-1.1 S0_19:
// "Input H-sync low active pulse length (for H-sync polarity detection)" -- so
// on a high-active source it counts everything EXCEPT the pulse, and the duty
// is its complement.
//
// Taken raw it lands around 0.9, outside the range VideoSourceLine::forDuty()
// will accept, so every high-active source silently gets FallbackDuty and the
// capture window is placed from a guess. Measured on the bench, RiscPC on vga:
//
//     320x256@50   HSPOL 1   2330 / 2506 = 93.0%   complement 7.0%   mode file 7.03%
//     800x600@60   HSPOL 1   1376 / 1566 = 87.8%   complement 12.2%  mode file 12.12%
//     640x480@60   HSPOL 0    181 / 1566 = 11.6%                     mode file 11.75%
TEST_CASE("the hsync duty is the pulse whichever polarity the source sends")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, true);
    g_fieldRate = 50.08f;

    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);
    CHECK(sampling.hsync().syncDuty() == doctest::Approx(181.0f / (float)BenchDivider));
    CHECK(sampling.hsync().syncAtHead());
}

// The correction is a WRITE, and the sync processor owns it, so the count read
// afterwards is the pulse on both polarities rather than the pulse on one and
// its complement on the other. A high-active source left uncorrected reads
// around 0.9, which forDuty() refuses -- so uncorrected the capture window is
// placed from FallbackDuty on every such source.
TEST_CASE("a high-active source is normalised at the sync processor")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, true);
    g_fieldRate = 50.08f;

    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);
    CHECK(SyncProcessor::SP_HS_INV_REG::read() == 1u);
}

TEST_CASE("a low-active source is left alone")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.08f;

    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);
    CHECK(SyncProcessor::SP_HS_INV_REG::read() == 0u);
    CHECK(sampling.hsync().syncDuty() == doctest::Approx(181.0f / (float)BenchDivider));
    CHECK_FALSE(sampling.hsync().syncAtHead());
}

// THE COUNT IS ONLY THE PULSE WHILE THE PROCESSOR IS COUNTING THE LINE THE
// SAMPLES ARE DIVIDED BY. STATUS_SYNC_PROC_HTOTAL counts real ADC clocks per
// line, so locked it echoes the divider in force; while it disagrees, the low
// count was taken off a line of another length and the ratio means nothing.
//
// Measured across eight source modes in one sweep, 36 samples: the 26 with
// |htotal - divider| <= 1 gave a duty within 0.24 points of the mode's, and the
// 10 with |htotal - divider| >= 102 were out by -5.64 to +3.25 points. Nothing
// landed in between.
// docs/investigations/the-duty-is-counted-before-the-processor-relocks.md
TEST_CASE("a duty counted against a line the processor was not locked to is refused")
{
    SourceMeasurement sampling(inputFormatter);
    seedSource(311, 2148, 2250);
    Wire.sourceHsync(225, 2250, true);
    g_fieldRate = 50.08f;

    CHECK(measurePastGate(sampling) == SourceMeasurement::Settling);
}


// NOTHING READ THROUGH A CLOCK THAT HAS JUST BEEN LATCHED MEANS ANYTHING, AND
// THAT REACHES THE COUNT AS WELL AS THE DUTY. applySampleRate() writes the PLL
// group and latches it, and the readings are taken in the same pass -- before
// the PLL has relocked and before the sync processor has counted one line at
// the new rate. A divider the PLL has not settled on corrupts the line count
// too, by locking to every Nth hsync.
//
// Measured with SamplingLog through a source mode change: STATUS_SYNC_PROC_HTOTAL
// took 82 ms to echo a newly latched divider, reading 808 against 1124 in
// between, which is four passes at the detection cadence.
// STATUS_MISC_PLLAD_LOCK is not the gate -- on a settled source it dithers,
// 136 transitions in 1109 samples with the count exact throughout.
TEST_CASE("a reading through a clock that has just been latched is refused")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.08f;
    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);

    sampling.samplingClockLatched();

    CHECK(measureOnce(sampling) == SourceMeasurement::ClockSettling);
}

// And it is a bounded wait, not a gate something has to satisfy: the passes are
// counted off and the pass after them reads normally.
TEST_CASE("the wait after a latch is spent and the source is measured")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.08f;
    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);

    sampling.samplingClockLatched();
    for (uint8_t pass = 0; pass < SourceMeasurement::LatchSettlePasses; ++pass)
        REQUIRE(measureOnce(sampling) == SourceMeasurement::ClockSettling);

    // The processor now counts the divider it was given, which is what it does
    // once the PLL has settled on it.
    Wire.lockSyncProcessor();
    Wire.sourceHsync(159, BenchDivider, false);

    CHECK(measurePastGate(sampling) == SourceMeasurement::Measured);
}

// A DUTY IS WAITED FOR, NEVER GUESSED. A processor that never echoes the
// divider means the ratio was counted against a line of another length, so it
// carries nothing -- and the fallback that used to be taken after a floor of
// passes is a plausible-looking number in a register the window is sized from.
// Measured on the bench, the unlocked samples on a 7.03% source read 9.96% and
// 1.82%, which forDuty() accepts as real.
//
// The floor existed because waiting looked impossible: the reference clock was
// sized from a nominal field rate, which put the ADC PLL on a post divider row
// it could not hold, so a locked reading never arrived at all. With the clock
// sized from the rate measured, the duty is taken during the change on every
// pass. docs/investigations/the-duty-is-counted-before-the-processor-relocks.md
TEST_CASE("a duty the processor was not locked for never completes a measurement")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(2250);
    seedSource(311, 2148, 2250);
    Wire.sourceHsync(225, 2250, true);
    g_fieldRate = 50.08f;

    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint16_t pass = 0; pass < 200; ++pass)
        reading = measureOnce(sampling);

    CHECK(reading == SourceMeasurement::Settling);
    CHECK(sampling.hsync().syncDuty() == doctest::Approx(0.0f));
}

// What the gate is FOR: the capture window is a proportion of a region the duty
// places, so a duty that moves with the transition history moves the framing of
// a source that never changed. Measured on the bench mode, same cable and a
// live duty of 7.06% throughout, the capturable region read 1147 arriving from
// 640x480@60 and 1109 arriving from 320x256@70.
TEST_CASE("an unlocked pass leaves the duty a locked one measured")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.08f;
    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);

    // The processor loses the line the samples are counted against, while the
    // source itself has not moved.
    const uint16_t adrift = BenchDivider - 100;
    Wire.bank[0][0x17] = (uint8_t)(adrift & 0xFF);
    Wire.bank[0][0x18] = (uint8_t)((adrift >> 8) & 0x0F);
    Wire.sourceHsync(1200, BenchDivider, false);

    // Measured, not settling: the engine holds a duty a locked pass gave it, so
    // there is nothing to wait for. Waiting here is what deadlocks it -- the
    // recovery ladder then runs against the sync path the wait depends on.
    CHECK(measureOnce(sampling) == SourceMeasurement::Measured);
    CHECK(sampling.hsync().syncDuty()
          == doctest::Approx(181.0f / (float)BenchDivider));
}

// AND THE FLOOR IS ONLY FOR A SOURCE NOTHING HAS EVER MEASURED. Once a locked
// pass has answered, an unlocked one carries no information at all, so letting
// the floor through after it replaces a measurement with a guess -- which is
// what it did on the bench: 320x256@50 took its correct 7.07% on the first two
// passes and then a 9.96% sample 32 passes later, and the capturable region
// solved 1110 where 1145 was due.
TEST_CASE("a measured duty is never replaced by an unlocked one")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, false);
    g_fieldRate = 50.08f;
    REQUIRE(measurePastGate(sampling) == SourceMeasurement::Measured);

    const uint16_t adrift = BenchDivider - 100;
    Wire.bank[0][0x17] = (uint8_t)(adrift & 0xFF);
    Wire.bank[0][0x18] = (uint8_t)((adrift >> 8) & 0x0F);
    Wire.sourceHsync(1200, BenchDivider, false);

    for (uint16_t pass = 0; pass < 128; ++pass)
        measureOnce(sampling);

    CHECK(sampling.hsync().syncDuty()
          == doctest::Approx(181.0f / (float)BenchDivider));
}

// HPERIOD_IF IS A CHANGE DETECTOR AND NOTHING ELSE. It rails, and it reads
// values that are plainly wrong and perfectly steady -- 511 on a 311-line
// source at 50 Hz reads as 13183 Hz against a real 15625 and holds, which no
// run of samples can reject. So nothing here asks what the rate IS: a reading
// compared against ITS OWN earlier one answers whether the source moved whether
// or not either number was ever right, and the idle path asks nothing more.
// docs/investigations/hperiod-if-railing.md
TEST_CASE("the line period says whether the source moved, not what its rate is")
{
    SourceMeasurement measurement(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    seedHPeriod(431);

    const uint16_t reference = measurement.settledLinePeriod();
    REQUIRE(reference == 431);

    SUBCASE("a period that has not moved is not a move") {
        CHECK_FALSE(measurement.hasLineRateMoved(reference));
    }

    SUBCASE("a period that moved is a move") {
        seedHPeriod(214);     // what 31.5 kHz reads, against 15.6 kHz
        CHECK(measurement.hasLineRateMoved(reference));
    }

    SUBCASE("a railed reference still sees a move away from it") {
        // The register can rail while the source stands still, so the reference
        // taken after a solve may itself be a rail. Only the difference is
        // asked about, so the comparison still works from one.
        seedHPeriod(511);
        const uint16_t railed = measurement.settledLinePeriod();
        REQUIRE(railed == 511);
        seedHPeriod(431);
        CHECK(measurement.hasLineRateMoved(railed));
    }

    SUBCASE("a rail that stays railed is not a move") {
        seedHPeriod(511);
        CHECK_FALSE(measurement.hasLineRateMoved(
            measurement.settledLinePeriod()));
    }

    SUBCASE("a reading that will not hold still says nothing") {
        Wire.drift(0, 0x06);
        CHECK(measurement.settledLinePeriod() == 0);
        CHECK_FALSE(measurement.hasLineRateMoved(reference));
    }

    SUBCASE("nothing to compare against says nothing") {
        CHECK_FALSE(measurement.hasLineRateMoved(0));
    }
}

// The cheap gate is INSIDE the one call, so a caller that asks every pass does
// not pay the vsync spin until the count has settled.
TEST_CASE("a count still gathering samples costs no field rate measurement")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    g_fieldRateCalls = 0;

    CHECK(measureOnce(sampling) == SourceMeasurement::NotSteady);
    CHECK(g_fieldRateCalls == 0);
}

// The caller widens the coast on this, so it has to be distinguishable from a
// run that is merely still gathering.
TEST_CASE("a count that read the serrations is reported apart from an unsettled one")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(622);
    seedSourceHalfLines(622);
    seedInterlaced();

    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && reading != SourceMeasurement::Serrations; ++pass)
        reading = measureOnce(sampling);

    CHECK(reading == SourceMeasurement::Serrations);
}

// A rate is not worth sizing a raster from until it has repeated, and the
// caller treats that differently from a source it cannot read at all.
TEST_CASE("a rate that has not repeated yet is settling rather than measured")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    g_fieldRate = 50.08f;

    SourceMeasurement::MeasurementStatus first = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 16 && first == SourceMeasurement::NotSteady; ++pass)
        first = measureOnce(sampling);

    CHECK(first == SourceMeasurement::Settling);
}

// --- a transient caught as the count moves must not become the held rate -----

TEST_CASE("a transient rate caught as the count changes does not refuse the real one")
{
    // Measured on the bench, 320x256@50 -> 640x480@60. The first reading after
    // the count moved was `524 lines x 45.98 Hz -> line rate 24142`, a
    // mid-change transient -- and rateFollowsCount() accepts anything when the
    // count moved, because a count change IS a mode change. It then became the
    // rate every correct 60.36 Hz reading was measured against: 59 refusals over
    // 2.28 s, until HeldRateRejectionLimit drained.
    //
    // The held rate is what refuses a settling transient, so it must not be one.
    seedSourceLines(311);
    g_fieldRate = 50.08f;
    SourceMeasurement sampling(inputFormatter);
    REQUIRE(rateMeasured(measurePastGate(sampling)));
    const uint32_t settled = sampling.lineRateHz();
    REQUIRE(settled == 15624u);

    // The source changes mode. One pass sees the new count through a rate that
    // is still moving.
    sampling.modeChanged();
    seedSourceLines(524);
    g_fieldRate = 45.98f;
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        measureOnce(sampling);

    // The real rate arrives. It must be taken, not refused against a transient.
    g_fieldRate = 60.36f;
    SourceMeasurement::MeasurementStatus reading = SourceMeasurement::NotSteady;
    for (uint8_t pass = 0; pass < 4; ++pass)
        reading = measureOnce(sampling);

    CHECK(rateMeasured(reading));
    CHECK(sampling.lineRateHz() == 31689u);
}

// --- the sampling table ------------------------------------------------------

namespace {

// What the chip can measure of a source: the frame's line count and its field
// rate. The horizontal is not among them -- the part sees sync edges, not
// pixels -- so the divider is a function of those two and the scan mode alone.
// `htotal` is here only to say what the divider is WORTH, and is the mode's
// own, not something the board can know.
struct InputMode {
    const char *name;
    uint16_t frameLines;
    float fieldRateHz;
    uint16_t htotal;
};

const InputMode Modes[] = {
    {"240p 60 (NTSC rate)",     262, 59.94f,  454},
    {"288p 50 (PAL rate)",      312, 50.08f,  512},
    {"1056x256 50",             312, 50.08f, 1536},
    {"480i 60, one field",      263, 59.94f,  858},
    {"576i 50, one field",      313, 50.00f,  864},
    {"480p 60",                 525, 59.94f,  858},
    {"576p 50",                 625, 50.00f,  864},
    {"640x480 60",              525, 59.94f,  800},
    {"640x480 72",              520, 72.81f,  832},
    {"640x480 75",              500, 75.00f,  840},
    {"800x600 56",              625, 56.25f,  936},
    {"800x600 60",              628, 60.32f, 1056},
    {"800x600 72",              666, 72.19f, 1040},
    {"800x600 75",              625, 75.00f, 1056},
    {"1024x768 60",             806, 60.00f, 1344},
    {"1024x768 70",             806, 70.07f, 1328},
    {"1024x768 75",             800, 75.03f, 1312},
    {"1280x1024 60",           1066, 60.02f, 1688},
    {"1280x720 60",             750, 60.00f, 1650},
    {"1920x1080 60",           1125, 60.00f, 2200},
};

// What one oversampling ratio could give this line, and which ceiling held it
// there. The chooser picks between exactly these.
struct Option {
    uint16_t divider;
    const char *boundBy;
};

Option optionAt(uint32_t lineRateHz, uint8_t ratio, bool doubled)
{
    const uint32_t counter =
        (uint32_t)InputFormatter::LineCounterMax * (doubled ? 2u : 1u);
    uint32_t ceiling = Adc::maxCkoFor(ratio) / lineRateHz;
    const char *bound = "row";
    if (ceiling > Adc::DividerMax) { ceiling = Adc::DividerMax; bound = "field"; }

    // The backoff guards the ADC's rating and nothing else, so it lands before
    // the counter rather than on the answer. Kept in step with
    // SamplingClock::recommendedDivider().
    ceiling = (ceiling * SamplingClock::RecommendedPercent) / 100;
    if (ceiling > counter) { ceiling = counter; bound = "counter"; }

    Option option;
    option.divider = (uint16_t)(ceiling & ~1u);
    option.boundBy = bound;
    return option;
}

void dumpTable()
{
    // The scan mode depends on the OUTPUT as well: a doubled frame with no room
    // to be shown is only cropped. 1080p is what the bench runs.
    const uint16_t showable = OutputWindow::maximumCapture(AxisVertical, 1125, 0, 0);
    const uint8_t Ratios[] = {4, 2, 1};

    printf("output 1080p, showable %u, oversampling asked for 4\n", showable);
    printf("kept >= pixels carries the picture; converted >= 2 x pixels clears "
           "Nyquist\n\n");
    printf("%-22s %6s %7s %4s", "source", "pixels", "line Hz", "2x?");
    for (uint8_t ratio : Ratios)
        printf("  %2ux div bound", ratio);
    printf("   PLLAD_MD over    kept  /px  converted  /2px\n");

    for (const InputMode &mode : Modes) {
        const uint16_t lines = (uint16_t)(mode.frameLines - 1);
        const uint32_t lineRate = VideoSignal::lineRateFor(lines, mode.fieldRateHz);
        const bool doubled = InputFormatter::shouldDoubleLine(lines, showable);
        const uint16_t divider =
            SamplingClock::recommendedDivider(lineRate, 4, doubled);
        const uint8_t installed =
            Adc::oversampleFor(Adc::postDividerFor((uint32_t)divider * lineRate), 4);
        const uint16_t kept = InputFormatter::lineCounterFor(divider, doubled);
        const uint32_t converted = (uint32_t)divider * installed;

        printf("%-22s %6u %7lu %4s", mode.name, mode.htotal,
               (unsigned long)lineRate, doubled ? "yes" : "no");
        for (uint8_t ratio : Ratios) {
            const Option option = optionAt(lineRate, ratio, doubled);
            printf("  %c%4u %6s", ratio == installed ? '*' : ' ',
                   option.divider, option.boundBy);
        }
        printf("   %8u %3ux  %6u %4.2f%s  %9lu %5.2f%s\n", divider, installed,
               kept, (double)kept / mode.htotal, kept >= mode.htotal ? "*" : " ",
               (unsigned long)converted, converted / (2.0 * mode.htotal),
               converted >= 2u * mode.htotal ? "*" : " ");
    }
}

}  // namespace

int main(int argc, char **argv)
{
    // Before the test runner, which exits non-zero on an option it does not
    // know.
    if (argc > 1 && std::strcmp(argv[1], "--table") == 0) {
        dumpTable();
        return 0;
    }
    return doctest::Context(argc, argv).run();
}


// THE COUNTER RE-COUNTS THE LINE, SO THE CORRECTION IS NOT INSTANT. The
// polarity write reaches SP_HS_INV_REG at once and
// STATUS_SYNC_PROC_HLOW_LEN keeps reporting what it counted before -- measured
// on the bench at 800x600 as 1790/2038 = 0.878 followed, about 1.5 s later, by
// 196/1606 = 0.122 against the mode's 0.1212.
//
// 0.878 is the complement, not a measurement. Taken as the duty it is what
// SourceTiming::lookUp() matches on, and no published raster has a duty near
// unity -- so pass-through blanks from the sync envelope instead of the
// standard, and the border it shows changes between two landings on one source
// with every register self-consistent.
// docs/investigations/the-duty-is-the-complement-until-the-counter-recounts.md
TEST_CASE("the pulse is the shorter interval, whatever the counter reports")
{
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(181, BenchDivider, true);
    Wire.hsyncInversionLag(200);   // the correction never reaches the counter
    g_fieldRate = 50.08f;

    measurePastGate(sampling);

    CHECK(sampling.hsync().syncDuty()
          == doctest::Approx(181.0f / (float)BenchDivider));
}

// A REFUSED DUTY IS A FAULT, NOT A DEFAULT, so it has to say so. Nothing
// substitutes a value any more -- the guess suited the bench source to one unit
// and was wrong on every other mode, invisibly -- so the console is the only
// place a reader can see the engine waiting and why.
TEST_CASE("a duty that is not a pulse is announced")
{
    // Nothing substitutes a value any more, so the console is the only place a
    // reader can see the engine waiting and why. The count has to be one no
    // shorter interval can rescue: half the line either way.
    SourceMeasurement sampling(inputFormatter);
    Adc::applyDivider(BenchDivider);
    seedSourceLines(311);
    Wire.sourceHsync(BenchDivider / 2, BenchDivider, false);
    g_fieldRate = 50.08f;

    measurePastGate(sampling);

    CHECK(loggedContaining("NOT A PULSE"));
}

// THE SCAN DECISION HAS ITS OWN RUN, AND IT NEEDS ONE. The solve's steadiness
// run stops being fed the moment a source settles, which is exactly when a
// source that starts alternating has to be noticed -- and a source going
// interlaced does not move the count enough for anything to re-measure, because
// 627 and 628 are one measurement by SteadyRun::agree().
TEST_CASE("a source that starts alternating is noticed without a solve pass")
{
    seedSourceLines(627);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);
    REQUIRE(measurement.measureScanType() == SourceMeasurement::ScanProgressive);

    SourceMeasurement::ScanType scan = SourceMeasurement::ScanProgressive;
    for (uint8_t i = 0; i < 12 && scan == SourceMeasurement::ScanProgressive; ++i) {
        seedSourceLines(i % 2 ? 628 : 627);
        scan = measurement.measureScanType();
    }
    CHECK(scan == SourceMeasurement::ScanInterlaced);
}

// The boot fault at this layer. One reading off by one, seen by the scan run
// alone, is what a source does as it is acquired -- and the deinterlacer
// engages on two consecutive interlaced answers.
TEST_CASE("one count off by one is not enough to read as interlaced")
{
    seedSourceLines(627);
    SourceMeasurement measurement(inputFormatter);
    REQUIRE(measurePastGate(measurement) != SourceMeasurement::NotSteady);

    seedSourceLines(628);
    CHECK(measurement.measureScanType() == SourceMeasurement::ScanProgressive);

    for (uint8_t i = 0; i < 20; ++i) {
        CAPTURE(i);
        seedSourceLines(627);
        CHECK(measurement.measureScanType() == SourceMeasurement::ScanProgressive);
    }
}
