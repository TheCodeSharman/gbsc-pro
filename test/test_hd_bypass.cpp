// Host-compiled unit tests for src/tv5725/HdBypass.cpp
// -- `make -C test hd-bypass`.
//
// Same fake-Wire seam as test_input_formatter.cpp: poison every bank, run
// init(), and ask the fake what was touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <Arduino.h>

#include "fake/Wire.h"

// The two the linked classes need from the sketch, plus the RGB patches the
// ladder is handed -- counted, so standard 13's coverage can assert them.
#include "DebugPinStub.h"
#include "MeasuredSource.h"

// Driveable, because the bypass decisions below are taken against a measured
// source and the rate is half of what they weigh.
static float g_fieldRate = 50.0f;
uint32_t debugPinPulseTicks() { return ticksForHz(g_fieldRate); }
void tv5725Log(const char *) {}

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourSpace.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ModeDetect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"

using Tv5725::ColourSpace;
using Tv5725::HdBypass;
using Tv5725::SourceMeasurement;

// The source's line count as the sync processor reports it.
static void seedSourceLines(uint16_t lines)
{
    Wire.reset();
    Wire.bank[0][0x1B] = (uint8_t)(lines & 0xFF);
    Wire.bank[0][0x1C] = (uint8_t)((lines >> 8) & 0x07);
}

// Neither a gain of 128 nor an offset of 0, so a field left at the poison is
// reported rather than mistaken for a write.
static const uint8_t Poison = 0xA5;

// init() is the OFF state and enable() is the on state, so every assertion
// about the block's registers runs against enable().
struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        HdBypass::enable();
    }
};

TEST_CASE("the bring-up state holds the block in reset and programs nothing")
{
    // Scaling does not go through the HD bypass block, so its off state is its
    // reset asserted -- and configuring a block held in reset is what the old
    // arrangement did, because the reset bit belonged to Chip and the registers
    // to nobody.
    Wire.reset();
    Wire.poison(Poison);
    HdBypass::init();

    CHECK(Wire.field(0, 0x47, 3, 1) == 0);  // SFTRST_HDBYPS_RSTZ, held

    for (int r = 0x30; r <= 0x5F; ++r) {
        CAPTURE(r);
        CHECK_FALSE(Wire.touched[1][r]);
    }
}

TEST_CASE("holding the reset is what the bring-up state is")
{
    Wire.reset();
    Wire.poison(Poison);
    HdBypass::enable();
    CHECK(HdBypass::enabled());

    HdBypass::hold();

    CHECK(Wire.field(0, 0x47, 3, 1) == 0);
    CHECK_FALSE(HdBypass::enabled());
}

TEST_CASE("enabled() reports the block, not what anyone remembers writing")
{
    // resetDigital() clears the whole of s0_47 and puts back what it found, so
    // it has to ask the block. Nothing else can answer: rto->outModeHdBypass is
    // the sketch's intent, and the two disagree while a reset is in progress.
    Wire.reset();
    Wire.poison(Poison);

    HdBypass::hold();
    CHECK_FALSE(HdBypass::enabled());

    HdBypass::enable();
    CHECK(HdBypass::enabled());
}

TEST_CASE("enabling the block releases its reset before configuring it")
{
    // A reset released AFTER its block is configured discards the
    // configuration, which is the rule BringUp.h states for Chip::init() and
    // the same one applies here.
    Wire.reset();
    Wire.poison(Poison);
    Wire.trace.clear();
    HdBypass::enable();

    size_t released = Wire.trace.size();
    size_t firstConfig = Wire.trace.size();
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        const FakeTwoWire::Traced &t = Wire.trace[i];
        if (t.reg == FakeTwoWire::SegmentRegister)
            continue;
        if (t.segment == 0 && t.reg == 0x47)
            released = i;
        if (t.segment == 1 && t.reg >= 0x30 && i < firstConfig)
            firstConfig = i;
    }

    CHECK(Wire.field(0, 0x47, 3, 1) == 1);  // SFTRST_HDBYPS_RSTZ, released
    CHECK(released < firstConfig);
}

TEST_CASE("the input pipe is in circuit")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x30, 0, 1) == 0);  // HD_IN_DREG_BYPS
    CHECK(Wire.field(1, 0x30, 3, 1) == 0);  // HD_SEL_BLK_IN
}

