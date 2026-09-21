// Host-compiled tests for what Tv5725::VideoPath writes -- `make -C test geometry`.
//
// One case per entry point, each the sketch's own call sequence, driving VideoPath
// at the top and reading the chip back through the firmware's own register
// declarations. A failure names the field and prints both values, and every
// number here is checkable against docs/scaler-geometry-model.md.
//
// Anchored to hardware rather than to itself. IF_HB 118..1008, IF_VB 46..578,
// HSCALE 524, VSCALE 487 and the 1915 x 1124 raster are measured on the unit.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CheckNear.h"
#include "MeasuredSource.h"
#include "Si5351Stubs.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameBuffer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoPath.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ModeDetect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Tv5725.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

#include "FrameAt.h"
#include "RegistersWritten.h"
#include "DebugPinStub.h"

using namespace Tv5725;

// What a 1080p raster affords this source: Axis::maximumCapture(1600), rounded
// even. Not a constant of the part -- change the output resolution and it
// changes with it.
static const uint16_t RasterDivider = 1446;

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

uint32_t debugPinPulseTicks()
{
    ++g_fieldRateCalls;
    g_dividerWhenSampled = (uint16_t)(Wire.bank[5][0x12] |
                                      ((Wire.bank[5][0x13] & 0x0F) << 8));
    g_blankStartWhenSampled = (uint16_t)(Wire.bank[1][0x1C] |
                                         ((Wire.bank[1][0x1D] & 0x07) << 8));
    return ticksForHz(g_fieldRate);
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

// The registers the engine is allowed to read, so a poison wipes the source
// itself: a case that poisons mid-test and expects the same source has to put
// them back.
// A raster the panel takes straight: progressive, above the line doubler, and
// at a rate that reaches the sink. What pass-through is decided from.
static void seedPassThroughSource()
{
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(0, 0x16, 0, 1, 0);       // STATUS_SYNC_PROC_HSPOL, negative-going
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 60.0f;
}

// The bench source's hsync as a share of its line, which is what the key
// carries. Modelled rather than seeded as a count, because the sync processor
// counts in ADC clocks: pin the count and the share moves with every divider
// the engine chooses, which no source does.
static const float BenchSyncWidth = 181.0f / 2250.0f;

// The bench source's vertical sync polarity, the other half of what the key
// carries beyond the count and the rate. `sync_pol:0` in the monitor
// definition, which reads 1.
static const bool BenchVsyncPositive = true;

static void seedSourceMeasurement()
{
    Wire.sourceHsync(181, 2250, true);
    seedField(0, 0x1B, 0, 11, 311);    // STATUS_SYNC_PROC_VTOTAL
    // The bench source's hsync is positive-going, which is what puts the pulse
    // at the head of the line. Unseeded this reads 0, the inverted case, and
    // the capture window then correctly stops guarding a head with no pulse in
    // it -- so leaving it out tests a source this fixture is not describing.
    seedField(0, 0x16, 0, 1, 1);       // STATUS_SYNC_PROC_HSPOL
    // The bench mode is sync_pol:0, both polarities positive, and the vertical
    // one is in the key -- so a fixture leaving it unseeded describes a source
    // the bench does not run.
    seedField(0, 0x16, 2, 1, 1);       // STATUS_SYNC_PROC_VSPOL
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    g_fieldRate = 50.08f;
}

// Poisoned, HPERIOD_IF reads a value that implies a plausible line rate, and
// measureLineRate() prefers it to the injected field rate. A case that wants the
// rate it injects has to say nothing was measured.
static void poisonChip()
{
    Wire.poison(Poison);
    seedField(0, 0x06, 0, 9, 0);       // HPERIOD_IF
}

// The bench RiscPC at 320x256@50 into the engine's own 1916 x 1126 raster.
// Every value is an INPUT: a raster already solved, and three measurements.
static void seedBenchSource()
{
    Wire.reset();
    poisonChip();
    seedField(3, 0x01, 0, 12, 1915);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1125);   // IF_HSYNC_RST, capture wrap - 1
    seedField(5, 0x12, 0, 12, 2250);   // PLLAD_MD
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running

    // A bus that answers, and a quiet interrupt byte: the poison sets every
    // latched bit, including the one that arms a re-measure on every pass.
    Chip::holdPower(true);
    seedField(0, 0x0F, 0, 8, 0);
    Wire.lockSyncProcessor();
    seedSourceMeasurement();
}

// Move the source to a different line count, leaving everything else as the
// bench seeded it. A different count at the same field rate is a different
// source as far as the key is concerned, which is what the table keys on.
static void seedSourceLines(uint16_t lines)
{
    seedField(0, 0x1B, 0, 11, lines);   // STATUS_SYNC_PROC_VTOTAL
}

// The input formatter's measurement of the frame in half-lines, and the bit
// that says it completed. Unseeded it reads nothing, which is the separate-sync
// case and makes no claim about the count either way.
static void seedSourceHalfLines(uint16_t halfLines)
{
    seedField(0, 0x07, 1, 11, halfLines);   // VPERIOD_IF
    seedField(0, 0x00, 0, 1, 1);            // STATUS_IF_VT_OK
}

static const OutputMode *benchMode() { return &Mode1080p; }

