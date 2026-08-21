#ifndef TEST_BENCH_GEOMETRY_H_
#define TEST_BENCH_GEOMETRY_H_

// A solved Geometry over the fake bus, shared by the suites that need one.
// Header-only and defining its globals: every host test is a single-translation
// -unit binary, so one include per binary is the whole contract.

#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"

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
    Tv5725::Geometry engine;

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

#endif  // TEST_BENCH_GEOMETRY_H_