// Which colour path the bypassed sample takes. The one thing bypass has to know
// about the source, and the input selection is where it is known: a component
// input needs the matrix, an RGB one needs it out of the way.

TEST_CASE("a component input keeps the matrix in circuit")
{
    Wire.reset();

    HdBypass::applyColourPath(true);

    CHECK(HdBypass::HD_MATRIX_BYPS::read() == 0);
    CHECK(HdBypass::HD_DYN_BYPS::read() == 0);
    CHECK(ColourSpace::DEC_MATRIX_BYPS::read() == 1);
}

TEST_CASE("an RGB input takes every matrix out of circuit")
{
    Wire.reset();

    HdBypass::applyColourPath(false);

    CHECK(HdBypass::HD_MATRIX_BYPS::read() == 1);
    CHECK(HdBypass::HD_DYN_BYPS::read() == 1);
    CHECK(ColourSpace::DEC_MATRIX_BYPS::read() == 1);
}

TEST_CASE("the decimator's matrix is out of circuit on either input")
{
    // It is the SCALING path's converter, and bypass does not go through it,
    // so both answers leave it bypassed and only the HD block's pair moves.
    Wire.reset();
    HdBypass::applyColourPath(true);
    const uint32_t component = ColourSpace::DEC_MATRIX_BYPS::read();

    Wire.reset();
    HdBypass::applyColourPath(false);

    CHECK(component == ColourSpace::DEC_MATRIX_BYPS::read());
}

TEST_CASE("the dynamic range passes the sample through unchanged")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x31, 0, 8) == 128);  // HD_Y_GAIN
    CHECK(Wire.field(1, 0x32, 0, 8) == 0);    // HD_Y_OFFSET
    CHECK(Wire.field(1, 0x33, 0, 8) == 128);  // HD_U_GAIN
    CHECK(Wire.field(1, 0x34, 0, 8) == 0);    // HD_U_OFFSET
    CHECK(Wire.field(1, 0x35, 0, 8) == 128);  // HD_V_GAIN
    CHECK(Wire.field(1, 0x36, 0, 8) == 0);    // HD_V_OFFSET
}

TEST_CASE("the bypass raster comes up at the resting timing")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x37, 0, 11) == 1023);  // HD_HSYNC_RST
    CHECK(Wire.field(1, 0x39, 0, 11) == 0);     // HD_INI_ST
    CHECK(Wire.field(1, 0x3B, 0, 12) == 3976);  // HD_HB_ST
    CHECK(Wire.field(1, 0x3D, 0, 12) == 208);   // HD_HB_SP
    CHECK(Wire.field(1, 0x3F, 0, 12) == 0);     // HD_HS_ST
    CHECK(Wire.field(1, 0x41, 0, 12) == 124);   // HD_HS_SP
    CHECK(Wire.field(1, 0x43, 0, 12) == 0);     // HD_VB_ST
    CHECK(Wire.field(1, 0x45, 0, 12) == 20);    // HD_VB_SP
    CHECK(Wire.field(1, 0x47, 0, 12) == 2);     // HD_VS_ST
    CHECK(Wire.field(1, 0x49, 0, 12) == 7);     // HD_VS_SP
}

TEST_CASE("the DVI-mode blanking is owned here and nowhere else")
{
    // No bypass switch and no runtime path writes these four, so init() is
    // their only writer.
    FreshChip chip;

    CHECK(Wire.field(1, 0x4B, 0, 12) == 0);  // HD_EXT_VB_ST
    CHECK(Wire.field(1, 0x4D, 0, 12) == 6);  // HD_EXT_VB_SP
    CHECK(Wire.field(1, 0x4F, 0, 12) == 0);  // HD_EXT_HB_ST
    CHECK(Wire.field(1, 0x51, 0, 12) == 6);  // HD_EXT_HB_SP
}

TEST_CASE("the programmed blank is black on all three channels")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x53, 0, 8) == 0);  // HD_BLK_GY_DATA
    CHECK(Wire.field(1, 0x54, 0, 8) == 0);  // HD_BLK_BU_DATA
    CHECK(Wire.field(1, 0x55, 0, 8) == 0);  // HD_BLK_RV_DATA
}

