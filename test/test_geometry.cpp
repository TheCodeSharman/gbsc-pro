// Host-compiled tests for what Tv5725::Geometry writes -- `make -C test geometry`.
//
// One case per entry point, each the sketch's own call sequence, driving Geometry
// at the top and reading the chip back through the firmware's own register
// declarations. A failure names the field and prints both values, and every
// number here is checkable against docs/scaler-geometry-model.md.
//
// Anchored to hardware rather than to itself. IF_HB 118..1008, IF_VB 46..578,
// HSCALE 524, VSCALE 487 and the 1915 x 1124 raster are measured on the unit.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameBuffer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputRaster.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Tv5725.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

static float g_fieldRate = 50.08f;
float getSourceFieldRate(boolean) { return g_fieldRate; }

// Neither a preset table's value nor the firmware's, so a read-back
// distinguishes a fresh write from a leftover.
static const uint8_t Poison = 0xE2;

static void seedField(uint8_t seg, uint8_t reg, uint8_t offset, uint8_t width,
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

// The bench RiscPC at 320x256@50 into the engine's own 1916 x 1126 raster.
// Every value is an INPUT: a raster already solved, and three measurements.
static void seedBenchSource()
{
    Wire.reset();
    Wire.poison(Poison);
    seedField(3, 0x01, 0, 12, 1915);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1125);   // IF_HSYNC_RST, capture wrap - 1
    seedField(0, 0x19, 0, 12, 181);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(5, 0x12, 0, 12, 2250);   // PLLAD_MD
    seedField(0, 0x1B, 0, 11, 311);    // STATUS_SYNC_PROC_VTOTAL
    g_fieldRate = 50.08f;
}

static const uint32_t BenchLineRate = 311 * 50;
static const OutputMode *benchMode() { return OutputRaster::modeFor(1125); }

// A stray write lands somewhere nothing below reads, so it moves this and
// nothing else. Reading every field but counting none would miss it.
static unsigned registersWritten()
{
    unsigned written = 0;
    for (uint8_t seg = 0; seg < FakeTwoWire::Segments; ++seg)
        for (int reg = 0; reg < 256; ++reg)
            if (Wire.touched[seg][reg])
                ++written;
    return written;
}

// --- what a whole solve puts on the chip -------------------------------------

