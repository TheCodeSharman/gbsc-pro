// Host-compiled unit tests for the registers one solve writes --
// `make -C test geometry-windows`.
//
// The net under doPostPresetLoadSteps()'s geometry writes: what makes them
// deletable is that the engine writes each of those fields afterwards
// regardless.
//
// **THE BENCH CANNOT SETTLE THAT.** The writes sit in eight groups behind
// videoStandardInput 1, 2, 3, 4, 8 or 9, presetIsPalForce60 or
// VPERIOD_IF == 523, and one source reaches exactly one group. So the question
// is asked here instead: poison every bank, seed only what the engine reads, run
// one solve, and ask the fake which registers it wrote.
// docs/chip-initialisation.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "BenchGeometry.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Memory.h"

using namespace Tv5725;

// Every field doPostPresetLoadSteps() writes into that the engine also owns,
// with the bytes each one spans. Named so a failure says which.
struct Owned {
    const char *name;
    uint8_t seg, reg, offset, width, bytes;
};

static const Owned OwnedFields[] = {
    {"IF_HB_ST2",       1, 0x18, 0, 11, 2},
    {"IF_HB_SP2",       1, 0x1A, 0, 11, 2},
    {"IF_VB_ST",        1, 0x1C, 0, 11, 2},
    {"IF_VB_SP",        1, 0x1E, 0, 11, 2},
    {"VDS_VSCALE_BYPS", 3, 0x00, 5,  1, 1},
    {"VDS_DIS_VB_ST",   3, 0x13, 0, 11, 2},
    {"VDS_DIS_VB_SP",   3, 0x14, 4, 11, 2},
    {"VDS_VSCALE",      3, 0x17, 4, 10, 2},
    {"PB_CAP_OFFSET",   4, 0x37, 0, 10, 2},
};

static const size_t OwnedCount = sizeof(OwnedFields) / sizeof(OwnedFields[0]);

TEST_CASE("one solve writes every geometry register the sketch used to poke")
{
    Bench bench;

    // Asked of the fake rather than inferred from the value: a field whose
    // computed value happened to equal the poison would read correct having
    // never been written at all.
    for (size_t i = 0; i < OwnedCount; ++i) {
        const Owned &f = OwnedFields[i];
        CAPTURE(f.name);
        for (uint8_t b = 0; b < f.bytes; ++b) {
            CAPTURE(b);
            CHECK(Wire.touched[f.seg][static_cast<uint8_t>(f.reg + b)]);
        }
    }
}

TEST_CASE("no field is left holding what was there before the solve")
{
    Bench bench;

    // The half `touched` cannot see: five of these nine share a byte with
    // another field the engine writes, so the byte is touched whether or not the
    // field itself was and only the value distinguishes.
    for (size_t i = 0; i < OwnedCount; ++i) {
        const Owned &f = OwnedFields[i];
        CAPTURE(f.name);
        uint32_t stale = (static_cast<uint32_t>(Poison) * 0x0101u >> f.offset)
                         & ((1u << f.width) - 1u);
        CHECK(Wire.field(f.seg, f.reg, f.offset, f.width) != stale);
    }
}

TEST_CASE("vertical scaling is switched ON, whatever a preset load asked for")
{
    Bench bench;

    // doPostPresetLoadSteps() sets VDS_VSCALE_BYPS to 1 for a >650-line source
    // on videoStandardInput 9, and the engine clears it unconditionally because
    // it has computed an explicit scale -- so the two disagree and the engine
    // wins on ordering. Deleting the sketch's write changes nothing.
    CHECK(Wire.field(3, 0x00, 5, 1) == 0);
    CHECK(Wire.field(3, 0x00, 4, 1) == 0);  // VDS_HSCALE_BYPS, the same way
}

TEST_CASE("the capture window is a window, not a leftover")
{
    Bench bench;

    // Start beyond stop is the shape a half-written window takes, and it is what
    // the sketch's own arithmetic produced when it ran late: IF_VB_SP 8 with
    // IF_VB_ST 6, seen at gbs-control.ino's needPostAdjust. Reading each pair as
    // an ordered window is the cheapest assertion that catches it.
    CHECK(Wire.field(1, 0x1A, 0, 11) < Wire.field(1, 0x18, 0, 11));  // horizontalStop < horizontalStart
    CHECK(Wire.field(1, 0x1E, 0, 11) < Wire.field(1, 0x1C, 0, 11));  // verticalStop < verticalStart
    CHECK(Wire.field(3, 0x14, 4, 11) < Wire.field(3, 0x13, 0, 11));  // display V

    // And inside the raster it is placed on, which is the other half of being a
    // window. The doubler is in the path on this source, so the vertical capture
    // counts half-lines and wraps at twice the source frame.
    CHECK(Wire.field(1, 0x18, 0, 11) <= 1277);
    CHECK(Wire.field(1, 0x1C, 0, 11) <= 2 * 312u);
    CHECK(Wire.field(3, 0x13, 0, 11) <= 1126);
}