TEST_CASE("the HD bypass block stays inside segment 1, bar its own reset")
{
    // s0_47 is the one exception and it is deliberate: the block's reset bit
    // belongs to the block, so no other class has to know this one exists.
    FreshChip chip;

    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        for (int r = 0; r < 256; ++r) {
            if (s == 1 || (s == 0 && r == 0x47))
                continue;
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}

TEST_CASE("the block writes the addresses it owns and no others")
{
    // s1_56..s1_5f carry no datasheet field. The blob wrote them 0x00 along
    // with the rest of its three banks; an address with no documented meaning
    // gets no writer, per docs/chip-initialisation.md.
    FreshChip chip;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[1][r] == (r >= 0x30 && r <= 0x55));
    }
}

TEST_CASE("the reset can be cycled without reloading the configuration")
{
    // resetDigital() holds every block's reset and puts back the ones it found
    // released. The bypass switches program the raster, both sync windows and
    // the RGB converter settings AFTER enable(), so a release that reloads the
    // block discards them: HD_INI_ST goes back to 1046 and the output raster
    // the encoder sees is no longer the one the switch built.
    Wire.reset();
    Wire.poison(Poison);
    HdBypass::enable();
    HdBypass::HD_INI_ST::write(0);
    HdBypass::HD_MATRIX_BYPS::write(1);
    HdBypass::HD_DYN_BYPS::write(1);

    HdBypass::hold();
    HdBypass::release();

    CHECK(HdBypass::enabled());
    CHECK(Wire.field(1, 0x39, 0, 11) == 0);
    CHECK(Wire.field(1, 0x30, 1, 1) == 1);
    CHECK(Wire.field(1, 0x30, 2, 1) == 1);
}


// ---------------------------------------------------------------------------
// What each standard implies. Every value below is decoded from the write
// traces captured through the bypass switch, so the same eyes that made the
// move are not also the ones checking it. No bench source reaches any of these
// standards, and every field is read by NAME -- a hand-written slice returns a
// plausible number rather than an error.

using Tv5725::Adc;
using Tv5725::Chip;
using Tv5725::ModeDetect;
using Tv5725::SyncProcessor;
using Tv5725::SyncMeasurement;

// The ladder runs after the switch has written the divider, and the SD arm
// derives its raster from it -- so a run that leaves it poisoned is asking a
// different question from the one the trace answers.
static const uint16_t DividerBeforeLadder = 2345;

// The line the bench source runs, which puts the divider below in the second
// crossover row. Only the RGBHV arm reads it.
static const uint32_t BenchLineRateHz = 37879;

// A source running nothing the standards state, which is what most of these
// cases are about.
static const Tv5725::SourceTiming Unpublished(0.0f);

static void applyForSource(uint16_t divider = DividerBeforeLadder,
                           uint32_t lineRateHz = BenchLineRateHz,
                           const Tv5725::SourceTiming &timing = Unpublished,
                           uint16_t frameLines = 0)
{
    Wire.reset();
    Wire.poison(Poison);
    Adc::PLLAD_MD::write(DividerBeforeLadder);
    HdBypass::applyForSource(divider, lineRateHz, timing, frameLines);
}

// ONE PATH FOR EVERY SOURCE. This dispatched on rto->videoStandardInput into
// four arms: interlaced SD, progressive SD, component, and the computed path
// everything else took. Each of the first three froze part of the sampling
// group or the channel raster per standard, and the values could not be right
// for any source but the one they were fitted to.
//
// ../docs/video-source-acquisition.md, step 12.

TEST_CASE("the channel raster is sized from the divider the engine holds")
{
    applyForSource(2039, 31469);

    CHECK(Adc::PLLAD_MD::read() == 2039);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 2047);   // + RasterGuardSamples
    CHECK(HdBypass::HD_HB_ST::read() == 2039);       // the line's end
    CHECK(HdBypass::HD_HB_SP::read()
          == (uint16_t)lrintf(Tv5725::AxisHorizontal.activeStart() * 2039.0f));
}

