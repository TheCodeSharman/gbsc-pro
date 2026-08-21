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

#include "Si5351Stubs.h"
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

// Counted because the cost is the point: this samples vsync edges through
// FrameSync, up to 250 ms a pulse, and the whole reason poll() has a cheap gate
// in front of it is that loop() cannot afford it on every pass.
static unsigned g_fieldRateCalls = 0;
float getSourceFieldRate(boolean) { ++g_fieldRateCalls; return g_fieldRate; }
void tv5725Log(const char *) {}

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
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 50.08f;
}

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

    // The rest of what PLLAD_LAT loads, and the decimators that follow the tap
    // it selects. 2250 samples on a 15574 Hz line is 35.0 MHz, the datasheet's
    // 40..20 MHz row, and 4x oversampling takes the tap two steps faster.
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_CKOS::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK1X::read() == 1);
    CHECK(Adc::ADC_CLK_ICLK2X::read() == 1);
    CHECK(Adc::DEC1_BYPS::read() == 0);
    CHECK(Adc::DEC2_BYPS::read() == 0);

    // Capture, released now the windows under it are the new mode's.
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);

    CHECK(registersWritten() == 57);
}

// poll() runs on every loop() pass, and the steadiness gate wants a few before
// it will pay for a field rate measurement.
static bool pollUntilSolved(Geometry &engine)
{
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        if (engine.poll())
            return true;
    return false;
}

// --- the scenarios, each the sketch's own call sequence ----------------------
//
// Two messages: the sketch says the mode changed, and loop() polls. The ORDER
// inside -- sampling, raster, clock, windows -- is the engine's, and what these
// pin is that it lands on the same chip state whichever way the source behaves
// on the way there.

TEST_CASE("a settled source is solved on the first poll that can measure it")
{
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    REQUIRE(pollUntilSolved(engine));

    checkBenchGeometry();

    SUBCASE("and nothing is outstanding afterwards") {
        Wire.reset();
        Wire.poison(Poison);
        CHECK_FALSE(engine.poll());
        CHECK(registersWritten() == 0);
    }
}

TEST_CASE("a custom preset inherits its divider rather than computing one")
{
    // A register dump replayed off the filesystem: its saved PLLAD_MD is a
    // value the user has a picture from, so it is adopted, not recomputed. The
    // saved divider is the one the solver would have chosen, so the chip ends
    // up in the same state by a different route.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), true, 4);
    REQUIRE(pollUntilSolved(engine));

    checkBenchGeometry();
}

TEST_CASE("a source still settling gets no geometry solved against it")
{
    // The measurements lag the mode change and do not fail when read early --
    // they return plausible garbage. Refusing has to mean refusing rather than
    // inheriting, so no window is written until they agree with each other.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    g_fieldRate = 0.0f;

    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    CHECK_FALSE(Wire.touched[1][0x18]);   // IF_HB_ST2, the capture window
    CHECK_FALSE(Wire.touched[1][0x1C]);   // IF_VB_ST
    CHECK_FALSE(Wire.touched[3][0x16]);   // VDS_HSCALE
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST, the raster

    SUBCASE("but the divider IS adopted, or every window defers forever") {
        // Deliberate: without a divider the capture window has no unit to be
        // measured in, and the pending flag is what stops the fallback becoming
        // permanent.
        CHECK(Wire.touched[5][0x12]);     // PLLAD_MD
        CHECK(Wire.touched[5][0x11]);     // PLLAD_LAT
    }

    SUBCASE("and it is solved by the poll after the source settles") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(engine));
        checkBenchGeometry();
    }
}

TEST_CASE("a line count outside what any source runs is never measured against")
{
    // 97 lines is what a preset load leaves behind, and it is perfectly steady
    // -- steadiness alone would call that settled and solve a raster for a
    // source that is not there yet.
    seedBenchSource();
    seedField(0, 0x1B, 0, 11, 97);
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);

    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    CHECK_FALSE(Wire.touched[3][0x16]);   // VDS_HSCALE
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST
    CHECK_FALSE(Wire.touched[5][0x12]);   // PLLAD_MD -- not even measured
}

TEST_CASE("entering bypass leaves nothing to solve")
{
    // In RGBHV bypass the VDS is out of the video path: there is no scaled
    // raster, so a solve must write nothing rather than size a window for one.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    REQUIRE(pollUntilSolved(engine));

    Wire.reset();
    Wire.poison(Poison);
    engine.enterBypass();
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    // Capture, and nothing else: bypass has no solve coming, so releasing it is
    // the only thing left to do.
    CHECK(registersWritten() == 1);
    CHECK(Wire.touched[4][0x21]);
}

TEST_CASE("a mode with no timings is given up on, not asked about forever")
{
    // The output resolutions with no OutputMode arrive as NULL, and nothing
    // about waiting will produce timings. Every pass that keeps the mode change
    // outstanding pays for a field rate measurement first.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(0, false, 4);
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    const unsigned settled = g_fieldRateCalls;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());
    CHECK(g_fieldRateCalls == settled);

    // The raster is left exactly as it was: a mode nobody could name is not a
    // reason to move one that is already driving a picture.
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST
}

// --- how often the source is measured ----------------------------------------