// The one quantity in its three registers: PLLAD_MD, IF_HSYNC_RST, SP_RT_HS_SP.
static uint16_t dividerInForce() { return (uint16_t)Wire.field(5, 0x12, 0, 12); }
static uint16_t lineCounterInForce() { return (uint16_t)Wire.field(1, 0x0E, 0, 11); }
static uint16_t retimeStopInForce() { return (uint16_t)Wire.field(5, 0x4B, 0, 12); }

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
    CHECK(Adc::PLLAD_MD::read() == 2200);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == 1100);
    CHECK(SyncProcessor::SP_RT_HS_SP::read() == 2046);

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

    // The capture window: this source runs no raster the standards state, so it
    // is placed across the envelope of what real sources put on a line. The
    // doubler is in the path here, so the vertical counts half-lines.
    CHECK(InputFormatter::IF_HB_SP2::read() == 129);
    CHECK(InputFormatter::IF_HB_ST2::read() == 1080);
    CHECK(InputFormatter::IF_VB_SP::read() == 38);
    CHECK(InputFormatter::IF_VB_ST::read() == 620);

    // The progressive line window spans exactly one line from where it starts,
    // and may run past the end of the line without that being a fault.
    CHECK(InputFormatter::IF_LINE_ST::read() == 64);
    CHECK(InputFormatter::IF_LINE_SP::read() == 1165);

    // Both scales computed from the capture and the raster, never inherited.
    CHECK(VideoProcessor::VDS_HSCALE::read() == 583);
    CHECK(VideoProcessor::VDS_VSCALE::read() == 552);
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
    // Two units short of where the picture ends on each axis: the scaler
    // interpolates between two capture units, so the last unit an aperture
    // closing on the picture would show reads the unit after the last one
    // captured, which is memory the previous mode left behind. Vertically one
    // output row more again, which the bench measured and nothing explains.
    CHECK(VideoProcessor::VDS_HB_ST::read() == 1808);
    CHECK(VideoProcessor::VDS_VB_ST::read() == 1117);

    // And the horizontal window is an ODD number of units wide, which is what
    // reaches the picture: an even one shears.
    // docs/known-issues.md
    CHECK((VideoProcessor::VDS_HB_ST::read()
           - VideoProcessor::VDS_HB_SP::read()) % 2 == 1);

    // The picture opens at the back porch the output mode states, so the write
    // floor of 8 no longer binds: 140 is 32 of sync and 108 of porch, which is
    // 1080p60's 996.6 ns at this clock, and 41 is its 5 sync lines and 36 of
    // porch. Below 41 the window would open with vsync still asserted.
    CHECK(VideoProcessor::VDS_HB_SP::read() == 41);
    CHECK(VideoProcessor::VDS_VB_SP::read() == 39);
    // One capture unit past the porch horizontally: the write origin marks
    // where content first appears, and that unit is only partly written.
    // Vertically the aperture opens ON the picture -- reading before the first
    // written line comes back as nothing, so the unit would buy no picture and
    // cost a black bar across the top of the screen.
    CHECK(VideoProcessor::VDS_DIS_HB_SP::read() == 142);
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() == 41);
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() > VideoProcessor::VDS_VS_SP::read());

    // The playback burst, sized from the capture width so the fetch rate does
    // not move with the scale.
    CHECK(FrameBuffer::PB_CAP_OFFSET::read() == 276);
    CHECK(FrameBuffer::PB_FETCH_NUM::read() == 238);

    // The rest of what PLLAD_LAT loads, and the decimators that follow the tap
    // it selects. 2508 samples on a 15574 Hz line is 39.1 MHz, just inside the
    // datasheet's 40..20 MHz row, and 4x oversampling takes the tap two steps
    // faster. The divider sits hard against that row's ceiling, because the row
    // is what installs the oversampling.
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_CKOS::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK1X::read() == 1);
    CHECK(Adc::ADC_CLK_ICLK2X::read() == 1);
    CHECK(Adc::DEC1_BYPS::read() == 0);
    CHECK(Adc::DEC2_BYPS::read() == 0);

    // Capture, released now the windows under it are the new mode's.
    CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);

    // The display PLL's VCO, released by choosing the clock. setResetParameters()
    // and runSyncWatcher() both assert it; held, there is no output clock and
    // the picture tears while every register reads correct.
    CHECK(GBS::PLL_VCORST::read() == 0);
    CHECK(GBS::PLL_IS::read() == 1);

    // The DACs take the scaled video and sync comes from vds_proc. Every other
    // writer of these three is a bypass path, so solving a raster is what has to
    // claim them back -- otherwise leaving bypass reaches the right conclusion
    // and does not act on it, with a scaled raster under bypass routing.
    CHECK(Chip::OUT_SYNC_SEL::read() == 0);
    CHECK(Chip::DAC_RGBS_ADC2DAC::read() == 0);
    CHECK(Chip::DAC_RGBS_BYPS2DAC::read() == 0);

    // The two DAC selects share s0_4b; the head blanking window is 12 bits
    // apiece over s1_24/s1_25 and s1_26/s1_27. s1_02, s3_24 and s2_17 are the
    // 422/444 conversion delays, which follow the scan mode the engine measures.
    // s0_49 is the output sync pad, taken away when the change was detected and
    // given back by the solve that ends it. s5_41/42 and s5_43/44 are the clamp
    // window, which is a fraction of the line and so follows the divider.
    // s5_55 is SP_HS_INV_REG, which normalises the source's hsync polarity so
    // the duty the capture window is placed from is the pulse on every source.
    // s5_56 is SP_HS2PLL_INV_REG, cleared by the same normalisation: the only
    // other writer is the bypass channel, which does not normalise. s5_17 is
    // PLLAD_ICP, which goes in with the group the latch loads.
    CHECK(registersWritten() == 81);
    CHECK(Wire.touched[0][0x49]);   // PAD_SYNC_OUT_ENZ

    // Three of those are the measurement rather than the geometry: timing the
    // field rate selects what the debug pin carries. The sync processor's own
    // stage selector is not among them -- this rate is timed off the input
    // formatter's bus, which does not go through it.
    CHECK(Wire.touched[0][0x4D]);   // TEST_BUS_SEL
    CHECK(Wire.touched[1][0x28]);   // IF_TEST_SEL
    CHECK(Wire.touched[0][0x48]);   // PAD_BOUT_EN
    CHECK_FALSE(Wire.touched[5][0x63]);
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

// poll() runs on every loop() pass, and the steadiness gate wants a few before
// it will pay for a field rate measurement.
static bool pollUntilSolved(VideoSourceAcquisition &acquisition)
{
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        if (pollOnce(acquisition))
            return true;
    return false;
}

// resolveFromSource() installs the reference sampling clock and then measures,
// and nothing read through a clock that has just been latched is the source's
// -- LatchSettlePasses have to be spent first.
static bool resolveUntilSolved(VideoSourceAcquisition &acquisition)
{
    for (uint8_t i = 0;
         i < 2 * (SourceMeasurement::LatchSettlePasses
                  + SourceMeasurement::SteadySamples); ++i)
        if (acquisition.resolveFromSource())
            return true;
    return false;
}

// --- the scenarios, each the sketch's own call sequence ----------------------
//
// Two messages: the sketch says the mode changed, and loop() polls. The ORDER
// inside -- sampling, raster, clock, windows -- is the engine's, and what these
// pin is that it lands on the same chip state whichever way the source behaves
// on the way there.

TEST_CASE("a solve puts all three registers of the one quantity on the chip")
{
    // A divider that moves without the other two leaves the sync processor
    // retiming a line that is not arriving, and the input formatter counting
    // one that is not the length it thinks.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(dividerInForce() == Adc::dividerInForce());
    CHECK(lineCounterInForce() == InputFormatter::lineCounterFor(
                                     Adc::dividerInForce(), engine.lineDoubled()));
    CHECK(retimeStopInForce()
          == SyncProcessor::retimeStopFor(Adc::dividerInForce()));
}

TEST_CASE("a measurement that solved nothing keeps the clock, and never writes a zero")
{
    // Writing a divider of zero stops the ADC clocking the line at all, and
    // every register downstream is then sized for a line that never arrives.
    // What a mode change writes instead is the clock already in force, because
    // the count it is about to take is measured through it.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);

    // Nothing MEASURED has sized a divider, which is what a chip reset leaves:
    // the clock in force is the bring-up one, and no source has been read.
    Adc::applyResetParameters();
    REQUIRE(sampling.lineRateHz() == 0);
    engine.inputTimingsChanged(4);

    CHECK(Adc::PLLAD_MD::read() == Adc::BringUpDivider);
    CHECK(InputFormatter::IF_HSYNC_RST::read()
          == InputFormatter::lineCounterFor(Adc::BringUpDivider,
                                            engine.lineDoubled()));
}