// 0x90 was inherited and nothing derived it. It is a count of SAMPLES against a
// divider chosen per source, so what it covers is whatever fraction that
// divider makes it -- and on the bench it covers nothing at all. Measured in
// bypass at 800x600@60 on a 2048 sample line, `HD_HB_SP` stepped 144, 240, 320,
// 400, 480, 560 against the panel:
//
//   HD_HB_SP        144  240  320  400  480  560
//   blanked columns   0    0    0   36  117  198
//
// The panel's own left edge falls at sample 364, so the first three blank
// nothing and the constant has been inert at every divider it has run at.
//
// AxisHorizontal::activeStart() is the engine's one answer to where video
// starts on a source whose raster it does not know, and the scaling path places
// its capture from it. One rule for both paths, rather than a second number
// that has to be kept in step by hand.
//
// **IT DOES NOT HIDE A SOURCE'S BORDER, AND MUST NOT BE TUNED UNTIL IT DOES.**
// The envelope is deliberately early so nothing is cropped. Hiding the bench
// source's 40-pixel border needs about 0.215 of the line against the envelope's
// 0.117, and bypass has no framing control to give the picture back with.
// docs/known-issues.md
TEST_CASE("the pass-through blank ends on the envelope, not on a constant")
{
    const uint16_t dividers[] = {1124, 1856, 2039};

    for (unsigned i = 0; i < sizeof(dividers) / sizeof(dividers[0]); i++) {
        CAPTURE(dividers[i]);
        applyForSource(dividers[i], 31469);

        CHECK(HdBypass::HD_HB_SP::read()
              == (uint16_t)lrintf(Tv5725::AxisHorizontal.activeStart()
                                  * (float)dividers[i]));
        CHECK(HdBypass::HD_HB_SP::read() < HdBypass::HD_HB_ST::read());
    }
}

// The envelope is the answer for a source whose raster is unknown. Where one
// IS known the standard states where active video starts, and pass-through
// plays that raster out untouched -- so the only thing it can blank correctly
// is what the raster says is not picture, which is the rule the vertical axis
// already follows.
//
// It matters because a mode file may spend part of the porch on BORDER, which
// is black active video and so electrically invisible. VESA 800x600@60 starts
// active at pixel 216 of 1056; the Acorn AKF50 mode of the same total and
// clock spends 176..216 on border and starts its 800 displayed pixels at 216
// too. Blanking to the published raster hides that border and crops nothing.
TEST_CASE("a published raster blanks the channel to where the standard puts video")
{
    const Tv5725::SourceTiming vesa800x600 =
        Tv5725::SourceTiming::matching(627, 60.0f, 128.0f / 1056.0f);
    REQUIRE(vesa800x600.published());

    applyForSource(2039, 37879, vesa800x600, 628);

    CHECK(HdBypass::HD_HB_SP::read()
          == (uint16_t)lrintf(216.0f / 1056.0f * 2039.0f));
}

// The far edge is the same argument as the near one. A mode file spends border
// at BOTH ends -- AKF50's 800x600 is 128,48,40,800,40,0, so 1016..1056 is
// border where VESA 800x600@60 spends 1016..1056 on front porch.
TEST_CASE("a published raster blanks the channel where the standard ends video")
{
    const Tv5725::SourceTiming vesa800x600 =
        Tv5725::SourceTiming::matching(627, 60.0f, 128.0f / 1056.0f);
    REQUIRE(vesa800x600.published());

    applyForSource(2039, 37879, vesa800x600, 628);

    CHECK(HdBypass::HD_HB_ST::read()
          == (uint16_t)lrintf(1016.0f / 1056.0f * 2039.0f));
}

TEST_CASE("the blanking start stays inside the line at every divider")
{
    // Above HD_HSYNC_RST that edge never fires, so the only blanking left in
    // the line is HD_HB_SP's and it reads as a black bar down the left of the
    // picture.
    const uint16_t dividers[] = {512, 1124, 1856, 2039};

    for (unsigned i = 0; i < sizeof(dividers) / sizeof(dividers[0]); i++) {
        CAPTURE(dividers[i]);
        applyForSource(dividers[i], 31469);

        CHECK(HdBypass::HD_HB_ST::read() < HdBypass::HD_HSYNC_RST::read());
    }
}

TEST_CASE("the channel's sync is delayed to match the delay it adds to the sample")
{
    // Left at the counter's origin the pulse leads the video it belongs to, and
    // the sink opens its window early on a band of the source's back porch.
    // Measured at 800x600@60 and again at 640x480@60 -- different back porches,
    // same correction -- so it is the channel's delay and not the source's.
    // ../docs/investigations/one-bypass-route-carries-rgbhv.md
    applyForSource(1856, BenchLineRateHz);

    CHECK(HdBypass::HD_HS_ST::read() == 40);
    CHECK(HdBypass::HD_HS_SP::read() == 164);
}

