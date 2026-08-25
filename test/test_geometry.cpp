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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Tv5725.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

#include "FrameAt.h"

using namespace Tv5725;

static float g_fieldRate = 50.08f;

// Counted because the cost is the point: this samples vsync edges through
// FrameSync, up to 250 ms a pulse, and the whole reason poll() has a cheap gate
// in front of it is that loop() cannot afford it on every pass.
static unsigned g_fieldRateCalls = 0;

// The divider actually in force at the moment the rate is sampled. The rate is
// timed off the input formatter's test bus and the IF's line counter is the
// divider's, so this is the state the measurement is taken through.
static uint16_t g_dividerWhenSampled = 0;

// The input formatter's vertical blank at that same moment. The rate is timed
// off this block, and a window whose start lies beyond the frame never fires.
static uint16_t g_blankStartWhenSampled = 0;

float getSourceFieldRate(boolean)
{
    ++g_fieldRateCalls;
    g_dividerWhenSampled = (uint16_t)(Wire.bank[5][0x12] |
                                      ((Wire.bank[5][0x13] & 0x0F) << 8));
    g_blankStartWhenSampled = (uint16_t)(Wire.bank[1][0x1C] |
                                         ((Wire.bank[1][0x1D] & 0x07) << 8));
    return g_fieldRate;
}
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

// The two registers the engine is allowed to read, so a poison wipes the source
// itself: a case that poisons mid-test and expects the same source has to put
// them back.
static void seedSourceMeasurement()
{
    seedField(0, 0x19, 0, 12, 181);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x1B, 0, 11, 311);    // STATUS_SYNC_PROC_VTOTAL
    g_fieldRate = 50.08f;
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
    seedField(5, 0x12, 0, 12, 2250);   // PLLAD_MD
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    seedSourceMeasurement();
}

// Move the source to a different line count, leaving everything else as the
// bench seeded it. A different count at the same field rate is a different
// source as far as the key is concerned, which is what the table keys on.
static void seedSourceLines(uint16_t lines)
{
    seedField(0, 0x1B, 0, 11, lines);   // STATUS_SYNC_PROC_VTOTAL
}

static const OutputMode *benchMode() { return OutputMode::forFrameHeight(1125); }

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

    // The capture window, measured on the unit. The doubler is in the path on
    // this source, so the vertical counts half-lines.
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

    CHECK(registersWritten() == 60);
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

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    checkBenchGeometry();

    SUBCASE("and nothing is outstanding afterwards") {
        Wire.reset();
        Wire.poison(Poison);
        CHECK_FALSE(engine.poll());
        CHECK(registersWritten() == 0);
    }
}