TEST_CASE("a transition blanks the picture and leaves the output sync running")
{
    // The sync pad is what the HDMI encoder locks to, so taking it away costs a
    // full sink re-acquisition -- measured at 3.3 s to 5.6 s of dark panel after
    // the pad comes back, against 0.1 s for a blank the link never sees. A source
    // mode change leaves the raster and the display clock where they are, so
    // there is nothing for the encoder to re-acquire.
    // docs/investigations/the-transition-is-mostly-the-encoder.md
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    engine.showOutput(false);

    CHECK(Chip::PAD_SYNC_OUT_ENZ::read() == 0);
    CHECK(VideoProcessor::VDS_DIS_VB_ST::read()
          == VideoProcessor::VDS_DIS_VB_SP::read() + 1);
}

TEST_CASE("a solve that completes with no change outstanding puts the picture back")
{
    // THE BLANK LIFTS WHEN THE GEOMETRY IS READY, WHICHEVER SOLVE FINISHED IT.
    // A solve runs through the blank state, so one that completes while the
    // picture is hidden writes an aperture that admits nothing -- and if
    // nothing shows the output afterwards the panel stays black with the source
    // acquired, the divider latched and every other register correct. Measured
    // on the bench, twice, on a 320x256@50 -> 640x512@50 leg.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    engine.showOutput(false);
    REQUIRE(VideoProcessor::VDS_DIS_VB_ST::read()
            == VideoProcessor::VDS_DIS_VB_SP::read() + 1);

    REQUIRE(engine.reset());

    CHECK(VideoProcessor::VDS_DIS_VB_ST::read()
          > VideoProcessor::VDS_DIS_VB_SP::read() + 1);
}

TEST_CASE("a settled source is solved on the first poll that can measure it")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    checkBenchGeometry();

    SUBCASE("and nothing is outstanding afterwards") {
        Wire.reset();
        poisonChip();
        CHECK_FALSE(pollOnce(acquisition));

        // The display aperture, and nothing else. A poisoned chip reads as a
        // source that has gone, and blanking the picture is what the engine
        // owes that -- no geometry is re-derived, which is what this is asking.
        // The output sync pad is NOT among them: it reaches the encoder, and
        // the sink's re-acquisition costs seconds the engine never gets back.
        CHECK(registersWritten() == 3);
        CHECK_FALSE(Wire.touched[0][0x49]);   // PAD_SYNC_OUT_ENZ
        CHECK(Wire.touched[3][0x13]);         // VDS_DIS_VB_ST
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
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    g_fieldRate = 0.0f;

    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        CHECK_FALSE(pollOnce(acquisition));

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
        REQUIRE(pollUntilSolved(acquisition));
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
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        CHECK_FALSE(pollOnce(acquisition));

    CHECK_FALSE(Wire.touched[3][0x16]);   // VDS_HSCALE
    CHECK_FALSE(Wire.touched[3][0x01]);   // VDS_HSYNC_RST

    // AND NOTHING IS MEASURED, which is what the refusal has to mean: the
    // divider is sized from the rate, so a rate taken from a count of 97 is
    // what would put the ADC on a clock no source runs.
    CHECK(sampling.lineRateHz() == 0);
}

TEST_CASE("entering bypass leaves nothing to solve")
{
    // In RGBHV bypass the VDS is out of the video path: there is no scaled
    // raster, so a solve must write nothing rather than size a window for one.
    //
    // On a source pass-through actually suits. The engine re-answers that from
    // the measurement on every mode change, so a 15 kHz line-doubled source
    // parked here would correctly leave rather than sit still.
    seedBenchSource();
    seedPassThroughSource();

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    Wire.reset();
    poisonChip();
    // The source has not gone anywhere, and the layer reads it on every pass
    // now: without this the count comes back as whatever the poison implies and
    // the engine correctly leaves a pass-through no source is asking for.
    seedPassThroughSource();
    engine.setOutputMode(&ModeBypass);
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        CHECK_FALSE(pollOnce(acquisition));

    // The output sync, and nothing else: bypass has no solve coming, so giving
    // it back is the whole of ending the change, and a pass-through left
    // without it shows nothing at all.
    CHECK(registersWritten() == 1);
    CHECK(Wire.touched[0][0x49]);   // PAD_SYNC_OUT_ENZ
}

TEST_CASE("a mode with no timings is given up on, not asked about forever")
{
    // The output resolutions with no OutputMode arrive as NULL, and nothing
    // about waiting will produce timings. Every pass that keeps the mode change
    // outstanding pays for a field rate measurement first.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(0);
    engine.inputTimingsChanged(4);
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        CHECK_FALSE(pollOnce(acquisition));

    const unsigned settled = g_fieldRateCalls;
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        CHECK_FALSE(pollOnce(acquisition));
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
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    // Three for a mode change, and no more. One reading has nothing to agree
    // with, so the pass that takes it stops there; the next settles the rate
    // and the sampling clock goes in from it; and the pass after that settle
    // reads the duty through the clock it installed, taking its own reading on
    // the way. Installing before the duty costs exactly that one extra sample.
    const unsigned before = g_fieldRateCalls;
    REQUIRE(pollUntilSolved(acquisition));
    CHECK(g_fieldRateCalls - before == 3);

    SUBCASE("and not at all for a pad press") {
        // A press moves the framing, not the source. Every quantity the windows
        // are solved from is already held, so paying for a vsync sample here
        // buys nothing -- and a refusal would silently drop the press.
        const unsigned solved = g_fieldRateCalls;
        REQUIRE(engine.zoom(16, 0));
        CHECK(g_fieldRateCalls - solved == 0);
    }
}

// --- a reset -----------------------------------------------------------------

TEST_CASE("a reset puts the framing back without re-deriving the rest")
{
    // Without one, a framing zoomed into a corner is only escapable by changing
    // mode or rebooting: the framing is the engine's own state and no register
    // holds it.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    REQUIRE(engine.zoom(400, 120));
    // The framing is a proportion now and carries no unit to compare, so the
    // zoom is witnessed by the window it moved off the default -- at the FAR
    // edge, which is the one a zoom has.
    REQUIRE(InputFormatter::IF_HB_ST2::read() != 1080);
    REQUIRE(InputFormatter::IF_VB_ST::read() != 620);

    // A framing change like any other. The source has not moved and no load has
    // disturbed the ADC, so it lands on the press itself -- no re-arm, no vsync
    // sample, no seconds of frozen capture.
    const unsigned before = g_fieldRateCalls;
    REQUIRE(engine.reset());
    CHECK(g_fieldRateCalls - before == 0);
    CHECK_FALSE(engine.changing());

    CHECK(InputFormatter::IF_HB_SP2::read() == 129);
    CHECK(InputFormatter::IF_HB_ST2::read() == 1080);
    CHECK(InputFormatter::IF_VB_SP::read() == 38);
    CHECK(InputFormatter::IF_VB_ST::read() == 620);

    // And leaves everything the framing does not own exactly as it was. The
    // divider, the raster and the clock are still the ones the mode change
    // solved, not a second answer to the same question.
    checkBenchGeometry();
}


