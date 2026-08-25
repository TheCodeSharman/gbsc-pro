// Host-compiled tests for what the engine does while the source is unsettled --
// `make -C test geometry-raster`.
//
// getSourceFieldRate() is the sketch's, and this file supplies it instead, so
// the test DRIVES the source measurement -- the input the whole solve turns on,
// and the one thing a bench test cannot hold still.
//
// What it pins is the difference between waiting and giving up. Getting that
// wrong does not look like a bug: the picture is fine, because the preset
// table's raster is still there. It just means the engine never ran.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Si5351Stubs.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/clock/ClockGen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"

using namespace Tv5725;

// The source field rate the engine will measure. The sketch defines this for
// real; here it is the test's to set, which is the point.
static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }
void tv5725Log(const char *) {}

// STATUS_SYNC_PROC_VTOTAL, s0_1B[10:0] -- the source's line count.
static void setSourceLines(uint16_t lines)
{
    Wire.bank[0][0x1B] = lines & 0xFF;
    Wire.bank[0][0x1C] = (Wire.bank[0][0x1C] & 0xF8) | ((lines >> 8) & 0x07);
}

// VDS_HSYNC_RST, s3_01[11:0] -- the horizontal total the engine writes.
static uint16_t horizontalTotalWritten() { return Wire.field(3, 0x01, 0, 12) + 1; }

// Neither a preset table's value nor anything the engine writes.
static const uint8_t Poison = 0xC2;

static uint32_t horizontalTotalUnwritten()
{
    return (Poison | (Poison << 8)) & 0x0FFF;
}

static unsigned registersWritten()
{
    unsigned written = 0;
    for (uint8_t seg = 0; seg < FakeTwoWire::Segments; ++seg)
        for (int reg = 0; reg < 256; ++reg)
            if (Wire.touched[seg][reg])
                ++written;
    return written;
}

// poll() gates on a line count steady over several passes before it will pay for
// a field rate measurement, so a solve takes more than one call.
static bool pollUntilSolved(Geometry &engine)
{
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        if (engine.poll())
            return true;
    return false;
}

struct SettledEngine {
    DisplayClock clock;
    Geometry engine;

    SettledEngine() : engine(clock)
    {
        Wire.reset();
        Wire.poison(Poison);
        g_fieldRate = 50.08f;
        setSourceLines(311);  // the bench RiscPC, settled: PAL-like
    }
};

TEST_CASE("a settled source gets the computed raster, not the table's")
{
    SettledEngine settled;

    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE(pollUntilSolved(settled.engine));

    CHECK(horizontalTotalWritten() == 1916);

    // The twelve tables ship 1445 (PAL) and 1602 (NTSC) at this frame height.
    // Landing on either would mean the table won, which is the whole failure
    // this file exists to catch.
    CHECK(horizontalTotalWritten() != 1445);
    CHECK(horizontalTotalWritten() != 1602);
}

TEST_CASE("an unsettled line count is waited out, not solved against")
{
    SettledEngine settled;

    // 97 is the documented mid-preset-load reading -- see CaptureWindow's
    // comment and CLAUDE.md -- and it is perfectly steady, so steadiness alone
    // would call it settled.
    setSourceLines(97);
    settled.engine.modeChanged(&Mode1080p, 4);
    CHECK_FALSE(pollUntilSolved(settled.engine));

    // A half-written raster is worse than none: the totals go in before the
    // sync positions, so bailing between them would leave the two disagreeing.
    CHECK(Wire.field(3, 0x01, 0, 12) == horizontalTotalUnwritten());

    SUBCASE("and the poll after it settles lands the whole raster") {
        // Giving up instead leaves the previous raster standing for the
        // session, invisibly -- the picture is fine and FrameSync steers the
        // Si5351 to whatever raster it finds. Measured 2026-08-15: stuck at the
        // table's 1445 x 1126 where the engine wants its own, with the Si5351
        // on 81.48 MHz to match the wrong one.
        setSourceLines(311);
        REQUIRE(pollUntilSolved(settled.engine));
        CHECK(horizontalTotalWritten() == 1916);

        // VDS_VSYN_SIZE1/2 are the vertical totals the frame-rate selector picks
        // between, and VDS_FR_SELECT never alternates, so both are the frame.
        // One quantity in three registers has one owner, so they arrive with the
        // raster rather than from whatever ran at load time.
        uint32_t verticalTotal = Wire.field(3, 0x02, 4, 11) + 1;
        CHECK(Wire.field(3, 0x20, 0, 11) == verticalTotal + 1);
        CHECK(Wire.field(3, 0x22, 0, 11) == verticalTotal + 1);
    }
}