TEST_CASE("a source still settling gets no geometry solved against it")
{
    // The measurements lag the mode change and do not fail when read early --
    // they return plausible garbage. Refusing has to mean refusing rather than
    // inheriting, so no window SOLVED from them is written until they agree
    // with each other. The state they are taken THROUGH is a separate thing and
    // is written at once: the divider below, and the vertical blank with it.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    g_fieldRate = 0.0f;

    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    CHECK_FALSE(Wire.touched[1][0x18]);   // IF_HB_ST2, the capture window
    CHECK_FALSE(Wire.touched[3][0x16]);   // VDS_HSCALE
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST, the raster

    SUBCASE("but the sampling state IS written, or every window defers forever") {
        // Without a divider the capture window has no unit to be measured in,
        // and the pending flag is what stops the fallback becoming permanent.
        CHECK(Wire.touched[5][0x12]);     // PLLAD_MD
        CHECK(Wire.touched[5][0x11]);     // PLLAD_LAT

        // And without a vertical blank inside the frame the input formatter
        // emits nothing to measure, so the refusal never ends.
        CHECK(InputFormatter::IF_VB_ST::read() == 0);
        CHECK(InputFormatter::IF_VB_SP::read() == 2);
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

    engine.modeChanged(benchMode(), 4);

    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        CHECK_FALSE(engine.poll());

    CHECK_FALSE(Wire.touched[3][0x16]);   // VDS_HSCALE
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST

    // The divider IS written, and to the reference rather than to anything
    // derived from 97: a count is only a measurement once the ADC is running at
    // a divider this pass chose, so refusing to touch it leaves the refusal
    // depending on the state that caused it.
    CHECK(Adc::PLLAD_MD::read() == SourceMeasurement::referenceDivider(true));
}

TEST_CASE("entering bypass leaves nothing to solve")
{
    // In RGBHV bypass the VDS is out of the video path: there is no scaled
    // raster, so a solve must write nothing rather than size a window for one.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
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

    engine.modeChanged(0, 4);
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
    engine.modeChanged(benchMode(), 4);

    // Two for a mode change, and no more: one reading has nothing to agree
    // with, so the pass that takes it stops there and the next one solves
    // everything from its own single reading.
    const unsigned before = g_fieldRateCalls;
    REQUIRE(pollUntilSolved(engine));
    CHECK(g_fieldRateCalls - before == 2);

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
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    REQUIRE(engine.zoom(400, 120));
    // The framing is a proportion now and carries no unit to compare, so the
    // zoom is witnessed by the window it moved off the default.
    REQUIRE(InputFormatter::IF_HB_SP2::read() != 118);
    REQUIRE(InputFormatter::IF_VB_SP::read() != 46);

    // Re-seeded rather than merely wiped: the source measurements are INPUTS
    // the chip keeps supplying, and poisoning those would test the engine
    // solving against garbage rather than resetting.
    seedBenchSource();
    engine.reset();
    REQUIRE(pollUntilSolved(engine));

    CHECK(InputFormatter::IF_HB_SP2::read() == 118);
    CHECK(InputFormatter::IF_HB_ST2::read() == 1008);
    CHECK(InputFormatter::IF_VB_SP::read() == 46);
    CHECK(InputFormatter::IF_VB_ST::read() == 578);

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

    engine.modeChanged(benchMode(), 4);
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 0);

    REQUIRE(pollUntilSolved(engine));
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
}

TEST_CASE("capture stays frozen while the source is still settling")
{
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
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
        engine.modeChanged(0, 4);
        for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
            CHECK_FALSE(engine.poll());
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }

    SUBCASE("and bypass, where there is no solve coming at all") {
        engine.modeChanged(benchMode(), 4);
        engine.enterBypass();
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }
}

// --- the framing belongs to the source ---------------------------------------

TEST_CASE("changing the output keeps the framing the user tuned")
{
    // applyPresets() is the one caller of modeChanged(), and it runs for a
    // SOURCE mode change and for a user picking a different output resolution.
    // The framing is a proportion of the capturable region, which is a property
    // of the input line -- so an output change moves neither the denominator nor
    // the user's intent, and dropping it makes every output change a re-tune.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    frameAt(engine, 300, 120, 40, -15);
    const PanAndZoom tuned = engine.framing();

    engine.modeChanged(OutputMode::forFrameHeight(525), 4);
    REQUIRE(pollUntilSolved(engine));

    CHECK(engine.framing() == tuned);
}

TEST_CASE("a source comes back to the framing it was left at")
{
    // The point of the table: tune a source, go somewhere else, come back, and
    // the picture is where it was left with nobody touching a control.
    // docs/framing-presets.md
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    frameAt(engine, 300, 120, 40, -15);
    const PanAndZoom tuned = engine.framing();

    // Away to another source entirely, and back.
    seedSourceLines(524);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    REQUIRE(engine.framing() != tuned);

    seedSourceLines(311);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    CHECK(engine.framing() == tuned);
}

TEST_CASE("a source nobody has framed takes no place in the table")
{
    // The table holds sixteen. Every solve seeds the framing from the placement
    // it computed, so a table that stored whatever the framing held on the way
    // out would fill with computed defaults and refuse the first real tuning.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    for (uint16_t lines = 311; lines <= 315; ++lines) {
        seedSourceLines(lines);
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
    }

    CHECK(engine.framings().count() == 0);
}

