// Host-compiled unit tests for src/tv5725/ModeDetect.cpp
// -- `make -C test mode-detect`.
//
// Same fake-Wire seam as test_hd_bypass.cpp: poison every bank, run init(),
// and ask the fake what was touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ModeDetect.h"

using Tv5725::ModeDetect;

static const uint8_t Poison = 0xA5;

struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        ModeDetect::init();
    }
};

TEST_CASE("every mode-detect threshold comes up at its bring-up value")
{
    // The thresholds Mode Detect compares a measured line against to name a
    // standard. getVideoMode() reads each one ONCE into a static and then
    // rewrites it dithered by random(-2,2) on every sweep, so what init()
    // establishes is the centre those dithered values wander around -- and if
    // it establishes nothing, the centre is whatever the last mode left.
    FreshChip chip;

    CHECK(Wire.field(1, 0x60, 0,  5) ==  22);  // MD_HPERIOD_LOCK_VALUE
    CHECK(Wire.field(1, 0x60, 5,  3) ==   5);  // MD_HPERIOD_UNLOCK_VALUE
    CHECK(Wire.field(1, 0x61, 0,  5) ==   4);  // MD_VPERIOD_LOCK_VALUE
    CHECK(Wire.field(1, 0x61, 5,  3) ==   4);  // MD_VPERIOD_UNLOCK_VALUE
    CHECK(Wire.field(1, 0x62, 0,  6) ==  32);  // MD_NTSC_INT_CNTRL
    CHECK(Wire.field(1, 0x62, 6,  2) ==   1);  // MD_WEN_CNTRL
    CHECK(Wire.field(1, 0x63, 0,  6) ==  38);  // MD_PAL_INT_CNTRL
    CHECK(Wire.field(1, 0x63, 6,  1) ==   0);  // MD_HS_FLIP
    CHECK(Wire.field(1, 0x63, 7,  1) ==   0);  // MD_VS_FLIP
    CHECK(Wire.field(1, 0x64, 0,  7) ==  65);  // MD_NTSC_PRG_CNTRL
    CHECK(Wire.field(1, 0x65, 0,  7) ==  62);  // MD_VGA_CNTRL
    CHECK(Wire.field(1, 0x65, 7,  1) ==   0);  // MD_SEL_VGA60
    CHECK(Wire.field(1, 0x66, 0,  8) == 178);  // MD_VGA_75HZ_CNTRL
    CHECK(Wire.field(1, 0x67, 0,  8) == 154);  // MD_VGA_85HZ_CNTRL
    CHECK(Wire.field(1, 0x68, 0,  7) ==  78);  // MD_V1250_VCNTRL
    CHECK(Wire.field(1, 0x69, 0,  8) == 214);  // MD_V1250_HCNTRL
    CHECK(Wire.field(1, 0x6A, 0,  8) == 177);  // MD_SVGA_60HZ_CNTRL
    CHECK(Wire.field(1, 0x6B, 0,  8) == 142);  // MD_SVGA_75HZ_CNTRL
    CHECK(Wire.field(1, 0x6C, 0,  8) == 124);  // MD_SVGA_85HZ_CNTRL
    CHECK(Wire.field(1, 0x6D, 0,  7) ==  99);  // MD_XGA_CNTRL
    CHECK(Wire.field(1, 0x6E, 0,  8) == 139);  // MD_XGA_60HZ_CNTRL
    CHECK(Wire.field(1, 0x6F, 0,  7) == 118);  // MD_XGA_70HZ_CNTRL
    CHECK(Wire.field(1, 0x70, 0,  7) == 112);  // MD_XGA_75HZ_CNTRL
    CHECK(Wire.field(1, 0x71, 0,  7) ==  98);  // MD_XGA_85HZ_CNTRL
    CHECK(Wire.field(1, 0x72, 0,  8) == 133);  // MD_SXGA_CNTRL
    CHECK(Wire.field(1, 0x73, 0,  7) == 105);  // MD_SXGA_60HZ_CNTRL
    CHECK(Wire.field(1, 0x74, 0,  7) ==  83);  // MD_SXGA_75HZ_CNTRL
    CHECK(Wire.field(1, 0x75, 0,  7) ==  72);  // MD_SXGA_85HZ_CNTRL
    CHECK(Wire.field(1, 0x76, 0,  7) ==  93);  // MD_HD720P_CNTRL
    CHECK(Wire.field(1, 0x77, 0,  8) == 148);  // MD_HD720P_60HZ_CNTRL
    CHECK(Wire.field(1, 0x78, 0,  8) == 178);  // MD_HD720P_50HZ_CNTRL
    CHECK(Wire.field(1, 0x79, 0,  7) ==  70);  // MD_HD1125I_CNTRL
    CHECK(Wire.field(1, 0x7A, 0,  8) == 198);  // MD_HD2200_1125I_CNTRL
    CHECK(Wire.field(1, 0x7B, 0,  8) == 238);  // MD_HD2640_1125I_CNTRL
    CHECK(Wire.field(1, 0x7C, 0,  8) == 140);  // MD_HD1125P_CNTRL
    CHECK(Wire.field(1, 0x7D, 0,  7) ==  98);  // MD_HD2200_1125P_CNTRL
    CHECK(Wire.field(1, 0x7E, 0,  7) == 118);  // MD_HD2640_1125P_CNTRL
    CHECK(Wire.field(1, 0x7F, 0,  8) ==  44);  // MD_HD1250P_CNTRL
    CHECK(Wire.field(1, 0x80, 0,  8) == 255);  // MD_USER_DEF_VCNTRL
    CHECK(Wire.field(1, 0x81, 0,  8) == 255);  // MD_USER_DEF_HCNTRL
    CHECK(Wire.field(1, 0x82, 0,  1) ==   1);  // MD_NOSYNC_DET_EN
    CHECK(Wire.field(1, 0x82, 1,  1) ==   0);  // MD_NOSYNC_USER_ID
    CHECK(Wire.field(1, 0x82, 2,  1) ==   1);  // MD_SW_DET_EN
    CHECK(Wire.field(1, 0x82, 3,  1) ==   0);  // MD_SW_USER_ID
    CHECK(Wire.field(1, 0x82, 4,  1) ==   0);  // MD_TIMER_DET_EN_H
    CHECK(Wire.field(1, 0x82, 5,  1) ==   0);  // MD_TIMER_DET_EN_V
    CHECK(Wire.field(1, 0x82, 6,  1) ==   0);  // MD_DET_BYPS_H
    CHECK(Wire.field(1, 0x82, 7,  1) ==   0);  // MD_H_USER_ID
    CHECK(Wire.field(1, 0x83, 0,  1) ==   0);  // MD_DET_BYPS_V
    CHECK(Wire.field(1, 0x83, 1,  1) ==   0);  // MD_V_USER_ID
    CHECK(Wire.field(1, 0x83, 2,  4) ==   3);  // MD_UNSTABLE_LOCK_VALUE
}