TEST_CASE("the channel plays out a vertical sync pulse of its own")
{
    // Three of the four arms wrote one of their own -- 3/522, 1/621, 6/0 --
    // and every one of them was five or six lines within ten of the frame's
    // start, so there was no raster property behind the differences.
    applyForSource(2039, 31469);

    CHECK(HdBypass::HD_VS_ST::read() == 2);
    CHECK(HdBypass::HD_VS_SP::read() == 7);
}

TEST_CASE("the SD vertical sync position is one value for every source")
{
    // The arms named it per standard -- 250/1, 301/5, 520/522, 48/46 -- and
    // 576p's 48 lands in active video rather than in the vertical interval.
    // Nothing on this route writes the pair otherwise, so a source reaching it
    // inherits whatever the last entry left.
    // ../docs/investigations/the-sd-vsync-window-follows-the-sync-type.md
    applyForSource(2039, 31469);

    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 14);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 11);
}

TEST_CASE("the sampling group follows the clock the divider and the rate make")
{
    // 1856 samples on a 37879 Hz line is CKO 70.3 MHz, which the crossover
    // table takes at post divider one and so runs the VCO at 140.6 MHz --
    // above the gain threshold the bench sweep put at 130.
    applyForSource(1856, BenchLineRateHz);

    CHECK(Adc::PLLAD_MD::read() == 1856);
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_ICP::read() == 6);
    CHECK(Adc::PLLAD_FS::read() == 1);
}

TEST_CASE("the analog corner is left as wide as the part offers")
{
    // The interlaced SD arm narrowed it to 40 MHz and inverted four sync
    // polarities, which on a 480p component source is why it never locked.
    // ../docs/investigations/the-analog-filter-corner-is-above-nyquist.md
    applyForSource(1124, 31469);

    CHECK(Adc::ADC_FLTR::read() == 0);
}

TEST_CASE("the sync polarities are not this block's to invert")
{
    // The interlaced SD arm flipped three of them and Mode Detect's two, and
    // nothing put them back for the source after it -- which is what the
    // bypass entry now has to do on the way in. Nothing here touches them.
    Wire.reset();
    SyncProcessor::SP_HS2PLL_INV_REG::write(1);
    SyncProcessor::SP_CS_P_SWAP::write(1);
    SyncProcessor::SP_HS_PROC_INV_REG::write(1);
    ModeDetect::MD_HS_FLIP::write(1);
    ModeDetect::MD_VS_FLIP::write(1);

    HdBypass::applyForSource(2039, 31469, Unpublished, 0);

    CHECK(SyncProcessor::SP_HS2PLL_INV_REG::read() == 1);
    CHECK(SyncProcessor::SP_CS_P_SWAP::read() == 1);
    CHECK(SyncProcessor::SP_HS_PROC_INV_REG::read() == 1);
    CHECK(ModeDetect::MD_HS_FLIP::read() == 1);
    CHECK(ModeDetect::MD_VS_FLIP::read() == 1);
}

TEST_CASE("the coast lengths are not written here")
{
    // The component arm wrote 4/4 over what applyForSyncType() had just
    // established, and SP_DLT_REG 0x70 over applyPulseWidthDifference()'s.
    // A second writer of the coast closes a loop: the coast changes the
    // measured line count, a changed count arms a solve, and a solve applies
    // the sync type -- which writes the coast again.
    // ../docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
    Wire.reset();
    SyncProcessor::SP_PRE_COAST::write(7);
    SyncProcessor::SP_POST_COAST::write(3);
    SyncProcessor::SP_DLT_REG::write(0xC0);

    HdBypass::applyForSource(2039, 31469, Unpublished, 0);

    CHECK(SyncProcessor::SP_PRE_COAST::read() == 7);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 3);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0xC0);
}

TEST_CASE("oversampling costs the channel nothing, so pass-through takes it all")
{
    // The decimators undo the faster tap, so PLLAD_MD samples a line reach the
    // channel whatever the ratio -- the played-out raster does not shrink with
    // it. Measured: at ratio two with the raster halved the picture fills half
    // the screen through an encoder that has re-acquired, and putting the
    // raster back to the divider restores it whole.
    // ../docs/investigations/the-decimators-filter.md

    SUBCASE("the played-out line is the divider, not the divider over the ratio") {
        Wire.reset();
        HdBypass::applyPassThroughSampling(2039, 37879, 2);
        REQUIRE(Adc::ADC_CLK_ICLK1X::read() == 1);   // ratio two really applied

        CHECK(HdBypass::HD_HSYNC_RST::read() == 2039 + 8);
        CHECK(HdBypass::HD_HB_ST::read() == 2039);
    }

    SUBCASE("the raster is the same at either ratio") {
        Wire.reset();
        HdBypass::applyPassThroughSampling(2039, 37879, 1);
        const uint16_t undecimated = HdBypass::HD_HSYNC_RST::read();

        Wire.reset();
        HdBypass::applyPassThroughSampling(2039, 37879, 2);

        CHECK(HdBypass::HD_HSYNC_RST::read() == undecimated);
    }
}