TEST_CASE("a source nobody has framed gets the computed default")
{
    // With no entry, the default exactly as before: the table adds recall, it
    // does not change what an untuned source looks like.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    const PanAndZoom untouched = engine.framing();

    seedSourceLines(524);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    const PanAndZoom other = engine.framing();

    // Never framed, so coming back gives the same default it gave the first
    // time rather than the other source's framing.
    seedSourceLines(311);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    CHECK(engine.framing() == untouched);
    CHECK(engine.framing() != other);
}

TEST_CASE("a framing restored from the file is applied when its source arrives")
{
    // Boot: the file is read before anything has been measured, so the entry
    // goes in against a key nothing has seen yet and has to be found when the
    // source turns up.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    const PanAndZoom stored(0.10f, 0.60f, 0.15f, 0.55f);
    REQUIRE(engine.rememberFraming(SourceKey(311, 50.08f), stored));

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    // The WINDOW, not the float. A solve re-grids the proportions onto the
    // capturable region it just measured, which is what carries a framing
    // across a mode change at all -- so the stored float does not come back
    // bit-identical and is not meant to. docs/framing-presets.md
    for (int vertical = 0; vertical < 2; ++vertical) {
        const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
        const uint16_t usable = engine.capturableOn(axis);
        CHECK(engine.originUnitsOn(axis)
              == lrintf(stored.originOn(axis) * (float)usable));
        CHECK(engine.extentUnitsOn(axis)
              == lrintf(stored.extentOn(axis) * (float)usable));
    }
}

TEST_CASE("a press stores the framing without leaving the source")
{
    // Storing only on the way out loses every tuning of a unit that is turned
    // off where it is used -- which is all of them. The in-memory table is free
    // to follow each press; it is the FLASH write that has to be debounced.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    REQUIRE(engine.framings().count() == 0);

    frameAt(engine, 300, 120, 40, -15);

    PanAndZoom stored;
    REQUIRE(engine.framings().find(SourceKey(311, 50.08f), &stored));
    CHECK(stored == engine.framing());
}

TEST_CASE("a reset forgets what the table stored for this source")
{
    // Otherwise the solve that follows the reset finds the entry and restores
    // exactly what was just discarded, and the control does nothing at all.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    const PanAndZoom untouched = engine.framing();

    frameAt(engine, 300, 120, 40, -15);
    REQUIRE(engine.framings().count() == 1);

    engine.reset();
    REQUIRE(pollUntilSolved(engine));

    CHECK(engine.framings().count() == 0);
    CHECK(engine.framing() == untouched);
}

TEST_CASE("the table says when it has something new to write")
{
    // A pad press must not write flash, so the sketch debounces -- and it needs
    // to know whether a write is owed at all, or every quiet tick costs one.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    const uint16_t settled = engine.framingRevision();

    SUBCASE("a solve that stores nothing leaves it alone") {
        REQUIRE(engine.resolve());
        CHECK(engine.framingRevision() == settled);
    }

    SUBCASE("and a source change that stores a tuning moves it") {
        frameAt(engine, 300, 120, 40, -15);
        seedSourceLines(524);
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));

        CHECK(engine.framingRevision() != settled);
    }

    SUBCASE("but a source change with nothing tuned does not") {
        seedSourceLines(524);
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));

        CHECK(engine.framingRevision() == settled);
    }
}

// --- every window follows the framing ----------------------------------------

