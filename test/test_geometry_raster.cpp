// Host-compiled unit tests for Geometry::solveRaster() -- `make -C test geometry-raster`.
//
// getSourceFieldRate() is the sketch's, and this file supplies it instead, so
// the test DRIVES the source measurement -- the input the whole function turns
// on, and the one thing a bench test cannot hold still.
//
// What it pins is the difference between "refuse" and "defer". Getting that
// wrong does not look like a bug: the picture is fine, because the preset
// table's raster is still there. It just means the engine never ran.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputRaster.h"

using namespace Tv5725;

// The source field rate the engine will measure. The sketch defines this for
// real; here it is the test's to set, which is the point.
static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }

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

struct Bench {
    Bench()
    {
        Wire.reset();
        Wire.poison(Poison);
        g_fieldRate = 50.08f;
        setSourceLines(311);  // the bench RiscPC, settled: PAL-like
    }
};

TEST_CASE("a settled source gets the computed raster, not the table's")
{
    Bench bench;
    Geometry engine;

    // 1916 x 1125, where the bench ran 1915 x 1126 until 2026-08-14. The frame
    // lost the extra line every shipped table carried and the line gained a
    // pixel for it -- horizontalTotal is clock / (verticalTotal x rate), so the two move
    // opposite ways and the clock demand is unchanged.
    REQUIRE(engine.solveRaster(&Mode1080p));
    CHECK(horizontalTotalWritten() == 1916);
    CHECK_FALSE(engine.rasterPending());
}

TEST_CASE("an unsettled line count DEFERS the solve, it does not abandon it")
{
    Bench bench;
    Geometry engine;

    // 97 is the documented mid-preset-load reading -- see Capture's comment and
    // CLAUDE.md. It is exactly when solveRaster() is called, so this is the
    // common case rather than an edge one.
    setSourceLines(97);

    CHECK_FALSE(engine.solveRaster(&Mode1080p));

    // Refusing without deferring means nothing retries, so the preset table's
    // raster stands for the session while the picture looks perfect. Measured
    // 2026-08-15: stuck at the table's 1445 x 1126 where the engine wanted 1915,
    // with FrameSync steering the Si5351 to 81.48 MHz to match the wrong one.
    CHECK(engine.rasterPending());
}

TEST_CASE("nothing is written while the source is unsettled")
{
    Bench bench;
    Geometry engine;
    setSourceLines(97);

    engine.solveRaster(&Mode1080p);

    // A half-written raster is worse than none: the totals go in before the
    // sync positions, so bailing between them would leave the two disagreeing.
    CHECK(Wire.field(3, 0x01, 0, 12) == ((Poison | (Poison << 8)) & 0x0FFF));
}

TEST_CASE("the deferred solve lands as soon as the source settles")
{
    Bench bench;
    Geometry engine;
    setSourceLines(97);
    REQUIRE_FALSE(engine.solveRaster(&Mode1080p));
    REQUIRE(engine.rasterPending());

    setSourceLines(311);

    // The no-argument retry, against the mode the first call named. The caller
    // does not have to have kept hold of it.
    CHECK(engine.solveRaster());
    CHECK(horizontalTotalWritten() == 1916);
    CHECK_FALSE(engine.rasterPending());
}

TEST_CASE("a field rate disagreeing with the line count defers too")
{
    Bench bench;
    Geometry engine;

    // 311 lines says PAL, so ~50 Hz; a 60 Hz reading is the transient this
    // guard exists for. Deferring was already right here -- pinned so it stays.
    g_fieldRate = 60.0f;

    CHECK_FALSE(engine.solveRaster(&Mode1080p));
    CHECK(engine.rasterPending());
}

TEST_CASE("a mode with no timings is refused for good, not retried forever")
{
    Bench bench;
    Geometry engine;

    // The four output resolutions with no OutputMode yet, and a custom preset,
    // arrive here as NULL. Nothing about waiting will produce timings, so
    // deferring would spin getSourceFieldRate() -- which samples vsync and is
    // not cheap -- on every pass of loop() for the whole session.
    CHECK_FALSE(engine.solveRaster(0));
    CHECK_FALSE(engine.rasterPending());
}

TEST_CASE("the raster the engine computes beats every table's")
{
    Bench bench;
    Geometry engine;
    REQUIRE(engine.solveRaster(&Mode1080p));

    // The twelve tables ship 1445 (PAL) and 1602 (NTSC) at this frame height.
    // Landing on either would mean the table won, which is the whole failure
    // this file exists to catch.
    CHECK(horizontalTotalWritten() != 1445);
    CHECK(horizontalTotalWritten() != 1602);
}

TEST_CASE("entering bypass drops a deferred solve")
{
    Bench bench;
    Geometry engine;

    // A deferred solve from the previous mode, which is the COMMON state now
    // that an unsettled source defers rather than abandoning.
    setSourceLines(97);
    REQUIRE_FALSE(engine.solveRaster(&Mode1080p));
    REQUIRE(engine.rasterPending());

    // Past 535 lines the unit drops to RGBHV bypass, where video routes around
    // the VDS and there is no raster to solve. bypassModeSwitch_RGBHV() returns
    // before doPostPresetLoadSteps() so nothing else clears the flag, and the
    // retry is not gated on bypass -- a stale pending writes a scaled raster
    // straight over the bypass setup.
    engine.enterBypass();

    CHECK_FALSE(engine.rasterPending());
}

