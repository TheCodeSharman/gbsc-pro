// Host-compiled unit tests for src/tv5725/Deinterlacer.cpp
// -- `make -C test deinterlacer`.
//
// This block is asserted as a BYTE IMAGE rather than field by field. The class
// replaced a 64-byte table, and the question that matters is whether any byte
// moved -- which 120 separate field assertions state less directly and less
// completely, since a field nobody thought to list is invisible to them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Deinterlacer.h"

using Tv5725::Deinterlacer;

static const uint8_t Poison = 0xA5;

// s2_00..s2_3f as the table shipped them.
static const uint8_t Expected[64] = {
    0xFF, 0x03, 0xEC, 0x00, 0xFF, 0xFF, 0x00, 0x1B,
    0x00, 0x70, 0x00, 0x00, 0x0F, 0x04, 0x7F, 0x14,
    0x18, 0x00, 0x8E, 0x00, 0x00, 0x00, 0x80, 0x00,
    0xC0, 0x61, 0x04, 0x15, 0x00, 0x00, 0x00, 0x10,
    0x30, 0x12, 0x04, 0x0F, 0x04, 0x00, 0x4C, 0x0C,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7F, 0x7F, 0x11, 0x10, 0x03, 0x0B,
    0x04, 0x44, 0x60, 0x04, 0x0F, 0x00, 0x00, 0x00,};

// The bits at each address that carry no field, which the class does not write
// and which therefore keep whatever was there. Five of them are 1 in the table:
// s2_02[3:2], s2_04[7], s2_05[7], s2_12[7] and s2_26[3:2], every one marked
// RESERVED in RD-5725-1.1's own bit table.
static const uint8_t Reserved[64] = {
    0x00, 0x00, 0x1E, 0xE0, 0x80, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x4F, 0x80, 0xC0, 0x80, 0x80, 0x00,
    0x00, 0x07, 0x80, 0x8B, 0x00, 0x00, 0x0C, 0x00,
    0x04, 0x02, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00,
    0x0C, 0xC8, 0xE0, 0xE0, 0xFB, 0xFF, 0x3F, 0x80,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0x80, 0xFF, 0xFF, 0xFF,};

struct Bench {
    Bench()
    {
        Wire.reset();
        Wire.poison(Poison);
        Deinterlacer::init();
    }
};

TEST_CASE("every documented bit of the block comes up as the table left it")
{
    Bench bench;

    for (int r = 0; r < 64; ++r) {
        const uint8_t owned = static_cast<uint8_t>(~Reserved[r]);
        CAPTURE(r);
        CHECK((Wire.bank[2][r] & owned) == (Expected[r] & owned));
    }
}

TEST_CASE("reserved bits are left alone rather than written zero")
{
    Bench bench;

    for (int r = 0; r < 64; ++r) {
        if (Reserved[r] == 0)
            continue;
        CAPTURE(r);
        CHECK((Wire.bank[2][r] & Reserved[r]) == (Poison & Reserved[r]));
    }
}

TEST_CASE("the deinterlacer stays inside segment 2")
{
    Bench bench;

    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 2)
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
    Bench bench;

    for (int r = 0; r < 256; ++r) {
        CAPTURE(r);
        CHECK(Wire.touched[2][r] == (r < 64 && Reserved[r] != 0xFF));
    }
}

TEST_CASE("the motion index fixed value is owned, despite sharing a datasheet name")
{
    // RD-5725-1.1 gives s2_0d and s2_0e the SAME name, MADPT_MI_THRESHOLD, for
    // two different functions. Keying an extraction by name loses one of them,
    // which is how s2_0e came to be absent from the register catalogue while
    // the table quietly wrote it 0x7f.
    Bench bench;

    CHECK(Wire.field(2, 0x0D, 0, 7) == 4);    // MADPT_MI_THRESHOLD
    CHECK(Wire.field(2, 0x0E, 0, 7) == 127);  // MADPT_MI_FIXED_VALUE
}