TEST_CASE("the playback stride covers the widest fetch, and holds still while zooming")
{
    Bench bench;

    // The stride is the per-line allocation and the fetch is what playback
    // reads into it, so a stride below the fetch overlaps lines. The fetch
    // follows the capture, which grows as the picture zooms OUT -- so a stride
    // sized for the framing on screen is short of the one the next press wants,
    // and it arrives as a green band down the right of the picture.
    // The bench line, COMPUTED rather than inherited: the divider caps at 2250
    // ADC samples so the whole line arrives, giving IF_HSYNC_RST 1125 and a
    // wrap at 1126. The 1276 seeded in the fixture is what the previous load
    // left behind, and the engine writes over it.
    CHECK(Wire.field(4, 0x37, 0, 10) == Memory::offsetFor(1126));
    CHECK(Wire.field(4, 0x37, 0, 10) >= Wire.field(4, 0x39, 0, 10));

    SUBCASE("and the zoom that widens the capture does not outgrow it") {
        // Rewriting the stride re-lays the buffer out under a picture being
        // read from it, so it may not move with the framing -- it has to be
        // right for every framing of this line from the start. Sizing it from
        // the widest capture would not do: that subtracts the measured hsync
        // pulse, which moves by a unit between solves.
        const uint32_t stride = Wire.field(4, 0x37, 0, 10);

        bench.engine.zoom(-5000, 0);

        CHECK(Wire.field(4, 0x37, 0, 10) == stride);
        CHECK(Wire.field(4, 0x37, 0, 10) >= Wire.field(4, 0x39, 0, 10));
    }
}

TEST_CASE("the engine uses the divider it was GIVEN, not the one in the register")
{
    // The window between a write and PLLAD_LAT, reproduced. The register reports
    // a divider the ADC is not clocking at, so an engine that reads it back
    // solves the capture window for a line that is not arriving -- PLLAD_MD
    // reading 2210 against a PLL running 2553 is a solid green screen with every
    // register self-consistent.
    Bench bench;

    const uint32_t startBefore = Wire.field(1, 0x18, 0, 11);   // IF_HB_ST2
    const uint32_t stopBefore  = Wire.field(1, 0x1A, 0, 11);   // IF_HB_SP2

    // Straight into the bank, so this is the chip changing under the engine
    // rather than the engine being told anything.
    seed(5, 0x12, 0, 12, 1276);
    seed(1, 0x0E, 0, 11, 638);

    REQUIRE(bench.engine.resolve());

    // Not vacuous: test_geometry.cpp's checkBenchGeometry() pins both of these
    // to the values measured on the unit, so "unchanged" is anchored to a
    // number rather than to whatever the engine happened to leave.
    CHECK(Wire.field(1, 0x18, 0, 11) == startBefore);
    CHECK(Wire.field(1, 0x1A, 0, 11) == stopBefore);

}

TEST_CASE("a preset load computes the divider it uses")
{
    // There is nothing to inherit, so the divider is COMPUTED from the line
    // rate the source is running at. The registers are outputs of that, never
    // inputs to it.
    Bench bench;

    // 311 lines at 50 Hz, which is what the seeds above describe.
    g_fieldRate = 50.08f;
    bench.engine.modeChanged(&Tv5725::Mode1080p, 4);
    REQUIRE(pollUntilSolved(bench.engine));

    const uint16_t wanted = SourceMeasurement::recommendedDivider(15550, 4, true);
    CHECK(wanted != 2553);   // or this test proves nothing about computing it

    CHECK(Wire.field(5, 0x12, 0, 12) == wanted);
    CHECK(Wire.field(1, 0x0E, 0, 11) == SourceMeasurement::ifLineFor(wanted, true));
    CHECK(Wire.field(5, 0x4B, 0, 12) == SourceMeasurement::retimeStopFor(wanted));

    SUBCASE("and the solve that follows uses it") {
        // The seeded IF_HSYNC_RST was 1276 for a 2553 divider. If the engine
        // were still reading rasters back it would mix the new divider with the
        // old wrap; it takes both from the same held value.
        REQUIRE(bench.engine.resolve());
        CHECK(Wire.field(1, 0x0E, 0, 11) == SourceMeasurement::ifLineFor(wanted, true));
    }
}

TEST_CASE("an unmeasurable source never leaves the engine without a divider")
{
    // getSourceFieldRate() reports 0 with no lock, and a divider computed from a
    // measurement that did not happen is the green screen. With the tables gone
    // there is nothing to fall back on either, and an engine with no divider
    // defers every solve forever -- so a first refusal adopts what it finds.
    Bench bench;
    const uint32_t inherited = Wire.field(5, 0x12, 0, 12);

    g_fieldRate = 0.0f;
    bench.engine.modeChanged(&Tv5725::Mode1080p, 4);
    CHECK_FALSE(pollUntilSolved(bench.engine));
    CHECK(Wire.field(1, 0x0E, 0, 11) == SourceMeasurement::ifLineFor((uint16_t)inherited, true));

    SUBCASE("and a later refusal keeps the divider it had already solved") {
        g_fieldRate = 50.08f;
        bench.engine.modeChanged(&Tv5725::Mode1080p, 4);
        REQUIRE(pollUntilSolved(bench.engine));
        const uint32_t solved = Wire.field(5, 0x12, 0, 12);

        g_fieldRate = 0.0f;
        bench.engine.modeChanged(&Tv5725::Mode1080p, 4);
        CHECK_FALSE(pollUntilSolved(bench.engine));
        CHECK(Wire.field(5, 0x12, 0, 12) == solved);
    }
}

