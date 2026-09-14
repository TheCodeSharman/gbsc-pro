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
static int rgbPatchCalls = 0;
static void countRgbPatches() { ++rgbPatchCalls; }
float getSourceFieldRate(boolean) { return 50.0f; }
void tv5725Log(const char *) {}

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourSpace.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ModeDetect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"

using Tv5725::ColourSpace;
using Tv5725::HdBypass;

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

static void applyForActiveStart(uint8_t standard, uint16_t activeStartLine,
                                uint16_t divider = 2039,
                                uint32_t lineRateHz = 31469)
{
    Wire.reset();
    Wire.poison(Poison);
    Adc::PLLAD_MD::write(DividerBeforeLadder);
    rgbPatchCalls = 0;
    HdBypass::applyForStandard(standard, divider, lineRateHz, activeStartLine,
                               countRgbPatches);
}

static void applyForStandard(uint8_t standard, uint16_t sourceLines = 311,
                             uint16_t divider = DividerBeforeLadder,
                             uint32_t lineRateHz = BenchLineRateHz)
{
    Wire.reset();
    Wire.poison(Poison);
    Adc::PLLAD_MD::write(DividerBeforeLadder);
    Tv5725::Tv5725::STATUS_SYNC_PROC_VTOTAL::write(sourceLines);
    rgbPatchCalls = 0;
    HdBypass::applyForStandard(standard, divider, lineRateHz,
                               0, countRgbPatches);
}

TEST_CASE("interlaced SD plays out a raster derived from the divider")
{
    applyForStandard(1);

    CHECK(HdBypass::HD_HSYNC_RST::read() == 1180);  // MD / 2 + 8
    CHECK(HdBypass::HD_HB_ST::read() == 2216);      // 0.945 of MD
    CHECK(HdBypass::HD_HB_SP::read() == 144);
    CHECK(HdBypass::HD_HS_ST::read() == 128);
    CHECK(HdBypass::HD_HS_SP::read() == 0);
}

TEST_CASE("interlaced SD inverts the three sync polarities and flips detection")
{
    applyForStandard(2);

    CHECK(SyncProcessor::SP_HS2PLL_INV_REG::read() == 1);
    CHECK(SyncProcessor::SP_CS_P_SWAP::read() == 1);
    CHECK(SyncProcessor::SP_HS_PROC_INV_REG::read() == 1);
    CHECK(ModeDetect::MD_HS_FLIP::read() == 1);
    CHECK(ModeDetect::MD_VS_FLIP::read() == 1);
    CHECK(Chip::OUT_SYNC_SEL::read() == 2);
    CHECK(SyncProcessor::SP_HS_LOOP_SEL::read() == 0);
    CHECK(Adc::ADC_FLTR::read() == 3);  // the 40 MHz corner
    CHECK(SyncProcessor::SP_CS_HS_ST::read() == 160);
    CHECK(SyncProcessor::SP_CS_HS_SP::read() == 0);
}

TEST_CASE("the two SD field rates differ only in where vertical sync sits")
{
    applyForStandard(1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 250);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 1);
    CHECK(HdBypass::HD_VS_ST::read() == 3);
    CHECK(HdBypass::HD_VS_SP::read() == 522);

    applyForStandard(2);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 45);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 5);
    CHECK(HdBypass::HD_VS_ST::read() == 1);
    CHECK(HdBypass::HD_VS_SP::read() == 621);
}

TEST_CASE("progressive SD sizes its raster from the divider the engine holds")
{
    // The 0x864 blanking start it carried was sized for the 2345 the bypass
    // switch used to write into PLLAD_MD on the way in. That literal is gone,
    // so the constant outran the line and the picture came out blanked.
    applyForStandard(3, 524, 2039, 31469);

    CHECK(Adc::PLLAD_MD::read() == 2039);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 2047);
    CHECK(HdBypass::HD_HB_ST::read() == 2039);
    CHECK(HdBypass::HD_HB_SP::read() == 144);
}

TEST_CASE("progressive SD places vertical sync where the standard puts it")
{
    applyForStandard(3, 524, 2039, 31469);

    CHECK(HdBypass::HD_VS_ST::read() == 6);
    CHECK(HdBypass::HD_VS_SP::read() == 0);
}

TEST_CASE("the two progressive standards differ in the vsync window alone")
{
    applyForStandard(3, 524, 2039, 31469);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 8);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 10);

    applyForStandard(4, 524, 2039, 31469);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 48);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 46);
}

