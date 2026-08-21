// Host-compiled unit tests for the registers Geometry::apply() writes --
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
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Memory.h"

using namespace Tv5725;

// The sketch defines this for real; here the test drives it, so the one input
// that cannot be held still on a board is a constant here.
static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }

// Chosen field by field rather than for looking unlikely. The binding
// constraint is VDS_VSCALE_BYPS, s3_00 bit 5, which the engine writes 0: under
// the neighbouring tests' 0xC2 that bit is ALREADY 0 and the byte is touched by
// VDS_HSCALE_BYPS regardless, so dropping the write would pass both a value and
// a touched check. 0xE2 sets bits 4 and 5, the two the engine clears.
static const uint8_t Poison = 0xE2;

// A field written straight into the fake's banks, bypassing the bus, so seeding
// an INPUT does not read as the code under test having written it.
// Read-modify-write because these fields share bytes -- VDS_HSYNC_RST and
// VDS_VSYNC_RST both live in s3_02.
static void seed(uint8_t seg, uint8_t reg, uint8_t offset, uint8_t width,
                 uint32_t value)
{
    uint8_t span = static_cast<uint8_t>((offset + width + 7) / 8);
    uint32_t mask = ((1u << width) - 1u) << offset;
    uint32_t raw = 0;
    for (uint8_t i = 0; i < span; ++i)
        raw |= static_cast<uint32_t>(Wire.bank[seg][static_cast<uint8_t>(reg + i)])
               << (8 * i);
    raw = (raw & ~mask) | ((value << offset) & mask);
    for (uint8_t i = 0; i < span; ++i)
        Wire.bank[seg][static_cast<uint8_t>(reg + i)] =
            static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
}

// The bench RiscPC at 320x256@50 into the engine's own 1915 x 1126 raster, the
// state of snapshots/CLEAN-engine-raster-1915-2026-08-15.json. Every value here
// is an INPUT: a raster the engine already solved, and three source
// measurements.
struct Bench {
    Geometry engine;

    Bench()
    {
        Wire.reset();
        Wire.poison(Poison);

        seed(3, 0x01, 0, 12, 1914);   // VDS_HSYNC_RST, output line - 1
        seed(3, 0x02, 4, 11, 1125);   // VDS_VSYNC_RST, output frame - 1
        seed(1, 0x0E, 0, 11, 1276);   // IF_HSYNC_RST, capture wrap - 1
        seed(0, 0x19, 0, 12, 181);    // STATUS_SYNC_PROC_HLOW_LEN, hsync low
        seed(5, 0x12, 0, 12, 2553);   // PLLAD_MD, the line in ADC samples
        seed(0, 0x1B, 0, 11, 311);    // STATUS_SYNC_PROC_VTOTAL, source lines

        // The divider is GIVEN to the engine, never read back mid-solve. Here
        // the bench inherits it the way a custom preset does -- adopt() reads
        // PLLAD_MD once, out loud -- which reproduces the seeds above.
        engine.adoptSampling();

        REQUIRE(engine.solveFromScratch());
    }
};

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
    // window: the vertical capture counts HALF-lines, so it wraps at twice the
    // source frame.
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
    // The bench line, from the seeds above: IF_HSYNC_RST 1276 wraps at 1277.
    CHECK(Wire.field(4, 0x37, 0, 10) == Memory::offsetFor(1277));
    CHECK(Wire.field(4, 0x37, 0, 10) >= Wire.field(4, 0x39, 0, 10));

    SUBCASE("and the zoom that widens the capture does not outgrow it") {
        // Rewriting the stride re-lays the buffer out under a picture being
        // read from it, so it may not move with the framing -- it has to be
        // right for every framing of this line from the start. Sizing it from
        // the widest capture would not do: that subtracts the measured hsync
        // pulse, which moves by a unit between solves.
        const uint32_t stride = Wire.field(4, 0x37, 0, 10);

        bench.engine.requestFraming(PanAndZoom(-5000, 0, 0, 0));
        REQUIRE(bench.engine.applyRequested());

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
    REQUIRE(bench.engine.solveFromScratch());

    CHECK(Wire.field(1, 0x18, 0, 11) == startBefore);
    CHECK(Wire.field(1, 0x1A, 0, 11) == stopBefore);

    SUBCASE("and adopting the new value is what makes it change") {
        // The other half of the claim: the window is not merely insensitive.
        // 181/1276 is a 14.2% hsync duty against 7.1%, so the capture starts
        // twice as far into the line -- a difference this assertion would have
        // caught either way round.
        bench.engine.adoptSampling();
        REQUIRE(bench.engine.solveFromScratch());
        CHECK(Wire.field(1, 0x18, 0, 11) != startBefore);
    }
}

TEST_CASE("solveSampling computes the divider and writes all three registers")
{
    // The step-4 shape: with no preset table there is nothing to adopt, so the
    // divider is COMPUTED from the line rate the source is running at. The
    // registers are outputs of that, never inputs to it.
    Bench bench;

    // 311 lines at 50 Hz, which is what the seeds above describe.
    REQUIRE(bench.engine.solveSampling(15550, 4));

    const uint16_t wanted = Sampling::recommendedDivider(15550, 4);
    CHECK(wanted != 2553);   // or this test proves nothing about computing it

    CHECK(Wire.field(5, 0x12, 0, 12) == wanted);
    CHECK(Wire.field(1, 0x0E, 0, 11) == Sampling::ifLineFor(wanted));
    CHECK(Wire.field(5, 0x4B, 0, 12) == Sampling::retimeStopFor(wanted));

    SUBCASE("and the solve that follows uses it") {
        // The seeded IF_HSYNC_RST was 1276 for a 2553 divider. If the engine
        // were still reading rasters back it would mix the new divider with the
        // old wrap; it takes both from the same held value.
        REQUIRE(bench.engine.solveFromScratch());
        CHECK(Wire.field(1, 0x0E, 0, 11) == Sampling::ifLineFor(wanted));
    }
}

TEST_CASE("an unmeasurable source never leaves the engine without a divider")
{
    // getSourceFieldRate() reports 0 with no lock, and a divider computed from a
    // measurement that did not happen is the green screen. With the tables gone
    // there is nothing to fall back on either, and an engine with no divider
    // defers every solve forever -- so a first refusal adopts what it finds.
    Bench bench;
    const uint32_t adopted = Wire.field(5, 0x12, 0, 12);

    CHECK_FALSE(bench.engine.solveSampling(0, 4));
    CHECK(Wire.field(1, 0x0E, 0, 11) == Sampling::ifLineFor((uint16_t)adopted));

    SUBCASE("and a later refusal keeps the divider it had already solved") {
        REQUIRE(bench.engine.solveSampling(15550, 4));
        const uint32_t solved = Wire.field(5, 0x12, 0, 12);
        CHECK_FALSE(bench.engine.solveSampling(0, 4));
        CHECK(Wire.field(5, 0x12, 0, 12) == solved);
    }
}