TEST_CASE("a field rate that moves without the line count is waited out too")
{
    SettledEngine settled;

    g_fieldRate = 50.08f;
    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE(pollUntilSolved(settled.engine));
    REQUIRE(horizontalTotalWritten() == 1916);

    // The same 311-line source now reading 60 Hz is the transient this guard
    // exists for. A raster solved at the wrong rate is out by the ratio of the
    // rates, so the previous answer has to stand.
    g_fieldRate = 60.0f;
    settled.engine.modeChanged(&Mode1080p, 4);
    CHECK_FALSE(pollUntilSolved(settled.engine));
    CHECK(horizontalTotalWritten() == 1916);

    SUBCASE("and the poll after the rate returns lands it") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(settled.engine));
        CHECK(horizontalTotalWritten() == 1916);
    }
}

TEST_CASE("entering bypass drops the outstanding solve")
{
    SettledEngine settled;

    // Outstanding from the previous mode, which is the common state now that an
    // unsettled source waits rather than giving up.
    setSourceLines(97);
    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE_FALSE(pollUntilSolved(settled.engine));

    // Past 535 lines the unit drops to RGBHV bypass, where video routes around
    // the VDS and there is no raster to solve. bypassModeSwitch_RGBHV() returns
    // before doPostPresetLoadSteps(), so nothing else clears the mode change,
    // and a solve landing afterwards writes a scaled raster and a recomputed
    // divider straight over the bypass setup.
    settled.engine.enterBypass();
    Wire.reset();
    Wire.poison(Poison);
    setSourceLines(311);

    CHECK_FALSE(pollUntilSolved(settled.engine));
    CHECK(registersWritten() == 0);
}

TEST_CASE("an unmeasurable line rate is retried, not settled for")
{
    SettledEngine settled;

    // A cold boot reads `271 lines x 49.22 Hz -> line rate 0` 3.6 s in, so
    // lineRateFrom() refuses. What the chip is left holding is the reference
    // the measurement was taken through, not what happened to be there: 1856 is
    // bypassModeSwitch_RGBHV()'s literal and 27% below the 2548 this source
    // wants, and a pass that could not measure has no reason to keep it.
    Wire.bank[5][0x12] = 0x40;  // PLLAD_MD = 1856, as the bypass path leaves it
    Wire.bank[5][0x13] = 0x07;
    g_fieldRate = 0.0f;

    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE_FALSE(pollUntilSolved(settled.engine));
    CHECK(Wire.field(5, 0x12, 0, 12) ==
          Tv5725::SourceMeasurement::referenceDivider(true));

    SUBCASE("and the poll that can measure it computes one") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(settled.engine));

        // 311 lines at 50.08 Hz is 15574 Hz. The ADC rating leaves room for 2548
        // there, and the capture write limit takes it to 2250.
        CHECK(Wire.field(5, 0x12, 0, 12) == 2250);
        CHECK(Wire.field(1, 0x0E, 0, 11) == 1125);   // IF_HSYNC_RST, divider / 2
        CHECK(Wire.field(5, 0x4B, 0, 12) == 2092);   // SP_RT_HS_SP, 93% of it
    }
}

TEST_CASE("a solve points the part at the clock source that can serve the raster")
{
    // s0_41 is a source selection. The seed names a target FREQUENCY; writing it
    // to the register puts the display on the internal PLL, which frame time
    // lock cannot steer, so the output drifts against the source while every
    // register reads correct. docs/tv5725-chip.md
    SettledEngine settled;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    settled.clock.driveWith(generator);

    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE(pollUntilSolved(settled.engine));

    CHECK(Wire.bank[0][0x41] == DisplayClock::ExternalPclkIn);

    SUBCASE("and the seed it holds is still the frequency to steer to") {
        CHECK(settled.clock.hz() == OutputMode::EngineCeilingHz);
    }
}

TEST_CASE("a board with no generator gets the seed's own internal divider")
{
    SettledEngine settled;

    settled.engine.modeChanged(&Mode1080p, 4);
    REQUIRE(pollUntilSolved(settled.engine));

    CHECK(Wire.bank[0][0x41] == 0x85);
}