TEST_CASE("the HD standards sample off the measurement, not off a frozen literal")
{
    // 720p, 1080i and 1080p each carried a divider, a crossover row, a clock
    // tap and a decimator pair chosen for one raster. 720p's PLLAD_MD 2474 is
    // past MaxChannelLine - RasterGuardSamples, so the channel could never play
    // the line out whatever the source did.
    // ../docs/investigations/the-bypass-divider-is-capped-by-the-channel-counter.md
    for (uint8_t standard : {5, 6, 7}) {
        CAPTURE(standard);
        applyForStandard(standard, 524, 2039, 31469);

        CHECK(Adc::PLLAD_MD::read() == 2039);
        CHECK(HdBypass::HD_HSYNC_RST::read() == 2047);
        CHECK(HdBypass::HD_HB_ST::read() == 2039);
        CHECK(HdBypass::HD_HS_ST::read() == 40);
        CHECK(HdBypass::HD_HS_SP::read() == 164);
    }
}

TEST_CASE("the computed path plays out a vertical sync pulse of its own")
{
    // The arms each wrote one and the computed path did not, so it ran on
    // whatever enable() last rested at. The pulse belongs beside the horizontal
    // one, which is already the computed path's.
    Wire.reset();
    Wire.poison(Poison);

    HdBypass::applyPassThroughSampling(2039, 37879);

    CHECK(HdBypass::HD_VS_ST::read() == 2);
    CHECK(HdBypass::HD_VS_SP::read() == 7);
}

TEST_CASE("a source with no arm of its own is given the SD vertical sync position")
{
    // Nothing on this route writes the pair otherwise, so a source reaching it
    // inherits whatever the last entry left -- the same defect the four sync
    // polarities the bypass switch puts back already had. One value for every
    // source, as the scaling path has.
    // ../docs/investigations/the-sd-vsync-window-follows-the-sync-type.md
    applyForStandard(14, 311, 1856);

    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 14);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 11);
}

TEST_CASE("an RGBHV source plays out the line the CHANNEL sees, not the ADC line")
{
    // The channel is fed the decimated sample stream, so its line is the
    // divider over the oversampling ratio. Derived from the ADC line instead,
    // HD_HB_ST lands beyond the end of the channel's line and the blank
    // generator never fires at all -- which is what left a bar of the source's
    // own back porch down the left and clipped the right.
    // docs/investigations/one-bypass-route-carries-rgbhv.md
    for (uint8_t standard : {14, 15}) {
        CAPTURE(standard);
        applyForStandard(standard, 311, 1856);

        CHECK(HdBypass::HD_HSYNC_RST::read() == 1864);  // 1856 + 8
        CHECK(HdBypass::HD_HB_ST::read() == 1856);      // the line's end
        CHECK(HdBypass::HD_HB_ST::read() < HdBypass::HD_HSYNC_RST::read());
    }
}

TEST_CASE("an RGBHV source delays its sync to match the channel's own delay")
{
    // Video passes THROUGH the channel on this route and around it on the
    // ADC-to-DAC one, but the sync the block emits is the same either way. Left
    // at the counter's origin it leads the video it belongs to, and the sink
    // opens its window early on a band of the source's back porch.
    //
    // Measured at 800x600@60 and again at 640x480@60 -- different back porches,
    // same correction -- so it is the channel's delay and not the source's.
    // docs/investigations/one-bypass-route-carries-rgbhv.md
    applyForStandard(14, 311, 1856);

    CHECK(HdBypass::HD_HS_ST::read() == 40);
    CHECK(HdBypass::HD_HS_SP::read() == 164);
}

TEST_CASE("a source with no standard samples off its own clock")
{
    // 1856 samples on a 37879 Hz line is CKO 70.3 MHz, which the crossover
    // table takes at post divider one and so runs the VCO at 140.6 MHz -- above
    // the gain threshold the bench sweep put at 130.
    applyForStandard(14, 311, 1856);

    CHECK(Adc::PLLAD_MD::read() == 1856);
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_ICP::read() == 4);
    CHECK(Adc::PLLAD_FS::read() == 1);
    CHECK(Adc::ADC_FLTR::read() == 0);
}

TEST_CASE("the channel blanks the lines before active video, whatever the standard")
{
    // 720x480p is 525 lines with active starting at 36. Every arm carried a
    // constant instead -- the progressive one 0x40, which is 64, so 28 lines of
    // picture came off the top. STATUS_SYNC_PROC_VTOTAL counts from zero, hence
    // 524 for a 525-line frame.
    applyForActiveStart(3, 36);

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

    HdBypass::applyForStandard(0, 2039, 31469, 0, countRgbPatches);

    CHECK(HdBypass::HD_VB_SP::read() == 64);
}