TEST_CASE("a mode change nothing will ever solve does not leave capture frozen")
{
    // Every path that stops the poll has to release it, or the picture is a
    // still frame for the rest of the session with nothing left to unstick it.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    SUBCASE("a mode with no timings") {
        engine.setOutputMode(0);
        engine.inputTimingsChanged(4);
        for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
            CHECK_FALSE(pollOnce(acquisition));
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }

    SUBCASE("and bypass, where there is no solve coming at all") {
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        engine.setOutputMode(&ModeBypass);
        CHECK(FrameBuffer::CAPTURE_ENABLE::read() == 1);
    }
}

// --- the framing belongs to the source ---------------------------------------

TEST_CASE("changing the output keeps the framing the user tuned")
{
    // applyPresets() is the one caller of inputTimingsChanged(), and it runs for a
    // SOURCE mode change and for a user picking a different output resolution.
    // The framing is a proportion of the capturable region, so an output change
    // keeps the user's intent and dropping it makes every output change a
    // re-tune.
    //
    // Within a unit rather than bit-exact: an output too short to show a doubled
    // frame takes the line doubler out, which halves what the IF counts, and the
    // proportion is re-gridded onto the new capture. The intent survives that;
    // the float does not.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    frameAt(engine, 300, 120, 40, -15);
    const PanAndZoom tuned = engine.framing();

    engine.setOutputMode(&Mode480p);
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    const float unit = 1.0f / (float)engine.lineUnitsOn(AxisVertical);
    CHECK_NEAR(engine.framing().originOn(AxisHorizontal),
               tuned.originOn(AxisHorizontal), unit);
    CHECK_NEAR(engine.framing().extentOn(AxisHorizontal),
               tuned.extentOn(AxisHorizontal), unit);
    CHECK_NEAR(engine.framing().originOn(AxisVertical),
               tuned.originOn(AxisVertical), unit);
    CHECK_NEAR(engine.framing().extentOn(AxisVertical),
               tuned.extentOn(AxisVertical), unit);
}

TEST_CASE("a framing tuned on one output resolution is not rewritten by another")
{
    // The framing is a proportion of the capturable INPUT region, so nothing
    // about the output may reach it. The magnification floor is what used to:
    // solved against a wider raster it seeded the framing back at the floor, and
    // a stored framing then meant a different part of the source on every output
    // resolution. A 300 unit capture is below the zoom's own stop on both
    // outputs, which is what makes it the case worth pinning: a framing this
    // narrow arrives from the TABLE, saved under another output, so it is
    // applied rather than pressed for.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(&Mode480p);
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    // Panned clear of the capture floor, which is a DIFFERENT bound and moves
    // with the scan mode: a doubled line's first capturable unit is a larger
    // proportion of it than an undoubled one's, so a framing left against the
    // floor here is legitimately clamped on arrival and would test that instead.
    const uint16_t usable = engine.lineUnitsOn(AxisHorizontal);
    REQUIRE(usable > 300);
    REQUIRE(engine.applyFraming(PanAndZoom(engine.framing().originOn(AxisHorizontal) + 0.03f,
                                           300.0f / (float)usable,
                                           engine.framing().originOn(AxisVertical),
                                           engine.framing().extentOn(AxisVertical))));
    const PanAndZoom tuned = engine.framing();
    REQUIRE(engine.extentUnitsOn(AxisHorizontal) == 300);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    const float unit = 1.0f / (float)engine.lineUnitsOn(AxisHorizontal);
    CHECK_NEAR(engine.framing().extentOn(AxisHorizontal),
               tuned.extentOn(AxisHorizontal), unit);
    CHECK_NEAR(engine.framing().originOn(AxisHorizontal),
               tuned.originOn(AxisHorizontal), unit);
}

TEST_CASE("a source comes back to the framing it was left at")
{
    // The point of the table: tune a source, go somewhere else, come back, and
    // the picture is where it was left with nobody touching a control.
    // docs/framing-presets.md
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    frameAt(engine, 300, 120, 40, -15);
    const PanAndZoom tuned = engine.framing();

    // Away to another source entirely, and back.
    seedSourceLines(524);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    REQUIRE(engine.framing() != tuned);

    seedSourceLines(311);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(engine.framing() == tuned);
}

TEST_CASE("a source nobody has framed takes no place in the table")
{
    // The table holds sixteen. Every solve seeds the framing from the placement
    // it computed, so a table that stored whatever the framing held on the way
    // out would fill with computed defaults and refuse the first real tuning.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    for (uint16_t lines = 311; lines <= 315; ++lines) {
        seedSourceLines(lines);
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));
    }

    CHECK(framings.count() == 0);
}

TEST_CASE("a source nobody has framed gets the computed default")
{
    // With no entry, the default exactly as before: the table adds recall, it
    // does not change what an untuned source looks like.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    const PanAndZoom untouched = engine.framing();

    seedSourceLines(524);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    const PanAndZoom other = engine.framing();

    // Never framed, so coming back gives the same default it gave the first
    // time rather than the other source's framing.
    seedSourceLines(311);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(engine.framing() == untouched);
    CHECK(engine.framing() != other);
}

// Where a stored proportion lands on this line: that fraction of the whole
// line, brought inside the units the capture path can actually open on. The
// proportion itself is untouched by a solve, so only these bounds move it.
static long askedOrigin(const Tv5725::VideoPath &engine,
                        const Tv5725::PanAndZoom &stored, const Tv5725::Axis &axis)
{
    // A framing names a position in the SOURCE and firstUnitOn() is a position
    // in the COUNTER, so the floor has to come back through the lag before the
    // two can be compared.
    const long first = (long)engine.firstUnitOn(axis) - engine.videoLagOn(axis);
    const long asked = lrintf(stored.originOn(axis) * (float)engine.lineUnitsOn(axis));
    return asked < first ? first : asked;
}

static long askedExtent(const Tv5725::VideoPath &engine,
                        const Tv5725::PanAndZoom &stored, const Tv5725::Axis &axis)
{
    const long origin = askedOrigin(engine, stored, axis);
    const long asked = lrintf(stored.extentOn(axis) * (float)engine.lineUnitsOn(axis));
    const long room = (long)engine.reachOn(axis) - origin;
    return asked > room ? room : asked;
}