TEST_CASE("the channel blanks the lines before active video")
{
    // 720x480p is 525 lines with active starting at 36. Every arm carried a
    // constant instead -- the progressive one 0x40, which is 64, so 28 lines
    // of picture came off the top.
    applyForSource(2039, 31469, Tv5725::SourceTiming::matching(524, 60.0f, 62.0f / 858.0f), 525);

    CHECK(HdBypass::HD_VB_ST::read() == 0);
    CHECK(HdBypass::HD_VB_SP::read() == 36);
}

TEST_CASE("a source running no published raster keeps the window it had")
{
    // No raster means no line to trust, and blanking a guessed count costs
    // picture. Leaving the window alone is the one answer that cannot.
    Wire.reset();
    Wire.poison(Poison);
    HdBypass::HD_VB_SP::write(64);

    HdBypass::applyForSource(2039, 31469, Unpublished, 0);

    CHECK(HdBypass::HD_VB_SP::read() == 64);
}

TEST_CASE("no divider means no sampling to install and no raster to size")
{
    // A rate with no divider behind it is a source that has not been measured,
    // and a raster sized from nothing is worse than the resting one.
    Wire.reset();
    HdBypass::enable();
    Adc::PLLAD_MD::write(DividerBeforeLadder);

    HdBypass::applyForSource(0, BenchLineRateHz, Unpublished, 0);

    CHECK(Adc::PLLAD_MD::read() == DividerBeforeLadder);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 1023);
    CHECK(HdBypass::HD_HB_ST::read() == 3976);
}

// The channel emits the pulse it is programmed with, and the sink expects it
// the way round the SOURCE sends it. The pair is held rather than read back:
// two registers cannot say which of the values in them is the start, and the
// arms and the computed path write different pairs.

static Tv5725::HdBypass::SourceSyncEdges edges(bool hFound, bool hPositive,
                                               bool vFound, bool vPositive)
{
    Tv5725::HdBypass::SourceSyncEdges e;
    e.hsyncFound = hFound;
    e.hsyncPositive = hPositive;
    e.vsyncFound = vFound;
    e.vsyncPositive = vPositive;
    return e;
}

TEST_CASE("a positive source hsync puts the channel's pulse start first")
{
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);

    HdBypass::applyChannelSyncEdges(edges(true, true, false, false));

    CHECK(HdBypass::HD_HS_ST::read() == 40);
    CHECK(HdBypass::HD_HS_SP::read() == 164);
    CHECK(Tv5725::SyncProcessor::SP_HS2PLL_INV_REG::read() == 0);
}

TEST_CASE("a negative source hsync puts the channel's pulse stop first")
{
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);

    HdBypass::applyChannelSyncEdges(edges(true, false, false, false));

    CHECK(HdBypass::HD_HS_ST::read() == 164);
    CHECK(HdBypass::HD_HS_SP::read() == 40);
    CHECK(Tv5725::SyncProcessor::SP_HS2PLL_INV_REG::read() == 1);
}

TEST_CASE("an hsync the sync processor cannot see leaves the pulse alone")
{
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);
    Tv5725::SyncProcessor::SP_HS2PLL_INV_REG::write(1);

    HdBypass::applyChannelSyncEdges(edges(false, false, false, false));

    CHECK(HdBypass::HD_HS_ST::read() == 40);
    CHECK(HdBypass::HD_HS_SP::read() == 164);
    CHECK(Tv5725::SyncProcessor::SP_HS2PLL_INV_REG::read() == 1);
}

TEST_CASE("a positive source vsync puts the channel's vertical start first")
{
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);

    HdBypass::applyChannelSyncEdges(edges(false, false, true, true));

    CHECK(HdBypass::HD_VS_ST::read() == 2);
    CHECK(HdBypass::HD_VS_SP::read() == 7);
}

