// Host-compiled unit tests for src/tv5725/InputFormatter.cpp
// -- `make -C test input-formatter`.
//
// Same fake-Wire seam as test_memory_bus.cpp and test_frame_buffer.cpp: poison
// every bank, run init(), and ask the fake what was touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"

using Tv5725::InputFormatter;

// Leaves IF_LD_ST reading 1, which is neither the 5 this writes nor the 3 the
// NTSC tables carry, so the assertion below can fail.
static const uint8_t Poison = 0xC2;

struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        InputFormatter::init();
    }
};

TEST_CASE("the line double write reset position is owned, one value for every mode")
{
    FreshChip chip;

    CHECK(Wire.field(1, 0x0C, 1, 4) == 5);  // IF_LD_ST
    CHECK(Wire.touched[1][0x0C]);
}

TEST_CASE("the rest of s1_0c is left to its own owners")
{
    FreshChip chip;

    // IF_LD_RAM_BYPS is bit 0 and IF_INI_ST is bits 7-5 of the same byte, and
    // both are written by doPostPresetLoadSteps(). Read-modify-write is what
    // keeps three owners in one byte from clobbering each other, and this is
    // the assertion that says so rather than assuming it.
    CHECK(Wire.field(1, 0x0C, 0, 1) == ((Poison >> 0) & 0x1));
    CHECK(Wire.field(1, 0x0C, 5, 3) == ((Poison >> 5) & 0x7));
}

TEST_CASE("the input formatter stays inside segment 1")
{
    // The input formatter is segment 1, so a write anywhere else is a wrong-bank
    // write, which is the defect the fake bus exists to catch.
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

TEST_CASE("the input formatter writes the addresses it owns and no others")
{
    // The three blanking sets split three ways, and this list is where that is
    // stated. Set 2 at s1_18/s1_1a is the capture window and the ENGINE's, so it
    // is absent along with IF_LINE_SP. Set 0 at s1_10/s1_12 and the scale-down
    // pair at s1_24/s1_26 are constants with no scaling-path writer, so they are
    // here. Set 1 at s1_14/s1_16 is measured inert and belongs to nobody, which
    // test_bringup.cpp asserts.
    FreshChip chip;

    const uint8_t owned[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0A, 0x0B, 0x0C,
                             0x10, 0x11, 0x12, 0x13,
                             0x24, 0x25, 0x26, 0x27, 0x28};
    bool expected[256] = {false};
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i)
        expected[owned[i]] = true;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[1][r] == expected[r]);
    }
}

// --- the scan mode, which is four registers deciding one thing ----------------

TEST_CASE("a line-doubled source undoes every progressive setting")
{
    FreshChip chip;

    InputFormatter::applyScanMode(InputFormatter::Progressive);
    InputFormatter::applyScanMode(InputFormatter::LineDoubled);

    CHECK(Wire.field(1, 0x0B, 4, 2) == 1);  // IF_HS_DEC_FACTOR
    CHECK(Wire.field(1, 0x0B, 7, 1) == 0);  // IF_LD_SEL_PROV
    CHECK(Wire.field(1, 0x0C, 0, 1) == 0);  // IF_LD_RAM_BYPS
    CHECK(Wire.field(1, 0x00, 6, 1) == 0);  // IF_PRGRSV_CNTRL
}

TEST_CASE("a progressive source undoes every line-doubled setting")
{
    FreshChip chip;

    InputFormatter::applyScanMode(InputFormatter::LineDoubled);
    InputFormatter::applyScanMode(InputFormatter::Progressive);

    CHECK(Wire.field(1, 0x0B, 4, 2) == 0);  // IF_HS_DEC_FACTOR
    CHECK(Wire.field(1, 0x0B, 7, 1) == 1);  // IF_LD_SEL_PROV
    CHECK(Wire.field(1, 0x0C, 0, 1) == 1);  // IF_LD_RAM_BYPS
    CHECK(Wire.field(1, 0x00, 6, 1) == 1);  // IF_PRGRSV_CNTRL
}