TEST_CASE("the sync type selects the VGA 60 Hz discriminator")
{
    // MD_SEL_VGA60 is not init() state: it follows rto->syncTypeCsync, which is
    // probed once per source. init() establishes the table's value and this
    // overrides it. docs/sync-type-selection.md.
    FreshChip chip;

    ModeDetect::applySyncType(ModeDetect::Csync);
    CHECK(Wire.field(1, 0x65, 7, 1) == 0);

    ModeDetect::applySyncType(ModeDetect::SeparateSync);
    CHECK(Wire.field(1, 0x65, 7, 1) == 1);
}

TEST_CASE("the medium-resolution line count is carried as a threshold")
{
    FreshChip chip;

    ModeDetect::applyMedResLineCount(0x33);
    CHECK(Wire.field(1, 0x7F, 0, 8) == 0x33);
}

TEST_CASE("neither runtime field disturbs its neighbours")
{
    // MD_SEL_VGA60 shares s1_65 with MD_VGA_CNTRL, which init() owns.
    FreshChip chip;

    ModeDetect::applySyncType(ModeDetect::SeparateSync);
    ModeDetect::applyMedResLineCount(0x33);

    CHECK(Wire.field(1, 0x65, 0, 7) == 62);  // MD_VGA_CNTRL, untouched
}

TEST_CASE("mode detect stays inside segment 1")
{
    FreshChip chip;

    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 1)
            continue;
        for (int r = 0; r < 256; ++r) {
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}

TEST_CASE("the block writes the addresses it owns and no others")
{
    FreshChip chip;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[1][r] == (r >= 0x60 && r <= 0x83));
    }
}

TEST_CASE("the top bit of a seven-bit threshold is left alone")
{
    // Thirteen of the thresholds are 7 bits wide with bit 7 carrying no
    // datasheet field. The blob wrote those bits 0 because it copied whole
    // banks; an undocumented bit gets no writer.
    FreshChip chip;

    const uint8_t sevenBit[] = {0x64, 0x68, 0x6D, 0x6F, 0x70, 0x71, 0x73,
                                0x74, 0x75, 0x76, 0x79, 0x7D, 0x7E};
    for (size_t i = 0; i < sizeof(sevenBit) / sizeof(sevenBit[0]); ++i) {
        CAPTURE(sevenBit[i]);
        CHECK(Wire.field(1, sevenBit[i], 7, 1) == ((Poison >> 7) & 1));
    }
    CHECK(Wire.field(1, 0x83, 6, 2) == ((Poison >> 6) & 0x3));
}