TEST_CASE("a negative source vsync puts the channel's vertical stop first")
{
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);

    HdBypass::applyChannelSyncEdges(edges(false, false, true, false));

    CHECK(HdBypass::HD_VS_ST::read() == 7);
    CHECK(HdBypass::HD_VS_SP::read() == 2);
}

TEST_CASE("a vsync the sync processor cannot see leaves the vertical pair alone")
{
    // Which is also the composite-sync case: STATUS_SYNC_PROC_VSACT reads 0 on
    // the csync path, so the found bit answers the same question the sync type
    // was being asked. ../CLAUDE.md
    Wire.reset();
    HdBypass::applyPassThroughSampling(2039, 37879);
    HdBypass::applyChannelSyncEdges(edges(false, false, true, false));

    HdBypass::applyChannelSyncEdges(edges(false, false, false, true));

    CHECK(HdBypass::HD_VS_ST::read() == 7);
    CHECK(HdBypass::HD_VS_SP::read() == 2);
}

TEST_CASE("the resting pulses are what a polarity finds before any source does")
{
    // Nothing has entered the channel, so the pair the block came up with is
    // what there is to order.
    Wire.reset();
    HdBypass::enable();

    HdBypass::applyChannelSyncEdges(edges(true, false, true, false));

    CHECK(HdBypass::HD_HS_ST::read() == 124);
    CHECK(HdBypass::HD_HS_SP::read() == 0);
    CHECK(HdBypass::HD_VS_ST::read() == 7);
    CHECK(HdBypass::HD_VS_SP::read() == 2);
}

TEST_CASE("the channel's entry records the oversampling it leaves the ADC on")
{
    // A caller asking what the ADC is running has to get what this installed
    // rather than whatever the last solve did.
    Wire.reset();
    REQUIRE(Adc::applySampleRate(2250, 15574, Adc::OversampleAsClockAllows) == 4);

    HdBypass::applyForSource(2039, 31469, Unpublished, 0);

    // 2039 samples on a 31469 Hz line is CKO 64.2 MHz, which the crossover
    // table takes at post divider one -- so two is all the tap can carry.
    CHECK(Adc::oversampleInForce() == 2);
}

// Whether a measured source should be passed through at all, which is a
// question about this block rather than about the measurement.