TEST_CASE("a framed picture holds every window against the framing")
{
    // Every window is recomputed on every solve, pan included. Inheriting one
    // freezes the picture at the previous mode's size.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    frameAt(engine, 300, 120, 40, -15);
    Wire.reset();
    Wire.poison(Poison);
    seedSourceMeasurement();
    REQUIRE(engine.resolve());

    // The capture narrows 300 units horizontally and 120 vertically,
    // then moves 40 right and 15 up.
    CHECK(InputFormatter::IF_HB_SP2::read() == 308);
    CHECK(InputFormatter::IF_HB_ST2::read() == 898);
    CHECK(InputFormatter::IF_VB_SP::read() == 91);
    CHECK(InputFormatter::IF_VB_ST::read() == 503);
    CHECK(InputFormatter::IF_LINE_ST::read() == 64);
    CHECK(InputFormatter::IF_LINE_SP::read() == 1190);

    // Both scales rise to magnify the smaller capture onto the same raster.
    CHECK(VideoProcessor::VDS_HSCALE::read() == 353);
    CHECK(VideoProcessor::VDS_VSCALE::read() == 378);
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
    CHECK(VideoProcessor::VDS_VB_ST::read() == 1117);
    CHECK(VideoProcessor::VDS_HB_SP::read() == 8);
    CHECK(VideoProcessor::VDS_VB_SP::read() == 2);
    CHECK(VideoProcessor::VDS_DIS_HB_SP::read() == 136);
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() == 4);

    CHECK(FrameBuffer::PB_CAP_OFFSET::read() == 282);
    CHECK(FrameBuffer::PB_FETCH_NUM::read() == 150);

    // The raster did not change, so its registers are not rewritten.
    CHECK(registersWritten() == 32);
}

// --- the IF line counter follows the scan mode -------------------------------

TEST_CASE("a progressive source's vertical capture fits the counter it is on")
{
    // The IF counts the source's own lines when the doubler is out and
    // half-lines when it is in, the same halving IF_HSYNC_RST follows. A window
    // written past the count the counter reaches never fires.
    // docs/scaler-geometry-model.md "What the IF counter counts"
    Wire.reset();
    Wire.poison(Poison);
    seedField(3, 0x01, 0, 12, 1278);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1124);   // IF_HSYNC_RST, capture wrap - 1
    seedField(0, 0x19, 0, 12, 84);     // STATUS_SYNC_PROC_HLOW_LEN
    seedField(5, 0x12, 0, 12, 1124);   // PLLAD_MD
    seedField(0, 0x1B, 0, 11, 499);    // STATUS_SYNC_PROC_VTOTAL
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 75.0f;

    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    // 494 of the frame's 500 lines, magnified 2.26x to fill the 1125-line
    // raster. Doubling the frame put the stop at 994, which the counter never
    // reaches, and left the scale sized for a capture twice the arriving one.
    CHECK(InputFormatter::IF_VB_SP::read() == 3);
    CHECK(InputFormatter::IF_VB_ST::read() == 497);
    CHECK(VideoProcessor::VDS_VSCALE::read() == 453);
}

// --- a divider carried over from the previous mode ---------------------------