// A source measurement that has not settled is not a mode, and the vertical
// axis is the one that can be fooled by it: the horizontal line comes from the
// held divider, while the vertical is entirely 2 x (STATUS_SYNC_PROC_VTOTAL + 1).
//
// Measured on the bench, sampled through a preset load: VTOTAL passes through
// 506, 251, 269, 259 and 511 before settling. Every one of those is inside the
// 200..1300 bounds the solve accepts, so a solve landing on one writes a
// vertical window sized for a frame the source is not sending -- and, having
// succeeded, never revisits it.
//
// What still refuses one is the 200..1300 bound and SourceMeasurement's
// steadiness run. **A transient count inside the bounds that holds still long
// enough is no longer caught**, because telling it from a real source at those
// timings needs an assumed field rate, and that assumption is what refused
// 640x480@75 outright. docs/firmware-geometry-engine.md
TEST_CASE("a vertical total outside what any source runs defers the solve")
{
    Wire.reset();
    Wire.poison(Poison);

    seed(3, 0x01, 0, 12, 1914);   // VDS_HSYNC_RST
    seed(3, 0x02, 4, 11, 1125);   // VDS_VSYNC_RST
    seed(1, 0x0E, 0, 11, 1276);   // IF_HSYNC_RST
    seed(0, 0x19, 0, 12, 181);    // STATUS_SYNC_PROC_HLOW_LEN
    seed(5, 0x12, 0, 12, 2553);   // PLLAD_MD

    // The 97 a preset load leaves behind, below SourceVerticalTotalMin.
    seed(0, 0x1B, 0, 11, 97);     // STATUS_SYNC_PROC_VTOTAL, mid-load
    g_fieldRate = 50.08f;

    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(&Tv5725::Mode1080p, 4);
    CHECK_FALSE(pollUntilSolved(engine));
    CHECK_FALSE(Wire.touched[1][0x1C]);   // IF_VB_ST
    CHECK_FALSE(Wire.touched[1][0x1E]);   // IF_VB_SP

    g_fieldRate = 50.08f;
}

// One press step is one pixel of the output screen, so the engine sizes a press
// from the scale its own solve produced. Reading VDS_?SCALE back to size it
// instead makes a register an input to the calculation.
TEST_CASE("a press is asked for in output pixels, not input units")
{
    Bench bench;

    // As an outside observer reads it, from the register the solve wrote.
    const float magnification = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
    REQUIRE(magnification > 1.5f);   // or pixels and units are indistinguishable

    const int16_t wanted = AxisHorizontal.stepUnits(16, magnification);
    REQUIRE(wanted != 16);

    SUBCASE("zooming") {
        REQUIRE(bench.engine.zoom(16, 0));
        CHECK(bench.engine.framing().horizontalZoom() == wanted);
    }

    SUBCASE("and panning, once a zoom has left room to pan into") {
        REQUIRE(bench.engine.zoom(400, 0));

        // Sized from the scale the zoom LEFT, not the one it started from: a
        // press is converted at the magnification in force when it is made.
        const float zoomed = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
        const int16_t step = AxisHorizontal.stepUnits(16, zoomed);
        const int16_t before = bench.engine.framing().horizontalPan();
        REQUIRE(bench.engine.pan(16, 0));
        CHECK(bench.engine.framing().horizontalPan() - before == step);
    }
}

TEST_CASE("a press on one axis leaves the other where it was")
{
    // stepUnits() floors at one granule, so an axis the press did not name
    // drifts a unit per press unless a press of nothing is skipped outright.
    Bench bench;
    const int16_t verticalZoom = bench.engine.framing().verticalZoom();

    REQUIRE(bench.engine.zoom(16, 0));
    CHECK(bench.engine.framing().verticalZoom() == verticalZoom);
}

// The output raster is one the engine SOLVED, so it is held rather than read
// back off VDS_?SYNC_RST. Reading it back makes a register an input to the
// calculation that produced it.
TEST_CASE("the solve uses the raster the engine holds, not the one on the chip")
{
    Bench bench;

    // Wiped after the solve that wrote them. A re-solve that reaches for these
    // sees no raster at all, reads it as bypass, and declines.
    const uint32_t stopBefore = Wire.field(3, 0x14, 4, 11);   // VDS_DIS_VB_SP
    seed(3, 0x01, 0, 12, 0);   // VDS_HSYNC_RST
    seed(3, 0x02, 4, 11, 0);   // VDS_VSYNC_RST

    // A framing the engine has not solved before, so a solve that ran shows as
    // a moved window and one that declined shows as no change at all. Big
    // enough to move VDS_DIS_VB_SP, which sits on its floor.
    bench.engine.zoom(0, 400);

    CHECK(Wire.field(3, 0x14, 4, 11) != stopBefore);
}