TEST_CASE("a framing restored from the file is applied when its source arrives")
{
    // Boot: the file is read before anything has been measured, so the entry
    // goes in against a key nothing has seen yet and has to be found when the
    // source turns up.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    const PanAndZoom stored(0.10f, 0.60f, 0.15f, 0.55f);
    REQUIRE(framings.remember(SourceKey(311, 50.08f, BenchSyncWidth, BenchVsyncPositive), stored));

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    // The framing is a proportion of the LINE, so the units it lands on are
    // that proportion of this line, clamped into what the capture path can
    // open on. docs/framing-presets.md
    for (int vertical = 0; vertical < 2; ++vertical) {
        const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
        CHECK(engine.originUnitsOn(axis) == askedOrigin(engine, stored, axis));
        CHECK(engine.extentUnitsOn(axis) == askedExtent(engine, stored, axis));
    }
}

TEST_CASE("a press stores the framing without leaving the source")
{
    // Storing only on the way out loses every tuning of a unit that is turned
    // off where it is used -- which is all of them. The in-memory table is free
    // to follow each press; it is the FLASH write that has to be debounced.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    REQUIRE(framings.count() == 0);

    frameAt(engine, 300, 120, 40, -15);

    PanAndZoom stored;
    REQUIRE(framings.find(SourceKey(311, 50.08f, BenchSyncWidth, BenchVsyncPositive), &stored));
    CHECK(stored == engine.framing());
}

TEST_CASE("a reset forgets what the table stored for this source")
{
    // Otherwise the solve that follows the reset finds the entry and restores
    // exactly what was just discarded, and the control does nothing at all.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    const PanAndZoom untouched = engine.framing();

    frameAt(engine, 300, 120, 40, -15);
    REQUIRE(framings.count() == 1);

    REQUIRE(engine.reset());

    CHECK(framings.count() == 0);
    CHECK(engine.framing() == untouched);
}

TEST_CASE("the table says when it has something new to write")
{
    // A pad press must not write flash, so the sketch debounces -- and it needs
    // to know whether a write is owed at all, or every quiet tick costs one.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    const uint16_t settled = framings.revision();

    SUBCASE("a solve that stores nothing leaves it alone") {
        REQUIRE(engine.resolve());
        CHECK(framings.revision() == settled);
    }

    SUBCASE("and a source change that stores a tuning moves it") {
        frameAt(engine, 300, 120, 40, -15);
        seedSourceLines(524);
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));

        CHECK(framings.revision() != settled);
    }

    SUBCASE("but a source change with nothing tuned does not") {
        seedSourceLines(524);
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));

        CHECK(framings.revision() == settled);
    }
}

// --- every window follows the framing ----------------------------------------

TEST_CASE("a framed picture holds every window against the framing")
{
    // Every window is recomputed on every solve, pan included. Inheriting one
    // freezes the picture at the previous mode's size.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    // What the solve placed before anything was framed. Held rather than
    // written down, because the placement follows the source and a constant
    // here would only track whatever the default happens to be.
    const long captureStart = InputFormatter::IF_HB_SP2::read();
    const long captureStop = InputFormatter::IF_HB_ST2::read();
    const long linesStart = InputFormatter::IF_VB_SP::read();
    const long linesStop = InputFormatter::IF_VB_ST::read();
    const long wideScale = VideoProcessor::VDS_HSCALE::read();
    const long tallScale = VideoProcessor::VDS_VSCALE::read();

    frameAt(engine, 300, 120, 40, -15);
    Wire.reset();
    poisonChip();
    seedSourceMeasurement();
    REQUIRE(engine.resolve());

    // The capture narrows 300 units horizontally and 120 vertically, then moves
    // 40 right and 15 up. The near edge carries the pan alone and the far edge
    // carries both, because the zoom takes its units off the far end.
    CHECK(InputFormatter::IF_HB_SP2::read() == captureStart + 40);
    CHECK(InputFormatter::IF_HB_ST2::read() == captureStop + 40 - 300);
    CHECK(InputFormatter::IF_VB_SP::read() == linesStart - 15);
    CHECK(InputFormatter::IF_VB_ST::read() == linesStop - 15 - 120);
    CHECK(InputFormatter::IF_LINE_ST::read() == 64);
    CHECK(InputFormatter::IF_LINE_SP::read() == 1165);

    // Both scales rise to magnify the smaller capture onto the same raster.
    CHECK(VideoProcessor::VDS_HSCALE::read() < wideScale);
    CHECK(VideoProcessor::VDS_VSCALE::read() < tallScale);
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
    // The picture still spans the raster it is magnified onto, whatever the
    // framing cropped: the far edges sit on the same porches the unframed solve
    // put them on.
    CHECK(VideoProcessor::VDS_HB_ST::read() > 1800);
    CHECK(VideoProcessor::VDS_VB_ST::read() > 1100);
    // The near edge follows the output mode's back porch less the write origin,
    // rather than resting on the floor of 8.
    CHECK(VideoProcessor::VDS_HB_SP::read() == 21);
    CHECK(VideoProcessor::VDS_VB_SP::read() > 0);
    CHECK(VideoProcessor::VDS_DIS_HB_SP::read() > VideoProcessor::VDS_HB_SP::read());
    CHECK(VideoProcessor::VDS_DIS_VB_SP::read() > VideoProcessor::VDS_VB_SP::read());

    CHECK(FrameBuffer::PB_CAP_OFFSET::read() == 276);

    // The raster did not change, so its registers are not rewritten -- and
    // neither is the sampling group, which was installed before the duty was
    // read rather than by the solve.
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
    poisonChip();
    seedField(3, 0x01, 0, 12, 1278);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1124);   // IF_HSYNC_RST, capture wrap - 1
    Wire.lockSyncProcessor();
    seedField(0, 0x19, 0, 12, 84);     // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 1);       // STATUS_SYNC_PROC_HSPOL
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    seedField(5, 0x12, 0, 12, 1124);   // PLLAD_MD
    seedField(0, 0x1B, 0, 11, 499);    // STATUS_SYNC_PROC_VTOTAL
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 75.0f;

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    // The window stays inside the 500 lines the counter reaches, and the scale
    // is sized for the capture that arrives. Doubling the frame put the stop at
    // 994, which the counter never reaches, and left the scale magnifying a
    // capture twice the real one -- so both halves are checked against the
    // frame rather than against a constant.
    const long start = InputFormatter::IF_VB_SP::read();
    const long stop = InputFormatter::IF_VB_ST::read();
    CHECK(stop > start);
    CHECK(stop < 500);

    const long produced = (stop - start) * 1024 / VideoProcessor::VDS_VSCALE::read();
    // 1080p60 states 1080 active lines of its 1125, and the window opens on that
    // porch, so the picture fills 1080 rather than the 1118 an unbounded window
    // reached.
    CHECK(produced > 1070);
    CHECK(produced <= 1126);
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
    poisonChip();
    seedField(3, 0x01, 0, 12, 1278);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1124);   // IF_HSYNC_RST, capture wrap - 1
    Wire.lockSyncProcessor();
    seedField(0, 0x19, 0, 12, 129);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 1);       // STATUS_SYNC_PROC_HSPOL
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    seedField(0, 0x1B, 0, 11, 524);    // STATUS_SYNC_PROC_VTOTAL
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 60.0f;

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    REQUIRE(Adc::PLLAD_MD::read() == RasterDivider);

    // The source changes down. The sketch reloads a preset for the new
    // standard, so the engine is told the mode changed -- but the divider it is
    // holding is the one that made the count unmeasurable.
    //
    // The sample count is read against the divider in force, so it is seeded
    // from that rather than from a literal: a count that no longer sits within
    // tolerance of a whole multiple of it is not a locked-to-every-other-hsync
    // source at all, and the case then measures something else.
    const uint32_t held = Adc::PLLAD_MD::read();
    Wire.lockSyncProcessor(false);
    seedField(0, 0x1B, 0, 11, 155);           // one line counted per two sent
    seedField(0, 0x17, 0, 12, 2 * held - 1);  // and twice the samples per line,
    seedField(0, 0x19, 0, 12, 181);           // which is evidence of the multiple
    g_fieldRate = 50.08f;
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    // The escape is the CORRECTION, applied against the divider the count was
    // read through: 155 counted where the samples per line are twice the
    // divider is 310 lines, which is a source, so the rate is measured while
    // the divider is still the one that caused the miscount. Sizing the clock
    // needs a rate, so a count taken at face value here is what would have no
    // exit.
    for (uint8_t i = 0;
         i < 2 * SourceMeasurement::LatchSettlePasses
                 + SourceMeasurement::SteadySamples + 1;
         ++i)
        CHECK_FALSE(pollOnce(acquisition));
    CHECK(sampling.sourceLines() == 310);
    CHECK(sampling.lineRateHz() > 15000);
    CHECK(sampling.lineRateHz() < 16000);

    // And the divider that caused it is on its way out: the clock is installed
    // from the rate just measured, and it is the doubled source's, not the
    // progressive mode's this arrived holding.
    CHECK(Adc::PLLAD_MD::read() > (uint32_t)RasterDivider);
    CHECK(Adc::PLLAD_LAT::read() == 1);

    SUBCASE("and the source it was blind to is then solved for") {
        // With the divider latched the PLL locks to every hsync, so the sync
        // processor counts the source as it really is.
        Wire.lockSyncProcessor();
        seedField(0, 0x1B, 0, 11, 311);
        seedField(0, 0x19, 0, 12, 181);
        REQUIRE(pollUntilSolved(acquisition));
        CHECK(sampling.sourceLines() == 311);
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

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    for (uint8_t i = 0;
         i < 4 * (SourceMeasurement::SteadySamples
                  + SourceMeasurement::LatchSettlePasses); ++i)
        pollOnce(acquisition);

    CHECK(InputFormatter::IF_HS_DEC_FACTOR::read() == 1);
    CHECK(InputFormatter::IF_LD_SEL_PROV::read() == 0);
    CHECK(InputFormatter::IF_LD_RAM_BYPS::read() == 0);
    CHECK(InputFormatter::IF_PRGRSV_CNTRL::read() == 0);
}

