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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncType.h"

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

TEST_CASE("the input pipe and both converters are in circuit")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x30, 0, 1) == 0);  // HD_IN_DREG_BYPS
    CHECK(Wire.field(1, 0x30, 1, 1) == 0);  // HD_MATRIX_BYPS
    CHECK(Wire.field(1, 0x30, 2, 1) == 0);  // HD_DYN_BYPS
    CHECK(Wire.field(1, 0x30, 3, 1) == 0);  // HD_SEL_BLK_IN
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
    CHECK(Wire.field(1, 0x39, 0, 11) == 1046);  // HD_INI_ST
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
using Tv5725::ColourSpace;
using Tv5725::ModeDetect;
using Tv5725::SyncProcessor;
using Tv5725::SyncType;

// The ladder runs after the switch has written the divider, and the SD arm
// derives its raster from it -- so a run that leaves it poisoned is asking a
// different question from the one the trace answers.
static const uint16_t DividerBeforeLadder = 2345;

static void applyForStandard(uint8_t standard, uint16_t sourceLines = 311)
{
    Wire.reset();
    Wire.poison(Poison);
    Adc::PLLAD_MD::write(DividerBeforeLadder);
    Tv5725::Tv5725::STATUS_SYNC_PROC_VTOTAL::write(sourceLines);
    rgbPatchCalls = 0;
    HdBypass::applyForStandard(standard, countRgbPatches);
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

TEST_CASE("the two SD field rates differ only in the vertical")
{
    applyForStandard(1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 250);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 1);
    CHECK(HdBypass::HD_VB_ST::read() == 500);
    CHECK(HdBypass::HD_VB_SP::read() == 16);
    CHECK(HdBypass::HD_VS_ST::read() == 3);
    CHECK(HdBypass::HD_VS_SP::read() == 522);

    applyForStandard(2);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 1);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 45);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 5);
    CHECK(HdBypass::HD_VB_ST::read() == 605);
    CHECK(HdBypass::HD_VB_SP::read() == 16);
    CHECK(HdBypass::HD_VS_ST::read() == 1);
    CHECK(HdBypass::HD_VS_SP::read() == 621);
}

TEST_CASE("progressive SD takes a fixed raster, not one off the divider")
{
    applyForStandard(3);

    CHECK(Adc::ADC_FLTR::read() == 2);  // the 70 MHz corner
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_CKOS::read() == 0);
    CHECK(HdBypass::HD_HB_ST::read() == 2148);
    CHECK(HdBypass::HD_HB_SP::read() == 160);
    CHECK(HdBypass::HD_VB_ST::read() == 0);
    CHECK(HdBypass::HD_VB_SP::read() == 64);
}

TEST_CASE("the two progressive standards differ in the sync pulse alone")
{
    applyForStandard(3);
    CHECK(HdBypass::HD_HS_ST::read() == 84);
    CHECK(HdBypass::HD_HS_SP::read() == 2148);
    CHECK(HdBypass::HD_VS_ST::read() == 6);
    CHECK(HdBypass::HD_VS_SP::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 8);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 10);

    applyForStandard(4);
    CHECK(HdBypass::HD_HS_ST::read() == 16);
    CHECK(HdBypass::HD_HS_SP::read() == 2176);
    CHECK(HdBypass::HD_VS_ST::read() == 6);
    CHECK(HdBypass::HD_VS_SP::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 48);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_H::read() == 0);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 46);
}

TEST_CASE("720p and 1080p replace the divider the switch wrote")
{
    applyForStandard(5);
    CHECK(Adc::PLLAD_MD::read() == 2474);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 550);

    applyForStandard(7);
    CHECK(Adc::PLLAD_MD::read() == 2749);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 0x710);
}

