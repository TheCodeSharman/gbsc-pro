#ifndef TEST_SOLVED_ENGINE_H_
#define TEST_SOLVED_ENGINE_H_

// A solved VideoPath over the fake bus, shared by the suites that need one.
// Header-only and defining its globals: every host test is a single-translation
// -unit binary, so one include per binary is the whole contract.

#include <doctest/doctest.h>

#include "LoggedLines.h"
#include "Si5351Stubs.h"
#include "DebugPinStub.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoPath.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"

// The sketch defines this for real; here the test drives it, so the one input
// that cannot be held still on a board is a constant here.
static float g_fieldRate = 50.08f;

// Counted because the cost is the point: this samples vsync edges through
// FrameSync, up to 250 ms a pulse, which is why the solve has a cheap gate in
// front of it and why a quiet source must not reach it at all.
static unsigned g_fieldRateCalls = 0;

uint32_t debugPinPulseTicks()
{
    ++g_fieldRateCalls;
    return ticksForHz(g_fieldRate);
}


// Chosen field by field rather than for looking unlikely. The binding
// constraint is VDS_VSCALE_BYPS, s3_00 bit 5, which the engine writes 0: under
// the neighbouring tests' 0xC2 that bit is ALREADY 0 and the byte is touched by
// VDS_HSCALE_BYPS regardless, so dropping the write would pass both a value and
// a touched check. 0xE2 sets bits 4 and 5, the two the engine clears.
static const uint8_t Poison = 0xE2;

// Poisoned, HPERIOD_IF reads a value that implies a plausible line rate, and
// measureLineRate() prefers it to the injected field rate. A case that wants the
// rate it injects has to say nothing was measured.
static void poisonChip()
{
    Wire.poison(Poison);
    Wire.bank[0][0x06] = 0;                       // HPERIOD_IF low eight
    Wire.bank[0][0x07] &= 0xFE;                   // and its ninth bit
}

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

// One pass of the whole acquisition path. The engine no longer drives itself:
// its stages are named calls the layer makes in order, with the measurement of
// the source taken between them, so a case that wants a solve drives the layer.
//
// The clock only has to increase. Which pass is a detection pass is the
// cadence's business and no case here is about that.
static uint32_t g_nowMs = 0;
static bool pollOnce(VideoSourceAcquisition &acquisition)
{
    g_nowMs += VideoSourceAcquisition::DetectionIntervalMs;
    return acquisition.poll(g_nowMs);
}

// poll() gates on a line count steady over several passes before it will pay
// for a field rate measurement, so a solve takes more than one call.
static bool pollUntilSolved(VideoSourceAcquisition &acquisition)
{
    for (uint8_t i = 0;
         i < 4 * (Tv5725::SourceMeasurement::SteadySamples
                  + Tv5725::SourceMeasurement::LatchSettlePasses); ++i)
        if (pollOnce(acquisition))
            return true;
    return false;
}

// resolveFromSource() installs the reference sampling clock and then measures,
// and nothing read through a clock that has just been latched is the source's
// -- LatchSettlePasses have to be spent first. The engine reaches the solve
// over several passes, so a case about the outcome does too.
static bool resolveUntilSolved(VideoSourceAcquisition &acquisition)
{
    for (uint8_t i = 0;
         i < 2 * (Tv5725::SourceMeasurement::LatchSettlePasses
                  + Tv5725::SourceMeasurement::SteadySamples); ++i)
        if (acquisition.resolveFromSource())
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
    Tv5725::SourceMeasurement sampling;
    Tv5725::FramingTable framings;
    Tv5725::VideoPath engine;
    VideoSourceAcquisition acquisition;

    // hsyncLow is a count in ADC samples, so it means nothing without the
    // divider it was counted at: the pair is the duty, and the duty is what the
    // engine measures.
    SolvedEngine(uint16_t sourceLines = 311, float fieldRateHz = 50.08f,
                 uint16_t hsyncLow = 181,
                 const Tv5725::OutputMode *choice = &Tv5725::Mode1080p,
                 bool hsyncPositive = true,
                 uint16_t hsyncCountedAt = 2553)
        : engine(clock, sampling, framings), acquisition(sampling, engine)
    {
        Wire.reset();
        poisonChip();
        g_fieldRate = fieldRateHz;

        // A bus that answers, and a quiet interrupt byte. Nothing per-source is
        // written to a board that may not be there, and the poison sets every
        // latched interrupt bit -- including the one that arms a re-measure.
        Tv5725::Chip::holdPower(true);
        seed(0, 0x0F, 0, 8, 0);
        Wire.lockSyncProcessor();

        seed(3, 0x01, 0, 12, 1915);          // VDS_HSYNC_RST, output line - 1
        seed(3, 0x02, 4, 11, 1124);          // VDS_VSYNC_RST, output frame - 1
        seed(1, 0x0E, 0, 11, 1276);          // IF_HSYNC_RST, capture wrap - 1
        seed(5, 0x12, 0, 12, 2553);          // PLLAD_MD, the line in ADC samples
        seed(0, 0x1B, 0, 11, sourceLines);   // STATUS_SYNC_PROC_VTOTAL
        // Modelled rather than seeded: the count is in ADC samples, so it has
        // to follow the divider the engine is measuring through. Which end of
        // the pulse the line is counted from goes with it -- the bench source
        // is positive-going; every VESA mode below 800x600 is not.
        Wire.sourceHsync(hsyncLow, hsyncCountedAt, hsyncPositive);
        seed(0, 0x16, 3, 1, 1);              // STATUS_SYNC_PROC_VSACT, V on its own pin

        engine.setOutputMode(choice);
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));
    }

    ~SolvedEngine() { g_fieldRate = 50.08f; }
};

#endif  // TEST_SOLVED_ENGINE_H_