TEST_CASE("bypass keeps the line rate it last measured")
{
    // Bypass does not measure, so the held rate is the one from the mode that
    // preceded it -- which is exactly the fact the caller wants. Whether the
    // source is a 15 kHz line decides whether the display can show it at all,
    // and that question is asked while bypassed.
    //
    // Discarding it here did not remove the stale fact, it moved it: the sketch
    // then read videoStandardInput instead, which carries the same measurement
    // from the same moment and cannot say what the rate was.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    REQUIRE(sampling.lowLineRate());
    const uint32_t measured = sampling.lineRateHz();
    REQUIRE(measured != 0);

    engine.setOutputMode(&ModeBypass);
    CHECK(sampling.lineRateHz() == measured);
    CHECK(sampling.lowLineRate());
}

TEST_CASE("the hsync duty is counted against the divider the source is left on")
{
    // The duty is a ratio of the pulse to the LINE, and the line it is counted
    // in is the divider in force -- so a duty taken through one divider and
    // used to place a window sized in another's units is out by the ratio
    // between them.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    // The SHARE the source spends on sync, which is what the fixture models and
    // what a key carries. The count the sync processor reports moves with the
    // divider and the share does not.
    CHECK(sampling.hsync().syncDuty()
          == doctest::Approx(BenchSyncWidth).epsilon(0.001));
}

TEST_CASE("an output change re-derives the divider even where the doubling holds")
{
    // The divider is bounded by the CAPTURE THE RASTER CAN SHOW, and the raster
    // is the output's -- so two outputs that agree about the doubling still want
    // different dividers. 480p affords 1876 units and 576p 1952, both undoubled
    // from a 311 line source.
    //
    // Measured on the bench, both directions: switching between the two left
    // PLLAD_MD on whichever was arrived from, with VDS_HSCALE stranded to match.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(&Mode480p);
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    REQUIRE(dividerInForce() == 1804);

    // The output alone. Nothing tells the engine the source moved, because it
    // has not -- which is the whole of what /uc?<key> does.
    engine.setOutputMode(&Mode576p);
    for (uint8_t i = 0; i < SourceMeasurement::SteadySamples; ++i)
        pollOnce(acquisition);

    CHECK(dividerInForce() == 1852);
}

TEST_CASE("the source is measured through a known divider, not the last mode's")
{
    // The field rate is timed at DEBUG_IN_PIN off the input formatter's test
    // bus, and IF_HSYNC_RST comes from the divider -- so a divider left over
    // from the previous mode corrupts the reading that would correct it, and
    // the state is self-latching. Measured on the bench at 640x480: 171.53 and
    // 184.60 Hz against a real 60, every reading refused, no divider chosen.
    // docs/investigations/field-rate-measured-downstream.md
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    SUBCASE("the divider the previous mode left is not what it is sampled through") {
        // 1124 is what a 524-line source solves to, and it is the value the
        // bench sticks on when a return to 311 lines cannot measure.
        seedField(5, 0x12, 0, 12, 1124);
        g_dividerWhenSampled = 0;
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));
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
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    SUBCASE("a frame that shrank is not measured through the taller mode's window") {
        seedField(0, 0x1B, 0, 11, 524);   // STATUS_SYNC_PROC_VTOTAL
        g_fieldRate = 60.0f;
        seedField(1, 0x1C, 0, 11, 578);   // IF_VB_ST, solved for 311 doubled

        g_blankStartWhenSampled = 0xFFFF;
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));
        CHECK(g_blankStartWhenSampled < 524);
    }

    SUBCASE("the line-doubled frame is measured through a window inside it too") {
        seedField(1, 0x1C, 0, 11, 700);   // beyond even the doubled 622

        g_blankStartWhenSampled = 0xFFFF;
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));
        CHECK(g_blankStartWhenSampled < 2 * 311);
    }

    SUBCASE("a re-solve on an unchanged source parks it too") {
        // The reference divider is a function of the scan mode alone, so a
        // source that did not move asks for the one already in force. Keying
        // the parking on the reference having CHANGED therefore never fires
        // here -- and this is the case that matters, because a window is not
        // only stranded by a mode change.
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));

        seedField(1, 0x1C, 0, 11, 700);
        g_blankStartWhenSampled = 0xFFFF;
        REQUIRE(resolveUntilSolved(acquisition));
        CHECK(g_blankStartWhenSampled < 2 * 311);
    }
}