TEST_CASE("only a rate a display accepts may be bypassed")
{
    // Bypass hands the source's own timing to the encoder, so it works only
    // where the DISPLAY can show that timing. Refusing falls back to the
    // scaling path, which shows any rate; accepting wrongly puts torn,
    // sheared content on the panel that reads as a broken scaler.
    // docs/rgbhv-bypass-trap.md
    SourceMeasurement measurement;

    SUBCASE("nothing measured yet cannot be bypassed") {
        CHECK_FALSE(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }

    SUBCASE("a 15.6 kHz line cannot") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK_FALSE(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }

    SUBCASE("the 31.4 kHz VGA line can") {
        // 640x480@60, VTOTAL 524. Measured locking.
        seedSourceLines(524);
        g_fieldRate = 60.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }

    SUBCASE("26.6 kHz can, which is under the VGA line") {
        // 640x512@50, VTOTAL 533. Measured locking, which is why the floor is
        // bracketed rather than taken from the VGA standard.
        seedSourceLines(533);
        g_fieldRate = 50.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }

    SUBCASE("21.8 kHz cannot, measured") {
        // 640x352@60, VTOTAL 363. Measured: the sink reports no signal, and
        // this rate clears LowLineRateBelowHz -- so that constant is not the
        // one to ask.
        seedSourceLines(363);
        g_fieldRate = 60.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK_FALSE(measurement.lowLineRate());
        CHECK_FALSE(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }
}
TEST_CASE("a source that can be passed through is never a slow-line source")
{
    // The two thresholds are disjoint, with 6 kHz between them, and code has
    // been written that only acts where both hold -- an HD bypass vsync steer
    // gated on lowLineRate(), which no source reaching the channel could ever
    // satisfy. Bringing the floors together again would revive that shape
    // silently, so the gap is asserted rather than left to be read off two
    // constants in different parts of the header.
    SourceMeasurement measurement;

    SUBCASE("the slowest rate that may bypass is well clear of the slow-line split") {
        // 640x512@50, VTOTAL 533 -- the measured floor.
        seedSourceLines(533);
        g_fieldRate = 50.0f;
        REQUIRE(rateMeasured(measurePastGate(measurement)));
        CHECK(HdBypass::suitsLineRate(measurement.lineRateHz()));
        CHECK_FALSE(measurement.lowLineRate());
    }

    SUBCASE("a 15.6 kHz line is slow and cannot bypass") {
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        REQUIRE(rateMeasured(measurePastGate(measurement)));
        CHECK(measurement.lowLineRate());
        CHECK_FALSE(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }
}
TEST_CASE("a source at 640x480 or above is passed through, and anything below is scaled")
{
    // A sink that takes HDMI takes 640x480 and up, so a source at least that
    // big reaches the panel intact by being handed over untouched -- and the
    // scaling path cannot carry it well anyway, the capture's write limit
    // bounding a line at about 1024 IF units however it is placed.
    // ../capture-limits.md
    SourceMeasurement measurement;

    SUBCASE("640x480 is the smallest that goes through") {
        // VTOTAL 524 at 60 Hz -- a 31.5 kHz line, which no sink taking HDMI
        // may refuse.
        seedSourceLines(524);
        g_fieldRate = 60.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK(HdBypass::suitsSource(524, measurement.fieldRateHz()));
    }

    SUBCASE("a source the line doubler is needed for is scaled") {
        // 320x256@50: 311 lines is short of a frame, so the capture doubles it
        // and there is nothing to hand over.
        seedSourceLines(311);
        g_fieldRate = 50.08f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK_FALSE(HdBypass::suitsSource(311, measurement.fieldRateHz()));
    }

    SUBCASE("a rate the sink refuses is scaled however tall the source") {
        // 448 lines at 50 Hz is a 22.4 kHz line: tall enough to need no
        // doubling and still under the floor the bench display locks at.
        seedSourceLines(448);
        g_fieldRate = 50.0f;
        CHECK(rateMeasured(measurePastGate(measurement)));
        CHECK_FALSE(HdBypass::suitsSource(448, measurement.fieldRateHz()));
    }

    SUBCASE("nothing counted is scaled") {
        CHECK_FALSE(HdBypass::suitsSource(0, measurement.fieldRateHz()));
    }

    SUBCASE("and a rate that was not measured is scaled, not stood in for") {
        // A COUNT ALONE LOOKS PASSABLE. 524 lines needs no doubling and clears
        // the sink floor at 60 Hz, so a stand-in field rate turns an unmeasured
        // source into a bypassed one -- and bypass hands the source's own
        // timing to the encoder, which is a mode the display may show nothing
        // for. Unreachable from the engine, because the decision follows a
        // completed measurement and measureLineRate() refuses a field rate
        // outside fieldRateIsSource() -- so this is what the answer must be if
        // a caller ever arrives without one.
        CHECK_FALSE(HdBypass::suitsSource(524, 0.0f));
    }
}
TEST_CASE("a source already bypassed is judged on a count taken now")
{
    // **THE HELD RATE CANNOT ANSWER THIS.** Bypass measures nothing, so what is
    // held still names the mode bypass was entered on -- a source that slows
    // underneath it keeps reading as displayable, the branch that would leave
    // never fires, and the panel stays blank for ever.
    // docs/rgbhv-bypass-trap.md
    SourceMeasurement measurement;

    seedSourceLines(524);
    g_fieldRate = 60.0f;
    CHECK(rateMeasured(measurePastGate(measurement)));
    CHECK(HdBypass::suitsLineRate(measurement.lineRateHz()));

    SUBCASE("the held rate outlives the mode it was measured on") {
        // 320x256@50 arrives while bypassed. Nothing re-measures, so the held
        // rate is still the 31.4 kHz line of the mode before it.
        seedSourceLines(311);
        CHECK(HdBypass::suitsLineRate(measurement.lineRateHz()));
    }

    SUBCASE("the count is what has moved, and it refuses") {
        CHECK_FALSE(HdBypass::suitsLineRate((uint32_t)(311 * measurement.fieldRateHz())));
    }

    SUBCASE("a count the display still takes stays bypassed") {
        CHECK(HdBypass::suitsLineRate((uint32_t)(524 * measurement.fieldRateHz())));
    }

    SUBCASE("nothing counted decides nothing") {
        CHECK_FALSE(HdBypass::suitsLineRate((uint32_t)(0 * measurement.fieldRateHz())));
    }
}