static void checkBenchGeometry()
{
    // The vertical PLL, from the frame the raster was solved for. These five
    // have not been migrated out of Tv5725::Tv5725 into a subsystem yet.
    CHECK(Tv5725::Tv5725::PLL_VS::read() == 1);
    CHECK(Tv5725::Tv5725::PLL_VS2::read() == 1);
    CHECK(Tv5725::Tv5725::PLL_VS4::read() == 0);
    CHECK(Tv5725::Tv5725::PLL_2XV::read() == 0);
    CHECK(Tv5725::Tv5725::PLL_4XV::read() == 1);

    // The sampling divider in its three registers: IF_HSYNC_RST is PLLAD_MD/2
    // and SP_RT_HS_SP is 93% of it. One quantity, never read back.
    CHECK(Adc::PLLAD_MD::read() == 2250);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == 1125);
    CHECK(SyncProcessor::SP_RT_HS_SP::read() == 2092);

    // PLLAD_LAT is the rising edge that loads MD into the PLL, so a divider
    // written after it leaves the ADC clocking at the old one.
    CHECK(Adc::PLLAD_LAT::read() == 1);
    CHECK(Adc::PLLAD_VCORST::read() == 0);
    CHECK(Adc::PLLAD_LEN::read() == 1);
    CHECK(Adc::PLLAD_TEST::read() == 0);
    CHECK(Adc::PLLAD_TS::read() == 0);
    CHECK(Adc::PLLAD_PDZ::read() == 0);
    CHECK(Adc::PLLAD_FS::read() == 1);
    CHECK(Adc::PLLAD_BPS::read() == 1);

    // The capture window, measured on the unit. The vertical counts HALF-lines,
    // and reading them as whole lines doubles the picture.
    CHECK(InputFormatter::IF_HB_SP2::read() == 118);
    CHECK(InputFormatter::IF_HB_ST2::read() == 1008);
    CHECK(InputFormatter::IF_VB_SP::read() == 46);
    CHECK(InputFormatter::IF_VB_ST::read() == 578);

    // The progressive line window spans exactly one line from where it starts,
    // and may run past the end of the line without that being a fault.
    CHECK(InputFormatter::IF_LINE_ST::read() == 64);
    CHECK(InputFormatter::IF_LINE_SP::read() == 1190);

    // Both scales computed from the capture and the raster, never inherited.
    CHECK(VideoProcessor::VDS_HSCALE::read() == 524);
    CHECK(VideoProcessor::VDS_VSCALE::read() == 487);
    CHECK(VideoProcessor::VDS_HSCALE_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_VSCALE_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_SYNC_EN::read() == 0);
    CHECK(VideoProcessor::VDS_FIELDAB_EN::read() == 1);
    CHECK(VideoProcessor::VDS_DFIELD_EN::read() == 0);
    CHECK(VideoProcessor::VDS_FIELD_FLIP::read() == 0);
    CHECK(VideoProcessor::VDS_HALF_EN::read() == 1);
    CHECK(VideoProcessor::VDS_SRESET::read() == 1);

    // The raster the engine solved, one less than the total on each axis.
    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 1915);
    CHECK(VideoProcessor::VDS_VSYNC_RST::read() == 1124);
    CHECK(VideoProcessor::VDS_HS_ST::read() == 0);
    CHECK(VideoProcessor::VDS_HS_SP::read() == 32);
    CHECK(VideoProcessor::VDS_VS_ST::read() == 0);
    CHECK(VideoProcessor::VDS_VS_SP::read() == 5);
    CHECK(VideoProcessor::VDS_VSYN_SIZE1::read() == 1126);
    CHECK(VideoProcessor::VDS_VSYN_SIZE2::read() == 1126);

    // The memory window IS the display window, allocating nothing spare, so
    // playback never walks past the written picture.
    CHECK(VideoProcessor::VDS_HB_ST::read() == VideoProcessor::VDS_DIS_HB_ST::read());
    CHECK(VideoProcessor::VDS_VB_ST::read() == VideoProcessor::VDS_DIS_VB_ST::read());
    CHECK(VideoProcessor::VDS_HB_ST::read() == 1849);
    CHECK(VideoProcessor::VDS_VB_ST::read() == 1118);

    // VDS_HB_SP is on its floor of 8 -- below that the left of the picture
    // corrupts -- so the display window carries the placement instead.
    CHECK(VideoProcessor::VDS_HB_SP::read() == 8);
    CHECK(VideoProcessor::VDS_VB_SP::read() == 1);
    CHECK(VideoProcessor::VDS_DIS_HB_SP::read() == 112);
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() == 3);

    // The playback burst, sized from the capture width so the fetch rate does
    // not move with the scale.
    CHECK(FrameBuffer::PB_CAP_OFFSET::read() == 282);
    CHECK(FrameBuffer::PB_FETCH_NUM::read() == 223);

    CHECK(registersWritten() == 53);
}

// --- the scenarios, each the sketch's own call sequence ----------------------
//
// What is pinned is the ORDER, which doPostPresetLoadSteps() and
// runSyncWatcher() state and which the engine depends on.

TEST_CASE("a scaled preset load writes the whole geometry")
{
    seedBenchSource();
    Geometry engine;

    engine.solveSampling(BenchLineRate, 4);
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    engine.solveFromScratch();

    checkBenchGeometry();
}

TEST_CASE("a custom preset inherits its divider rather than computing one")
{
    // A register dump replayed off the filesystem: its saved PLLAD_MD is a
    // value the user has a picture from, so it is adopted, not recomputed. The
    // saved divider is the one the solver would have chosen, so the chip ends
    // up in the same state by a different route.
    seedBenchSource();
    Geometry engine;

    engine.adoptSampling();
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    engine.solveFromScratch();

    checkBenchGeometry();
}

TEST_CASE("a raster that deferred is finished by the sync watcher")
{
    // The field rate is unmeasurable while the source settles, so solveRaster()
    // refuses rather than solving against a transient. runSyncWatcher() retries
    // the WHOLE sequence once sync is stable, and the retry has to land on the
    // state a settled source would have solved for directly.
    seedBenchSource();
    Geometry engine;

    g_fieldRate = 0.0f;
    engine.solveSampling(BenchLineRate, 4);
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    REQUIRE(engine.rasterPending());

    g_fieldRate = 50.08f;
    REQUIRE(engine.solveRaster());
    engine.solveFromScratch();

    checkBenchGeometry();
}