TEST_CASE("a field rate that jitters across a divider step installs one clock")
{
    // Every install re-latches the ADC PLL and restarts the settle, and the
    // duty is read after that settle -- so a clock re-installed on every pass
    // leaves a source that never finishes measuring. The divider is quantised,
    // so a field rate wandering by hundredths lands either side of a step.
    //
    // Measured on the bench: 50.08 Hz and 50.05 Hz alternating chose 2506 and
    // 2508, the engine oscillated between them for as long as it was left, and
    // every duty reported UNLOCKED with the picture dark.
    seedBenchSource();

    // A processor that never echoes the divider, so the duty is refused and the
    // mode change stays open -- which is the state the clock is asked for on
    // every pass, and the one the oscillation was measured in.
    Wire.lockSyncProcessor(false);
    seedField(0, 0x17, 0, 12, 3252);

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        pollOnce(acquisition);

    Wire.trace.clear();
    for (int step = 0; step < 24; ++step) {
        g_fieldRate = (step % 2) ? 49.95f : 50.20f;
        pollOnce(acquisition);
    }

    unsigned installs = 0;
    for (size_t i = 0; i < Wire.trace.size(); ++i)
        if (Wire.trace[i].segment == 5 && Wire.trace[i].reg == 0x12)
            ++installs;
    CHECK(installs == 0);
}

TEST_CASE("a source is acquired from the state a chip reset leaves")
{
    // THE RESET STATE IS A CLOCK, and this is what turns on it. The divider is
    // sized from the rate and the rate is measured through the divider, so a
    // reset that left nothing in force left nothing able to measure its way
    // out: measured on the bench, the recovery ladder cycled every 7 s
    // indefinitely with the picture dark, the sync processor reading 166 lines
    // of 3829 samples against a parked 1792.
    // ../docs/investigations/the-reference-divider-was-the-bootstrap.md
    seedBenchSource();
    Adc::applyResetParameters();

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    for (uint8_t i = 0;
         i < 2 * SourceMeasurement::LatchSettlePasses
                 + SourceMeasurement::SteadySamples + 1;
         ++i)
        pollOnce(acquisition);

    CHECK(sampling.sourceLines() == 311);
    CHECK(Adc::PLLAD_MD::read() == Adc::dividerInForce());
}

TEST_CASE("a divider from another mode does not stop the source being counted")
{
    // The line count is a MEASUREMENT, taken by the sync processor in ADC
    // clocks, so a divider far from the source's line puts the PLL outside its
    // lock range and it locks to every kth hsync instead. Measured on the
    // bench: a 311-line source read 155 with the divider left at another mode's
    // 1124, held indefinitely. The count is corrected against the divider it
    // was taken through, which is what gives the state an exit -- the divider
    // is sized from the rate, so a count read raw here would leave nothing able
    // to choose one.
    seedBenchSource();
    Adc::applyDivider(1124);           // a progressive mode's, left in force

    // The processor counting the source's real line rather than the divider,
    // which is what locking to every other hsync looks like from here: half the
    // count, and twice the samples per line as the evidence of the multiple.
    Wire.lockSyncProcessor(false);
    seedField(0, 0x1B, 0, 11, 155);
    seedField(0, 0x17, 0, 12, 2 * 1124);

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);

    for (uint8_t i = 0;
         i < 2 * SourceMeasurement::LatchSettlePasses
                 + SourceMeasurement::SteadySamples + 1;
         ++i)
        pollOnce(acquisition);

    // The count the correction recovered, and a divider sized from the rate it
    // gave -- both while the chip was still clocking the other mode's line.
    CHECK(sampling.sourceLines() == 310);
    CHECK(sampling.lineRateHz() > 15000);
    CHECK(Adc::PLLAD_MD::read() != 1124);

    SUBCASE("and the source is solved for once the processor follows it") {
        Wire.lockSyncProcessor();
        seedField(0, 0x1B, 0, 11, 311);
        seedField(0, 0x19, 0, 12, 181);
        REQUIRE(pollUntilSolved(acquisition));
        CHECK(sampling.sourceLines() == 311);
    }
}

TEST_CASE("a framing applied whole lands as the window it describes")
{
    // What loading a slot does: the stored proportions become the live framing
    // and every register is re-solved from them, rather than a saved framing
    // being replayed as registers. docs/framing-presets.md
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    const PanAndZoom stored(0.10f, 0.60f, 0.15f, 0.55f);
    REQUIRE(engine.applyFraming(stored));

    for (int vertical = 0; vertical < 2; ++vertical) {
        const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
        CAPTURE(vertical);
        CHECK(engine.originUnitsOn(axis) == askedOrigin(engine, stored, axis));
        CHECK(engine.extentUnitsOn(axis) == askedExtent(engine, stored, axis));
    }

    SUBCASE("and the source is left framed that way for next time") {
        PanAndZoom remembered;
        REQUIRE(framings.find(SourceKey(311, 50.08f, BenchSyncWidth, BenchVsyncPositive), &remembered));
        CHECK(remembered == engine.framing());
    }
}

TEST_CASE("the engine says which source the framing it holds is against")
{
    // What a slot records alongside the framing, so restoring one into a
    // different source can be refused. docs/framing-presets.md
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    CHECK_FALSE(engine.framedKey().valid());

    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(engine.framedKey() == SourceKey(311, 50.08f, BenchSyncWidth, BenchVsyncPositive));
}


// --- the sync type is a property of the SOURCE, re-established per change -----
//
// It cannot be read back, so it is probed by moving the sync path and watching
// for V. The source can change it without changing the mux -- a RISC PC sets it
// from CMOS, so any mode change may carry a new one -- which is why this runs on
// every change rather than once per input. It costs a settle plus a window, and
// that hides behind the blank a mode change already holds.

static unsigned g_probeCalls = 0;
static bool g_hasOwnVsync = true;
static bool probeOwnVsync() { ++g_probeCalls; return g_hasOwnVsync; }