TEST_CASE("a forgotten raster is not resurrected by the retry")
{
    Bench bench;
    Geometry engine;
    setSourceLines(97);
    REQUIRE_FALSE(engine.solveRaster(&Mode1080p));
    engine.enterBypass();

    // Now settled -- but there is still no mode, so the retry must write
    // nothing. Asserting on the register rather than the return value: the
    // damage a resurrected solve does is the write, not the boolean.
    setSourceLines(311);
    CHECK_FALSE(engine.solveRaster());
    CHECK(Wire.field(3, 0x01, 0, 12) == ((Poison | (Poison << 8)) & 0x0FFF));
}

TEST_CASE("the vertical size registers move with the raster the engine solved")
{
    Bench bench;
    Geometry engine;

    // VDS_VSYN_SIZE1/2 are the vertical totals the frame-rate selector picks
    // between, both holding the same total here, and ten of the twelve tables
    // ship VDS_VSYNC_RST + 2. Written from doPostPresetLoadSteps() instead, they
    // are left behind by a deferred solve: the retry re-solves the raster, the
    // clock and the windows without re-entering that function. One quantity in
    // three registers belongs to one owner.
    REQUIRE(engine.solveRaster(&Mode1080p));

    uint32_t verticalTotal = Wire.field(3, 0x02, 4, 11) + 1;
    CHECK(Wire.field(3, 0x20, 0, 11) == verticalTotal + 1);
    CHECK(Wire.field(3, 0x22, 0, 11) == verticalTotal + 1);
}

TEST_CASE("a deferred solve carries the vertical size registers with it")
{
    Bench bench;
    Geometry engine;

    // The path that was broken: refuse, then retry, with nothing running
    // doPostPresetLoadSteps() in between.
    setSourceLines(97);
    REQUIRE_FALSE(engine.solveRaster(&Mode1080p));
    setSourceLines(311);
    REQUIRE(engine.solveRaster());

    uint32_t verticalTotal = Wire.field(3, 0x02, 4, 11) + 1;
    CHECK(Wire.field(3, 0x20, 0, 11) == verticalTotal + 1);
    CHECK(Wire.field(3, 0x22, 0, 11) == verticalTotal + 1);
}

TEST_CASE("an unmeasurable line rate DEFERS the sampling solve, it does not settle for the register")
{
    Bench bench;
    Geometry engine;

    // A cold boot reads `271 lines x 49.22 Hz -> line rate 0` 3.6 s in, so
    // lineRateFrom() refuses and the engine adopts what is on the chip: 1856,
    // bypassModeSwitch_RGBHV()'s literal, 27% below the 2548 the source wants.
    // Adopting is right -- the engine needs some divider -- so what matters is
    // that the fallback is REMEMBERED and retried.
    Wire.bank[5][0x12] = 0x40;  // PLLAD_MD = 1856, as the bypass path leaves it
    Wire.bank[5][0x13] = 0x07;

    CHECK_FALSE(engine.solveSampling(0, 4));
    CHECK(engine.samplingPending());
}

TEST_CASE("a measurable line rate leaves nothing pending")
{
    Bench bench;
    Geometry engine;

    // 311 lines at 50.08 Hz is 15574 Hz, and 98% of the 162 MSPS ceiling over
    // four-times oversampling is 2548 -- the divider this bench has run on all
    // along.
    CHECK(engine.solveSampling(15574, 4));
    CHECK_FALSE(engine.samplingPending());
    CHECK(Wire.field(5, 0x12, 0, 12) == 2548);
}

TEST_CASE("the deferred sampling solve lands once the source can be measured")
{
    Bench bench;
    Geometry engine;

    REQUIRE_FALSE(engine.solveSampling(0, 4));
    REQUIRE(engine.samplingPending());

    CHECK(engine.solveSampling(15574, 4));
    CHECK_FALSE(engine.samplingPending());
    CHECK(Wire.field(5, 0x12, 0, 12) == 2548);
    CHECK(Wire.field(1, 0x0E, 0, 11) == 1274);   // IF_HSYNC_RST, divider / 2
    CHECK(Wire.field(5, 0x4B, 0, 12) == 2369);   // SP_RT_HS_SP, 93% of it
}

TEST_CASE("entering bypass drops a deferred sampling solve too")
{
    Bench bench;
    Geometry engine;

    // Same reason enterBypass() drops the deferred raster: neither bypass
    // switch reaches doPostPresetLoadSteps(), so nothing else would clear it,
    // and a retry firing afterwards would move the divider out from under a
    // bypass setup that chose its own.
    REQUIRE_FALSE(engine.solveSampling(0, 4));
    REQUIRE(engine.samplingPending());

    engine.enterBypass();

    CHECK_FALSE(engine.samplingPending());
}
