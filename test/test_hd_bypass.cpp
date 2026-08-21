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

struct Bench {
    Bench()
    {
        Wire.reset();
        Wire.poison(Poison);
        HdBypass::init();
    }
};

TEST_CASE("the input pipe and both converters are in circuit")
{
    Bench bench;

    CHECK(Wire.field(1, 0x30, 0, 1) == 0);  // HD_IN_DREG_BYPS
    CHECK(Wire.field(1, 0x30, 1, 1) == 0);  // HD_MATRIX_BYPS
    CHECK(Wire.field(1, 0x30, 2, 1) == 0);  // HD_DYN_BYPS
    CHECK(Wire.field(1, 0x30, 3, 1) == 0);  // HD_SEL_BLK_IN
}

TEST_CASE("the dynamic range passes the sample through unchanged")
{
    Bench bench;

    CHECK(Wire.field(1, 0x31, 0, 8) == 128);  // HD_Y_GAIN
    CHECK(Wire.field(1, 0x32, 0, 8) == 0);    // HD_Y_OFFSET
    CHECK(Wire.field(1, 0x33, 0, 8) == 128);  // HD_U_GAIN
    CHECK(Wire.field(1, 0x34, 0, 8) == 0);    // HD_U_OFFSET
    CHECK(Wire.field(1, 0x35, 0, 8) == 128);  // HD_V_GAIN
    CHECK(Wire.field(1, 0x36, 0, 8) == 0);    // HD_V_OFFSET
}

TEST_CASE("the bypass raster comes up at the resting timing")
{
    Bench bench;

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
    Bench bench;

    CHECK(Wire.field(1, 0x4B, 0, 12) == 0);  // HD_EXT_VB_ST
    CHECK(Wire.field(1, 0x4D, 0, 12) == 6);  // HD_EXT_VB_SP
    CHECK(Wire.field(1, 0x4F, 0, 12) == 0);  // HD_EXT_HB_ST
    CHECK(Wire.field(1, 0x51, 0, 12) == 6);  // HD_EXT_HB_SP
}

TEST_CASE("the programmed blank is black on all three channels")
{
    Bench bench;

    CHECK(Wire.field(1, 0x53, 0, 8) == 0);  // HD_BLK_GY_DATA
    CHECK(Wire.field(1, 0x54, 0, 8) == 0);  // HD_BLK_BU_DATA
    CHECK(Wire.field(1, 0x55, 0, 8) == 0);  // HD_BLK_RV_DATA
}

TEST_CASE("the HD bypass block stays inside segment 1")
{
    Bench bench;

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
    // s1_56..s1_5f carry no datasheet field. The blob wrote them 0x00 along
    // with the rest of its three banks; an address with no documented meaning
    // gets no writer, per docs/chip-initialisation.md.
    Bench bench;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[1][r] == (r >= 0x30 && r <= 0x55));
    }
}