TEST_CASE("a divider the source cannot lock to is replaced before it is believed")
{
    // A divider carried from a faster line asks the ADC PLL for a frequency
    // under its lock range, and the PLL locks to every other hsync instead: the
    // sync processor then counts one line per two sent and twice the samples
    // per line. The count that produces is outside what any source runs, so the
    // steadiness gate refuses it on every pass and the line rate is never
    // measured -- which makes the refusal self-latching, because nothing
    // recomputes the divider that caused it.
    Wire.reset();
    Wire.poison(Poison);
    seedField(3, 0x01, 0, 12, 1278);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1124);   // IF_HSYNC_RST, capture wrap - 1
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 60.0f;

    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    // The capture write limit is what caps it here, not the ADC rating, which
    // is why the same 1124 comes out of the 75 Hz mode as well.
    REQUIRE(Adc::PLLAD_MD::read() == 1124);

    // The source changes down. The sketch reloads a preset for the new
    // standard, so the engine is told the mode changed -- but the divider it is
    // holding is the one that made the count unmeasurable.
    seedField(0, 0x1B, 0, 11, 155);    // one line counted per two sent
    seedField(0, 0x17, 0, 12, 2247);   // and twice the samples per line, which
    seedField(0, 0x19, 0, 12, 181);    // is the evidence of the multiple
    g_fieldRate = 50.08f;
    engine.modeChanged(benchMode(), 4);

    // Nothing to wait for. The reference is written before the count is read,
    // so the divider that made the count unmeasurable is gone on the first
    // pass -- and replacing it is not a solve, so the capture stays frozen and
    // poll() still says no.
    CHECK_FALSE(engine.poll());
    CHECK(Adc::PLLAD_MD::read() == 2250);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == 1125);
    CHECK(SyncProcessor::SP_RT_HS_SP::read() == 2092);
    CHECK(Adc::PLLAD_LAT::read() == 1);

    SUBCASE("and the source it was blind to is then solved for") {
        // With the divider latched the PLL locks to every hsync, so the sync
        // processor counts the source as it really is.
        seedField(0, 0x1B, 0, 11, 311);
        seedField(0, 0x17, 0, 12, 2250);
        REQUIRE(pollUntilSolved(engine));
        checkBenchGeometry();
    }
}

TEST_CASE("the scan mode is corrected even when the source cannot be measured")
{
    // The circularity this breaks: the input formatter's measurements are only
    // meaningful once its scan mode matches the source, so a scan mode left
    // wrong makes measureLineRate() fail, and a scan mode derived AFTER that
    // gate is never reached. Measured on the bench -- a source returning from
    // 524 lines to 311 reloaded its preset, armed the engine, and still held
    // PLLAD_MD 1124 with the line doubler bypassed.
    //
    // STATUS_SYNC_PROC_VTOTAL is what breaks it. The sync processor counts the
    // source directly and does not care what the input formatter is doing, so
    // the line count is available while everything downstream of it is not.
    Wire.reset();
    Wire.poison(0xFF);

    seedField(0, 0x1B, 0, 11, 311);    // STATUS_SYNC_PROC_VTOTAL, a 15 kHz source
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 0.0f;                // nothing measurable: every gate below fails

    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), 4);
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        engine.poll();

    CHECK(InputFormatter::IF_HS_DEC_FACTOR::read() == 1);
    CHECK(InputFormatter::IF_LD_SEL_PROV::read() == 0);
    CHECK(InputFormatter::IF_LD_RAM_BYPS::read() == 0);
    CHECK(InputFormatter::IF_PRGRSV_CNTRL::read() == 0);
}

TEST_CASE("the engine arms itself when the source line count changes")
{
    // A mode change IS a change in the source, and the sync processor's line
    // count is the engine's own measurement of it. Waiting to be told makes the
    // trigger a classification: the sketch reloads a preset when getVideoMode()
    // reports a different STANDARD, and two RISC OS modes that are nothing alike
    // can share one -- a 311-line and a 524-line RGBHV source are both filed
    // under PresetLoad::ScalingRgbhvStandard.
    //
    // Measured on the bench: a 524 -> 311 return reloaded its preset,
    // GBS_PRESET_ID 5 -> 21, and still held PLLAD_MD 1124 sixty seconds later
    // with every other reading healthy. The solve had completed against the
    // count taken while the source was still moving, and nothing re-armed when
    // it settled.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    REQUIRE(Adc::PLLAD_MD::read() == 2250);

    // The source moves, and NOBODY tells the engine.
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    g_fieldRate = 60.0f;

    bool solved = false;
    for (uint8_t i = 0; i < 8 * SourceMeasurement::SteadySamples && !solved; ++i)
        solved = engine.poll();

    CHECK(solved);
    CHECK(Adc::PLLAD_MD::read() == 1124);
    CHECK(InputFormatter::IF_PRGRSV_CNTRL::read() == 1);
}