TEST_CASE("the source is measured once per poll, not once per thing that needs it")
{
    // getSourceFieldRate() samples vsync edges through FrameSync with no yield()
    // in the spin, up to 250 ms a pulse and ~40 ms at 50 Hz. Paying for it once
    // per consumer is not merely slow: each measurement is a separate reading of
    // a moving quantity, so the capture can be solved against a rate the raster
    // was not, and nothing downstream can tell.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), false, 4);

    // Two for a mode change, and no more: one reading has nothing to agree
    // with, so the pass that takes it stops there and the next one solves
    // everything from its own single reading.
    const unsigned before = g_fieldRateCalls;
    REQUIRE(pollUntilSolved(engine));
    CHECK(g_fieldRateCalls - before == 2);

    SUBCASE("and once for a framing the user asked for") {
        const unsigned solved = g_fieldRateCalls;
        engine.requestFraming(PanAndZoom(300, 120, 40, -15));
        engine.poll();
        CHECK(g_fieldRateCalls - solved == 1);
    }

    SUBCASE("and once for a pad press") {
        const unsigned solved = g_fieldRateCalls;
        REQUIRE(engine.zoom(16, 0));
        CHECK(g_fieldRateCalls - solved == 1);
    }
}

// --- a reset -----------------------------------------------------------------

TEST_CASE("a reset puts the framing back and re-solves everything from it")
{
    // Without one, a framing zoomed into a corner is only escapable by changing
    // mode or rebooting: the framing is the engine's own state and no register
    // holds it.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), false, 4);
    REQUIRE(pollUntilSolved(engine));

    REQUIRE(engine.zoom(400, 120));
    REQUIRE(engine.framing().horizontalZoom() != 0);
    REQUIRE(engine.framing().verticalZoom() != 0);

    // Re-seeded rather than merely wiped: the source measurements are INPUTS
    // the chip keeps supplying, and poisoning those would test the engine
    // solving against garbage rather than resetting.
    seedBenchSource();
    engine.reset();
    REQUIRE(pollUntilSolved(engine));

    CHECK(engine.framing().horizontalZoom() == 0);
    CHECK(engine.framing().verticalZoom() == 0);
    CHECK(engine.framing().horizontalPan() == 0);
    CHECK(engine.framing().verticalPan() == 0);

    // Not just the framing: the divider, the raster, the clock and both windows
    // land where a fresh mode change would put them.
    checkBenchGeometry();
}

// --- the freeze that covers a mode change ------------------------------------
//
// The windows land seconds after the load, once the source has settled into the
// new mode, so anything released at load time shows the previous mode's geometry
// against the new source until then.

TEST_CASE("capture is frozen across a mode change and released when it lands")
{
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 0);

    REQUIRE(pollUntilSolved(engine));
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
}

TEST_CASE("capture stays frozen while the source is still settling")
{
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    g_fieldRate = 0.0f;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 0);

    SUBCASE("and is released by the poll that solves it") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(engine));
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }
}

TEST_CASE("a mode change nothing will ever solve does not leave capture frozen")
{
    // Every path that stops the poll has to release it, or the picture is a
    // still frame for the rest of the session with nothing left to unstick it.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    SUBCASE("a mode with no timings") {
        engine.modeChanged(0, false, 4);
        for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
            CHECK_FALSE(engine.poll());
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }

    SUBCASE("and bypass, where there is no solve coming at all") {
        engine.modeChanged(benchMode(), false, 4);
        engine.enterBypass();
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }
}

// --- a horizontal total retimed outside the engine ---------------------------

TEST_CASE("a hand-retimed horizontal total is what the windows are fitted to")
{
    // The htotal search moves VDS_HSYNC_RST itself, on a board with no clock
    // generator to steer the frame time instead. The engine holds the raster
    // rather than reading it back, so a re-solve that is not told the new total
    // fits every window and both scales to the one the last solve chose.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    REQUIRE(pollUntilSolved(engine));
    const uint16_t scaleAtSolvedRaster = VideoProcessor::VDS_HSCALE::read();
    const uint16_t farEdgeAtSolvedRaster = VideoProcessor::VDS_DIS_HB_ST::read();

    REQUIRE(engine.rasterWidthChanged(2016));

    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 2015);

    // 100 px more raster to fill from the same capture, so the picture is
    // magnified further and reaches further along the line. VDS_HSCALE counts
    // DOWN with magnification.
    CHECK(VideoProcessor::VDS_HSCALE::read() < scaleAtSolvedRaster);
    CHECK(VideoProcessor::VDS_DIS_HB_ST::read() > farEdgeAtSolvedRaster);
}

// --- a framing request, which re-solves every window -------------------------

TEST_CASE("a framing request is drained and re-solves every window")
{
    // The web handler runs in a network callback rather than loop(), so it sets
    // the framing and loop() applies it. Every window is recomputed, pan
    // included -- inheriting one is what froze a picture at 620 lines.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), false, 4);
    REQUIRE(pollUntilSolved(engine));

    Wire.reset();
    Wire.poison(Poison);
    // A framing is not a mode change, so poll() applies it and still reports
    // nothing completed -- the caller's frame time lock has no ratio to forget.
    engine.requestFraming(PanAndZoom(300, 120, 40, -15));
    CHECK_FALSE(engine.poll());

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
