#ifndef TEST_SOLVED_ENGINE_H_
#define TEST_SOLVED_ENGINE_H_

// A solved Geometry over the fake bus, shared by the suites that need one.
// Header-only and defining its globals: every host test is a single-translation
// -unit binary, so one include per binary is the whole contract.

#include <doctest/doctest.h>

#include "Si5351Stubs.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"

// The sketch defines this for real; here the test drives it, so the one input
// that cannot be held still on a board is a constant here.
static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }
void tv5725Log(const char *) {}

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

// poll() gates on a line count steady over several passes before it will pay
// for a field rate measurement, so a solve takes more than one call.
static bool pollUntilSolved(Tv5725::Geometry &engine)
{
    for (uint8_t i = 0; i < 4 * Tv5725::SourceMeasurement::SteadySamples; ++i)
        if (engine.poll())
            return true;
    return false;
}

// The bench RiscPC at 320x256@50 into the engine's own 1916 x 1125 raster.
// The seeds are the source measurements the engine is allowed to read; the
// divider, the raster and both windows are computed from them, so what is
// seeded at PLLAD_MD and the raster registers is only what the previous load
// left behind.
struct SolvedEngine {
    Tv5725::DisplayClock clock;
    Tv5725::Geometry engine;

    SolvedEngine(uint16_t sourceLines = 311, float fieldRateHz = 50.08f,
                 uint16_t hsyncLow = 181,
                 const Tv5725::OutputMode *mode = &Tv5725::Mode1080p)
        : engine(clock)
    {
        Wire.reset();
        Wire.poison(Poison);
        g_fieldRate = fieldRateHz;

        seed(3, 0x01, 0, 12, 1915);          // VDS_HSYNC_RST, output line - 1
        seed(3, 0x02, 4, 11, 1124);          // VDS_VSYNC_RST, output frame - 1
        seed(1, 0x0E, 0, 11, 1276);          // IF_HSYNC_RST, capture wrap - 1
        seed(0, 0x19, 0, 12, hsyncLow);      // STATUS_SYNC_PROC_HLOW_LEN
        seed(5, 0x12, 0, 12, 2553);          // PLLAD_MD, the line in ADC samples
        seed(0, 0x1B, 0, 11, sourceLines);   // STATUS_SYNC_PROC_VTOTAL

        engine.modeChanged(mode, 4);
        REQUIRE(pollUntilSolved(engine));
    }

    ~SolvedEngine() { g_fieldRate = 50.08f; }
};

#endif  // TEST_SOLVED_ENGINE_H_
