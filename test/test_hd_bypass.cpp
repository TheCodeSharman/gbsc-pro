// Host-compiled unit tests for src/tv5725/HdBypass.cpp
// -- `make -C test hd-bypass`.
//
// Same fake-Wire seam as test_input_formatter.cpp: poison every bank, run
// init(), and ask the fake what was touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/HdBypass.h"

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