TEST_CASE("every arm leaves the blanking start inside the line")
{
    // Above HD_HSYNC_RST that edge never fires, so the only blanking left in
    // the line is HD_HB_SP's and it reads as a black bar down the left of the
    // picture. applyHorizontalFromChannelLine() states the rule; the arms that
    // freeze a raster per standard were sized for a divider literal that no
    // longer exists.
    // 13 is left out: it writes neither register, so it inherits the channel
    // raster from whatever entered bypass before it. That is a different
    // defect and it has no bench source.
    const uint8_t standards[] = {0, 3, 4, 5, 6, 7, 14};

    for (unsigned i = 0; i < sizeof(standards); i++) {
        CAPTURE(standards[i]);
        applyForStandard(standards[i], 524, 2039, 31469);

        CHECK(HdBypass::HD_HB_ST::read() < HdBypass::HD_HSYNC_RST::read());
    }
}

TEST_CASE("a source no standard names is passed through, not taken for SD")
{
    // 0 is "nothing recognised", not a standard. Taken for SD it gets a channel
    // line of PLLAD_MD/2 and a blanking start of 0.945 x PLLAD_MD, so blanking
    // begins past the end of the line, that edge never fires, and the only one
    // left is HD_HB_SP -- a black bar down the left of the picture. Measured on
    // the bench with a Wii at 480p on ypbpr: HD_HSYNC_RST 570 against
    // HD_HB_ST 1062.
    applyForStandard(0, 524, 1124, 31469);

    CHECK(HdBypass::HD_HB_ST::read() < HdBypass::HD_HSYNC_RST::read());
}

TEST_CASE("a source no standard names samples off the divider it is handed")
{
    applyForStandard(0, 524, 1124, 31469);

    CHECK(Adc::PLLAD_MD::read() == 1124);
}

TEST_CASE("a source no standard names keeps the widest analog corner")
{
    // The SD arm narrows it to 40 MHz and inverts four sync polarities, which
    // on a 480p component source is why it never locks.
    applyForStandard(0, 524, 1124, 31469);

    CHECK(Adc::ADC_FLTR::read() == 0);
}

TEST_CASE("oversampling costs the channel nothing, so pass-through takes it all")
{
    // The decimators undo the faster tap, so PLLAD_MD samples a line reach the
    // channel whatever the ratio -- the played-out raster does not shrink with
    // it. Measured: at ratio two with the raster halved the picture fills half
    // the screen through an encoder that has re-acquired, and putting the
    // raster back to the divider restores it whole.
    // docs/investigations/the-decimators-filter.md

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

    SUBCASE("asking for more than the row carries takes what it has") {
        // A doubling costs a step of PLLAD_KS headroom and there is no tap
        // above the top row, so the ceiling is 2^postDivider.
        Wire.reset();
        CHECK(Adc::applySampleRate(2039, 37879, Adc::OversampleAsClockAllows) == 2);
        CHECK(Adc::PLLAD_KS::read() == 1);

        Wire.reset();
        CHECK(Adc::applySampleRate(2039, 60000, Adc::OversampleAsClockAllows) == 1);
        CHECK(Adc::PLLAD_KS::read() == 0);
    }
}

TEST_CASE("the pass-through divider is as dense as the channel and the PLL allow")
{
    // PLLAD_MD is samples per line, and pass-through writes nothing to memory,
    // so the capture's write limit does not bound it. Two other things do.

    SUBCASE("the channel's own counter binds it at a bench line rate") {
        // HD_HSYNC_RST is eleven bits and the counter ignores the twelfth the
        // register stores, so the played-out line stops at 2047 -- the divider
        // plus the guard the generator needs past it.
        CHECK(HdBypass::dividerFor(37879) == 2039);
    }

    SUBCASE("the PLL's top row binds it on a fast line") {
        // RD-5725-1.1's rows stop at 162 MHz and there is nothing above, so a
        // fast enough source runs out of clock before it runs out of counter.
        CHECK(HdBypass::dividerFor(100000) == 1620);
    }

    SUBCASE("nothing measured asks for nothing") {
        // A divider of 0 leaves the block at its resting timing rather than
        // playing out a raster derived from a zero.
        CHECK(HdBypass::dividerFor(0) == 0);
    }
}