TEST_CASE("bypass measures nothing, so it reports no line rate")
{
    // The held rate outlives the mode that produced it, and bypass never
    // measures: the sync processor's readers would then be configured for a
    // source that has been gone since the excursion started.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));
    REQUIRE(engine.sourceLowLineRate());

    engine.enterBypass();
    CHECK(engine.sourceLineRateHz() == 0);
    CHECK_FALSE(engine.sourceLowLineRate());
}

TEST_CASE("the source is measured through a known divider, not the last mode's")
{
    // The field rate is timed at DEBUG_IN_PIN off the input formatter's test
    // bus, and IF_HSYNC_RST comes from the divider -- so a divider left over
    // from the previous mode corrupts the reading that would correct it, and
    // the state is self-latching. Measured on the bench at 640x480: 171.53 and
    // 184.60 Hz against a real 60, every reading refused, no divider chosen.
    // docs/investigations/field-rate-measured-downstream.md
    //
    // The reference is the divider the capture write limit allows, which puts
    // the IF line counter on WriteLimitUnits whatever the scan mode -- one
    // state, every source.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    SUBCASE("a line-doubled source is sampled at twice the write limit") {
        g_dividerWhenSampled = 0;
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_dividerWhenSampled == SourceMeasurement::referenceDivider(true));
    }

    SUBCASE("a progressive source is sampled at the write limit itself") {
        seedField(0, 0x1B, 0, 11, 524);   // STATUS_SYNC_PROC_VTOTAL
        g_fieldRate = 60.0f;
        g_dividerWhenSampled = 0;
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_dividerWhenSampled == SourceMeasurement::referenceDivider(false));
    }

    SUBCASE("the divider the previous mode left is not what it is sampled through") {
        // 1124 is what a 524-line source solves to, and it is the value the
        // bench sticks on when a return to 311 lines cannot measure.
        seedField(5, 0x12, 0, 12, 1124);
        g_dividerWhenSampled = 0;
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_dividerWhenSampled != 1124);
    }
}

TEST_CASE("the source is measured through a known vertical blank, not the last mode's")
{
    // The field rate is timed off the input formatter's test bus and HPERIOD_IF
    // is counted inside the same block, and neither produces anything while
    // IF_VB_ST lies beyond the frame -- the window never fires, so there is no
    // edge to time and no line period to read. A window solved for a taller mode
    // therefore strands the measurement that would replace it, which is why the
    // fault only appears when the frame SHRINKS.
    //
    // Measured on the bench: IF_VB_ST 578 against a 524-line source gives
    // `524 lines x 0.00 Hz` and HPERIOD_IF 50 where 213 is due, and writing the
    // window alone restores both.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    SUBCASE("a frame that shrank is not measured through the taller mode's window") {
        seedField(0, 0x1B, 0, 11, 524);   // STATUS_SYNC_PROC_VTOTAL
        g_fieldRate = 60.0f;
        seedField(1, 0x1C, 0, 11, 578);   // IF_VB_ST, solved for 311 doubled

        g_blankStartWhenSampled = 0xFFFF;
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_blankStartWhenSampled < 524);
    }

    SUBCASE("the line-doubled frame is measured through a window inside it too") {
        seedField(1, 0x1C, 0, 11, 700);   // beyond even the doubled 622

        g_blankStartWhenSampled = 0xFFFF;
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_blankStartWhenSampled < 2 * 311);
    }

    SUBCASE("a re-solve on an unchanged source parks it too") {
        // The reference divider is a function of the scan mode alone, so a
        // source that did not move asks for the one already in force. Keying
        // the parking on the reference having CHANGED therefore never fires
        // here -- and this is the case that matters, because a window is not
        // only stranded by a mode change.
        engine.modeChanged(benchMode(), 4);
        REQUIRE(pollUntilSolved(engine));

        seedField(1, 0x1C, 0, 11, 700);
        g_blankStartWhenSampled = 0xFFFF;
        engine.reset();
        REQUIRE(pollUntilSolved(engine));
        CHECK(g_blankStartWhenSampled < 2 * 311);
    }
}