TEST_CASE("a divider that deferred is finished by the sync watcher")
{
    // Moving the divider moves the capture window with it, so the windows are
    // re-solved after the retry rather than left sized for the old one.
    seedBenchSource();
    Geometry engine;

    engine.adoptSampling();
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    engine.solveSampling(0, 4);
    REQUIRE(engine.samplingPending());

    REQUIRE(engine.solveSampling(BenchLineRate, 4));
    engine.solveFromScratch();

    checkBenchGeometry();
}

TEST_CASE("entering bypass leaves nothing to solve")
{
    // In RGBHV bypass the VDS is out of the video path: there is no scaled
    // raster, so a solve must write nothing rather than size a window for one.
    seedBenchSource();
    Geometry engine;

    engine.solveSampling(BenchLineRate, 4);
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    engine.solveFromScratch();

    Wire.reset();
    Wire.poison(Poison);
    engine.enterBypass();
    engine.solveFromScratch();

    CHECK(registersWritten() == 0);
}

// --- a framing request, which re-solves every window -------------------------

TEST_CASE("a framing request is drained and re-solves every window")
{
    // The web handler runs in a network callback rather than loop(), so it sets
    // the framing and loop() applies it. Every window is recomputed, pan
    // included -- inheriting one is what froze a picture at 620 lines.
    seedBenchSource();
    Geometry engine;

    engine.solveSampling(BenchLineRate, 4);
    engine.adoptRaster();
    engine.solveRaster(benchMode());
    engine.solveFromScratch();

    Wire.reset();
    Wire.poison(Poison);
    engine.requestFraming(PanAndZoom(300, 120, 40, -15));
    REQUIRE(engine.applyRequested());

    // The capture narrows 300 units horizontally and 120 half-lines vertically,
    // then moves 40 right and 15 up.
    CHECK(InputFormatter::IF_HB_SP2::read() == 308);
    CHECK(InputFormatter::IF_HB_ST2::read() == 898);
    CHECK(InputFormatter::IF_VB_SP::read() == 120);
    CHECK(InputFormatter::IF_VB_ST::read() == 1125);
    CHECK(InputFormatter::IF_LINE_ST::read() == 64);
    CHECK(InputFormatter::IF_LINE_SP::read() == 1190);

    // Both scales rise to magnify the smaller capture onto the same raster.
    CHECK(VideoProcessor::VDS_HSCALE::read() == 353);
    CHECK(VideoProcessor::VDS_VSCALE::read() == 919);
    CHECK(VideoProcessor::VDS_HSCALE_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_VSCALE_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_SYNC_EN::read() == 0);
    CHECK(VideoProcessor::VDS_FIELDAB_EN::read() == 1);
    CHECK(VideoProcessor::VDS_DFIELD_EN::read() == 0);
    CHECK(VideoProcessor::VDS_FIELD_FLIP::read() == 0);
    CHECK(VideoProcessor::VDS_HALF_EN::read() == 1);
    CHECK(VideoProcessor::VDS_SRESET::read() == 1);

    CHECK(VideoProcessor::VDS_HB_ST::read() == VideoProcessor::VDS_DIS_HB_ST::read());
    CHECK(VideoProcessor::VDS_VB_ST::read() == VideoProcessor::VDS_DIS_VB_ST::read());
    CHECK(VideoProcessor::VDS_HB_ST::read() == 1845);
    CHECK(VideoProcessor::VDS_VB_ST::read() == 1119);
    CHECK(VideoProcessor::VDS_HB_SP::read() == 8);
    CHECK(VideoProcessor::VDS_VB_SP::read() == 2);
    CHECK(VideoProcessor::VDS_DIS_HB_SP::read() == 136);
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() == 3);

    CHECK(FrameBuffer::PB_CAP_OFFSET::read() == 282);
    CHECK(FrameBuffer::PB_FETCH_NUM::read() == 150);

    // The raster did not change, so its registers are not rewritten.
    CHECK(registersWritten() == 32);
}