TEST_CASE("an RGBHV source takes the crossover row its own clock lands in")
{
    // The row is not a property of pass-through, it is a property of the
    // frequency the divider and the line rate make between them -- and a row
    // left on the wrong band takes the PLL out of lock, measured, with the
    // divider never latching and STATUS_SYNC_PROC_HTOTAL reading neither value.

    SUBCASE("a 70 MHz clock is the second row") {
        applyForStandard(14, 311, 1856, 37879);
        CHECK(Adc::PLLAD_KS::read() == 1);
    }

    SUBCASE("the same divider on a 15 kHz line is 29 MHz and the third") {
        applyForStandard(14, 311, 1856, 15625);
        CHECK(Adc::PLLAD_KS::read() == 2);
    }

    SUBCASE("a divider past the second row's ceiling takes the first") {
        applyForStandard(14, 311, 2400, 37879);
        CHECK(Adc::PLLAD_KS::read() == 0);
    }
}

TEST_CASE("an RGBHV source samples at the divider it is handed, not the literal")
{
    // The switch writes a literal into PLLAD_MD on its way here, so a raster
    // read back off the register is a raster for that literal. Measured on the
    // bench: derived from the switch's 2345 against a source measured at 1124,
    // the sink reports no signal.
    Wire.reset();
    Wire.poison(Poison);
    Adc::PLLAD_MD::write(DividerBeforeLadder);

    HdBypass::applyForStandard(14, 1124, BenchLineRateHz,
                               0, countRgbPatches);

    CHECK(Adc::PLLAD_MD::read() == 1124);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 1132);  // 1124 + 8
    CHECK(HdBypass::HD_HB_ST::read() == 1124);      // the line's end
}

TEST_CASE("an unmeasured source leaves the bypass raster alone")
{
    // Nothing solved yet. Deriving from a zero would play out a raster of no
    // width at all, where the resting timing at least leaves the block in the
    // state the switch built.
    Wire.reset();
    Wire.poison(Poison);
    HdBypass::enable();
    Adc::PLLAD_MD::write(DividerBeforeLadder);

    HdBypass::applyForStandard(14, 0, BenchLineRateHz,
                               0, countRgbPatches);

    CHECK(Adc::PLLAD_MD::read() == DividerBeforeLadder);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 1023);
    CHECK(HdBypass::HD_HB_ST::read() == 3976);
}

TEST_CASE("RGBHV patches the RGB path and coasts on its own pair")
{
    // The colour path is NOT this arm's: it follows the input selection, which
    // is where whether the source is component is known.
    applyForStandard(13);

    CHECK(rgbPatchCalls == 1);
    CHECK(SyncMeasurement::isCsync());
    CHECK(SyncProcessor::SP_PRE_COAST::read() == 4);
    CHECK(SyncProcessor::SP_POST_COAST::read() == 4);
    CHECK(SyncProcessor::SP_DLT_REG::read() == 0x70);
    CHECK(SyncProcessor::SP_VS_PROC_INV_REG::read() == 0);
}

TEST_CASE("RGBHV samples the line undecimated")
{
    applyForStandard(13);

    CHECK(Adc::PLLAD_MD::read() == 512);
    CHECK(Adc::PLLAD_CKOS::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK1X::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK2X::read() == 0);
    CHECK(Adc::DEC1_BYPS::read() == 1);
    CHECK(Adc::DEC2_BYPS::read() == 1);
}

TEST_CASE("an RGBHV source picks its PLL row off its own line count")
{
    // The only quantity in the ladder that no standard can carry. It follows
    // STATUS_SYNC_PROC_VTOTAL, which is a measurement of the source and so one
    // of the reads the engine is allowed.
    applyForStandard(13, 311);
    CHECK(Adc::PLLAD_KS::read() == 3);
    CHECK(Adc::PLLAD_FS::read() == 1);

    applyForStandard(13, 627);
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_FS::read() == 0);

    applyForStandard(13, 1125);
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_FS::read() == 1);
}

TEST_CASE("the boundaries between the three PLL rows")
{
    applyForStandard(13, 531);
    CHECK(Adc::PLLAD_KS::read() == 3);
    applyForStandard(13, 532);
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_FS::read() == 0);
    applyForStandard(13, 809);
    CHECK(Adc::PLLAD_FS::read() == 0);
    applyForStandard(13, 810);
    CHECK(Adc::PLLAD_FS::read() == 1);
}

TEST_CASE("only an RGBHV source reads its line count at all")
{
    // Every other standard's values are fixed, so a ladder that consulted the
    // measurement for one of them would move with the source.
    for (uint8_t standard = 1; standard <= 7; ++standard) {
        CAPTURE(standard);
        applyForStandard(standard, 311);
        const uint16_t ks = Adc::PLLAD_KS::read();
        const uint16_t fs = Adc::PLLAD_FS::read();

        applyForStandard(standard, 1125);
        CHECK(Adc::PLLAD_KS::read() == ks);
        CHECK(Adc::PLLAD_FS::read() == fs);
    }
}