TEST_CASE("a divider from another mode does not stop the source being counted")
{
    // The line count is a MEASUREMENT, taken by the sync processor in ADC
    // clocks, so it is only meaningful once the ADC is running at a divider
    // this pass chose. Left on the previous mode's, the PLL sits outside its
    // lock range and the count that comes back is not the source's -- and the
    // gate that count feeds is what stands between here and the reference being
    // applied. Measured on the bench: a 311-line source read 155 with the
    // divider left at 1124, held indefinitely, and writing the reference by
    // hand moved it to 311 with the PLL locking at once.
    seedBenchSource();
    seedField(5, 0x12, 0, 12, 1124);   // PLLAD_MD, a progressive mode's
    seedField(0, 0x1B, 0, 11, 155);    // the count that divider produces

    DisplayClock clock;
    Geometry engine(clock);
    engine.modeChanged(benchMode(), 4);

    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        engine.poll();

    CHECK(Adc::PLLAD_MD::read() == SourceMeasurement::referenceDivider(true));
}

TEST_CASE("an interrupt re-measures a source whose line count did not move")
{
    // The line count is the only change sourceMoved() can see, so a source that
    // returns at the same count and a different field rate is invisible to it.
    // The chip latches the disturbance instead, and measuring is now an act that
    // moves the sampling clock, so it needs an event rather than a schedule.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    engine.modeChanged(benchMode(), 4);
    REQUIRE(pollUntilSolved(engine));

    SUBCASE("a quiet source is left alone") {
        g_fieldRateCalls = 0;
        for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
            CHECK_FALSE(engine.poll());
        CHECK(g_fieldRateCalls == 0);
    }

    SUBCASE("an interrupted one is measured again") {
        engine.sourceInterrupted();
        g_fieldRateCalls = 0;
        CHECK(pollUntilSolved(engine));
        CHECK(g_fieldRateCalls > 0);
    }

    SUBCASE("bypass has no raster to re-solve, so it stays put") {
        engine.enterBypass();
        engine.sourceInterrupted();
        g_fieldRateCalls = 0;
        for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
            CHECK_FALSE(engine.poll());
        CHECK(g_fieldRateCalls == 0);
    }
}

TEST_CASE("the reference is re-applied when the count it was sized from moves")
{
    // holdReferenceSampling() picks PLLAD_KS from the line count, because the
    // ADC's crossover row is a function of CKO and CKO is the divider times the
    // line rate. A count caught mid-transition picks the wrong octave, and the
    // divider alone cannot say so: the reference for a scan mode does not
    // change, so an early return keyed on it leaves KS an octave out with
    // PLLAD_MD correct. Measured on the bench as KS 1 where 2 was due, and the
    // input formatter's test output stops -- `524 lines x 0.00 Hz`.
    seedBenchSource();
    DisplayClock clock;
    Geometry engine(clock);

    // A count high enough to put CKO over the 40 MHz crossover at the
    // progressive reference, then the settled one, which is under it.
    // The rate stays unmeasurable throughout, which is the state this has to
    // hold in: on the bench the wrong KS is WHY nothing can be measured, so a
    // later solve is not there to put it right.
    g_fieldRate = 0.0f;

    seedField(0, 0x1B, 0, 11, 700);
    engine.modeChanged(benchMode(), 4);
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        engine.poll();

    seedField(0, 0x1B, 0, 11, 524);
    for (uint8_t i = 0; i < 2 * SourceMeasurement::SteadySamples; ++i)
        engine.poll();

    // PLLAD_KS, s5_16[5:4], against what the settled count asks for.
    const uint8_t ks = (uint8_t)((Wire.bank[5][0x16] >> 4) & 0x03);
    CHECK(ks == Adc::postDividerFor(
                    (uint32_t)Wire.field(5, 0x12, 0, 12) * 524u * 60u));
}