TEST_CASE("1080i keeps the divider and only widens its raster")
{
    applyForStandard(6);

    CHECK(Adc::PLLAD_MD::read() == DividerBeforeLadder);
    CHECK(HdBypass::HD_HSYNC_RST::read() == 0x710);
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_CKOS::read() == 0);
    CHECK(Adc::ADC_FLTR::read() == 1);  // the 110 MHz corner
}

TEST_CASE("the HD line carries the detail the widest ADC filter passes")
{
    // 720p and 1080p run the 150 MHz corner and one decimator, where 1080i's
    // half-rate line does not.
    applyForStandard(5);
    CHECK(Adc::ADC_FLTR::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK1X::read() == 0);
    CHECK(Adc::DEC2_BYPS::read() == 1);
    CHECK(Adc::PLLAD_ICP::read() == 6);
    CHECK(Adc::PLLAD_FS::read() == 1);

    applyForStandard(7);
    CHECK(Adc::ADC_FLTR::read() == 0);
    CHECK(Adc::ADC_CLK_ICLK1X::read() == 0);
    CHECK(Adc::DEC2_BYPS::read() == 1);
    CHECK(Adc::PLLAD_ICP::read() == 6);
    CHECK(Adc::PLLAD_FS::read() == 1);
}

TEST_CASE("each HD standard brings its own blanking and sync windows")
{
    applyForStandard(5);
    CHECK(HdBypass::HD_HB_ST::read() == 0);
    CHECK(HdBypass::HD_HB_SP::read() == 320);
    CHECK(HdBypass::HD_HS_ST::read() == 32);
    CHECK(HdBypass::HD_HS_SP::read() == 128);
    CHECK(HdBypass::HD_VB_ST::read() == 0);
    CHECK(HdBypass::HD_VB_SP::read() == 108);
    CHECK(HdBypass::HD_VS_ST::read() == 0);
    CHECK(HdBypass::HD_VS_SP::read() == 5);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 2);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 0);

    applyForStandard(6);
    CHECK(HdBypass::HD_HB_ST::read() == 0);
    CHECK(HdBypass::HD_HB_SP::read() == 184);
    CHECK(HdBypass::HD_HS_ST::read() == 4);
    CHECK(HdBypass::HD_HS_SP::read() == 80);
    CHECK(HdBypass::HD_VB_ST::read() == 0);
    CHECK(HdBypass::HD_VB_SP::read() == 30);
    CHECK(HdBypass::HD_VS_ST::read() == 4);
    CHECK(HdBypass::HD_VS_SP::read() == 9);
    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == 8);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == 6);

    applyForStandard(7);
    CHECK(HdBypass::HD_HB_ST::read() == 0);
    CHECK(HdBypass::HD_HB_SP::read() == 176);
    CHECK(HdBypass::HD_HS_ST::read() == 32);
    CHECK(HdBypass::HD_HS_SP::read() == 112);
    CHECK(HdBypass::HD_VB_ST::read() == 0);
    CHECK(HdBypass::HD_VB_SP::read() == 47);
    CHECK(HdBypass::HD_VS_ST::read() == 4);
    CHECK(HdBypass::HD_VS_SP::read() == 10);
}

TEST_CASE("1080p leaves the SD vertical window where it found it")
{
    // Standard 7 is the one HD arm that writes neither half of the pair, so a
    // ladder that wrote it anyway would show up here and nowhere else.
    applyForStandard(7);

    CHECK(SyncProcessor::SP_SDCS_VSST_REG_L::read() == Poison);
    CHECK(SyncProcessor::SP_SDCS_VSSP_REG_L::read() == Poison);
}

TEST_CASE("RGBHV arrives as RGB with every matrix out of circuit")
{
    applyForStandard(13);

    CHECK(rgbPatchCalls == 1);
    CHECK(SyncType::isCsync());
    CHECK(ColourSpace::DEC_MATRIX_BYPS::read() == 1);
    CHECK(HdBypass::HD_MATRIX_BYPS::read() == 1);
    CHECK(HdBypass::HD_DYN_BYPS::read() == 1);
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