TEST_CASE("a mode change establishes the sync type before it measures anything")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.useSyncTypeProbe(probeOwnVsync);
    g_probeCalls = 0;

    SUBCASE("a source with no vsync of its own is composite sync") {
        g_hasOwnVsync = false;
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));

        CHECK(SyncMeasurement::isCsync());
        CHECK(SyncProcessor::SP_SOG_MODE::read() == 1);
        CHECK(SyncProcessor::SP_EXT_SYNC_SEL::read() == 1);
        CHECK(ModeDetect::MD_SEL_VGA60::read() == 0);
    }

    SUBCASE("a source bringing its own vsync is separate H/V") {
        g_hasOwnVsync = true;
        engine.setOutputMode(benchMode());
        engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(acquisition));

        CHECK_FALSE(SyncMeasurement::isCsync());
        CHECK(SyncProcessor::SP_SOG_MODE::read() == 0);
        CHECK(SyncProcessor::SP_EXT_SYNC_SEL::read() == 0);
        CHECK(ModeDetect::MD_SEL_VGA60::read() == 1);
    }
}

TEST_CASE("the sync type is probed once, not once per mode change or per poll")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    g_probeCalls = 0;
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    CHECK(g_probeCalls == 1);

    // Settled, so nothing further asks: the probe moves the sync path and costs
    // a settle plus a window, which is not something a poll may do.
    for (uint8_t i = 0; i < 6; ++i)
        pollOnce(acquisition);
    CHECK(g_probeCalls == 1);

    // NOR DOES A MODE CHANGE ASK AGAIN. How a source carries sync is a property
    // of the source and not of the mode it is in, and measuring costs the path
    // settle plus the probe's window -- a composite source has no V to arrive,
    // so it spends the whole window every time. The evidence that the held
    // answer is wrong is a count no source runs, which arms its own re-probe.
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    CHECK(g_probeCalls == 1);
}

// --- what the engine can say about the source, which is three answers --------
//
// A steadiness run over the line count answers the VERTICAL question alone. A
// source can hold a correct, steady count while the ADC samples a line it is
// not locked to, and there the sketch's escalation is exactly what is needed --
// so a recovery withheld on the count alone leaves the source stuck. Measured:
// after a sync-type round trip, STATUS_SYNC_PROC_VTOTAL 311 held for a minute
// while STATUS_SYNC_PROC_HTOTAL read near 3250 against a divider of 2250, the
// ADC PLL out of lock and the picture scrambled with every config register
// correct.


TEST_CASE("reacquiring the sync type puts the registers on the answered path")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    REQUIRE_FALSE(SyncMeasurement::isCsync());

    SyncProcessor::SP_SOG_MODE::write(1);

    engine.reacquireSyncType();

    CHECK(SyncProcessor::SP_SOG_MODE::read() == 0);
}

TEST_CASE("reacquiring the sync type asks the probe again")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));
    g_probeCalls = 0;

    engine.reacquireSyncType();

    CHECK(g_probeCalls == 1);
}

TEST_CASE("reacquiring the sync type reports what the source carries")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.useSyncTypeProbe(probeOwnVsync);

    g_hasOwnVsync = true;
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK_FALSE(engine.reacquireSyncType());

    g_hasOwnVsync = false;
    CHECK(engine.reacquireSyncType());
}

// --- what a mode change looks like when the line count cannot show it ---------
//
// sourceMoved() is the only thing that arms a solve while the engine is idle,
// and it had one input: the line count. Two sources move underneath it.

TEST_CASE("a source whose serrations are counted as lines is coasted further")
{
    // Refusing to solve is not enough on its own. Nothing would change, so a
    // source whose vertical interval the default pair does not cover would
    // never come up at all.
    // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
    seedBenchSource();
    seedSourceLines(607);
    seedSourceHalfLines(624);
    SyncProcessor::applyForSyncType(true);
    const uint32_t before = SyncProcessor::SP_PRE_COAST::read();

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    pollUntilSolved(acquisition);

    CHECK(SyncProcessor::SP_PRE_COAST::read() > before);
}

TEST_CASE("a source that measures its own lines is left on the pair it has")
{
    seedBenchSource();
    seedSourceHalfLines(624);
    SyncProcessor::applyForSyncType(true);
    const uint32_t before = SyncProcessor::SP_PRE_COAST::read();

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(SyncProcessor::SP_PRE_COAST::read() == before);
}

TEST_CASE("re-solving every register measures nothing")
{
    // The re-derive command re-solves from what is HELD. Measuring is the
    // acquisition layer's, and it costs up to 250 ms a vsync pulse, so an engine
    // that reached for it had a second place the source was read from and a
    // second answer to disagree with. docs/video-source-acquisition.md
    seedBenchSource();

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    g_fieldRateCalls = 0;

    REQUIRE(engine.resolve());

    CHECK(g_fieldRateCalls == 0);
}

TEST_CASE("a framing press solves from the reading handed in, not from the chip")
{
    // The engine is PASSED what the source measures; the layer that measures is
    // the only thing that reads the chip. So a press re-solves from what is
    // held, and a sync-processor register moving under it -- which is what a
    // source event exists to catch -- does not reach the windows until a
    // measurement does. docs/video-source-acquisition.md
    seedBenchSource();

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    const uint16_t capturable = engine.lineUnitsOn(AxisHorizontal);
    REQUIRE(capturable > 0);

    // A sync low nothing measured, three times what the source runs.
    seedField(0, 0x19, 0, 12, 543);    // STATUS_SYNC_PROC_HLOW_LEN

    REQUIRE(engine.pan(-16, 0));

    CHECK(engine.lineUnitsOn(AxisHorizontal) == capturable);
}

TEST_CASE("an undoubled source is sampled up to the measured ceiling")
{
    // 800x600@60: 628 lines at 60 Hz. Undoubled, one IF unit is one ADC sample,
    // so the eleven-bit line counter is what bounds the divider rather than any
    // crossover row -- 2006 samples, which is 1.9 per source pixel.
    Wire.reset();
    poisonChip();
    seedField(3, 0x01, 0, 12, 1915);   // VDS_HSYNC_RST, output line - 1
    seedField(3, 0x02, 4, 11, 1124);   // VDS_VSYNC_RST, output frame - 1
    seedField(1, 0x0E, 0, 11, 1124);   // IF_HSYNC_RST, capture wrap - 1
    Wire.lockSyncProcessor();
    seedField(0, 0x19, 0, 12, 137);    // STATUS_SYNC_PROC_HLOW_LEN
    seedField(0, 0x16, 0, 1, 1);       // STATUS_SYNC_PROC_HSPOL
    seedField(0, 0x16, 3, 1, 1);       // STATUS_SYNC_PROC_VSACT, V on its own pin
    seedField(0, 0x1B, 0, 11, 627);    // STATUS_SYNC_PROC_VTOTAL
    seedField(4, 0x21, 0, 1, 1);       // CAPTURE_ENABLE, running
    g_fieldRate = 60.0f;

    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);
    engine.setOutputMode(benchMode());
    engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(acquisition));

    CHECK(Adc::PLLAD_MD::read() == RasterDivider);

    SUBCASE("the IF line counter follows it, undoubled") {
        CHECK(InputFormatter::IF_HSYNC_RST::read() == RasterDivider);
        CHECK(InputFormatter::IF_HSYNC_RST::read()
              <= InputFormatter::LineCounterMax);
    }
}
