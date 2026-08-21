// Host-compiled unit tests for src/tv5725/FrameBuffer.cpp -- `make -C test frame-buffer`.
//
// Same seam as test_memory_bus.cpp: poison every bank, run init(), and ask the
// fake which registers were touched. A field this subsystem forgets is supplied
// by whatever loaded underneath it, so the omission is invisible on the bench
// and an assertion here. docs/testing.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameBuffer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/MemoryMap.h"

using Tv5725::FrameBuffer;
using Tv5725::MemoryMap;

// **EVERY owned field's value must differ from what the poison leaves in its
// bits**, or dropping that field's write survives the suite. Bit 1 set, bits 3,
// 4 and 5 clear, and the low six bits equal to none of 24, 61, 36 or 60. 0xC2
// satisfies all of it, and as a 21-bit address field it reads 0x02C2C2, which is
// none of the four the memory map writes. docs/testing.md
static const uint8_t Poison = 0xC2;

struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        FrameBuffer::init();
    }
};

TEST_CASE("the memory map is written, not left to a preset")
{
    FreshChip chip;

    CHECK(Wire.field(4, 0x51, 0, 21) == MemoryMap::FieldStoreStart);
    CHECK(Wire.field(4, 0x44, 0, 21) == MemoryMap::FieldStoreGuard);
    CHECK(Wire.field(4, 0x47, 0, 21) == MemoryMap::FieldStoreGuard);
    CHECK(Wire.field(4, 0x31, 0, 21) == MemoryMap::CaptureStart);
    CHECK(Wire.field(4, 0x34, 0, 21) == MemoryMap::CaptureStart);
    CHECK(Wire.field(4, 0x24, 0, 21) == MemoryMap::CaptureGuard);
    CHECK(Wire.field(4, 0x27, 0, 21) == MemoryMap::CaptureGuard);
}

TEST_CASE("both bounds are armed, including the one no preset ever switched on")
{
    FreshChip chip;

    CHECK(Wire.field(4, 0x42, 3, 1) == 1);  // WFF_SAFE_GUARD, live in all twelve
    CHECK(Wire.field(4, 0x21, 5, 1) == 1);  // CAP_SAFE_GUARD_EN, 0 in all twelve
}

TEST_CASE("the FIFO request watermarks are owned, and are one value for every mode")
{
    FreshChip chip;

    // MASTER_FLAG sets the FIFO's HIGH request timing and GENERAL_FLAG its LOW
    // (RD-5725-1.1, s4_2c/2d and s4_4e/4f). The datasheet gives no units and no
    // formula, so these are carried as constants -- see FrameBuffer.cpp for why
    // these constants and not the eleven other sets the tables offer.
    CHECK(Wire.field(4, 0x2C, 0, 6) == 24);  // PB_MAST_FLAG_REG
    CHECK(Wire.field(4, 0x2D, 0, 6) == 61);  // PB_GENERAL_FLAG_REG
    CHECK(Wire.field(4, 0x4E, 0, 6) == 36);  // RFF_MASTER_FLAG
    CHECK(Wire.field(4, 0x4F, 0, 6) == 60);  // RFF_GENERAL_FLAG
}

TEST_CASE("a low request watermark of zero can never be written")
{
    FreshChip chip;

    // Two of the twelve tables carry PB_GENERAL_FLAG_REG = 0 where the other ten
    // carry 58-61. A zero low-request watermark is not a tuning choice, so
    // whatever the value ends up being, it is not zero.
    CHECK(Wire.field(4, 0x2D, 0, 6) != 0);
    CHECK(Wire.field(4, 0x4F, 0, 6) != 0);
}

TEST_CASE("the write FIFO's two mode-dependent bits are owned")
{
    FreshChip chip;

    CHECK(Wire.field(4, 0x42, 1, 1) == 0);  // WFF_FF_HALF_REQ
    CHECK(Wire.field(4, 0x4A, 4, 1) == 1);  // WFF_LINE_FLIP
}

TEST_CASE("the deinterlacer's FIFO settings are owned with the deinterlacer off")
{
    FreshChip chip;

    // Four fields whose only other writers are the motion-adaptive
    // deinterlacer's enable and disable, neither of which runs on a progressive
    // source -- so a writer existed somewhere and none on the path that needed
    // it. The values are not merely the tables': RFF_FETCH_NUM 1 and
    // WFF_FF_STA_INV 1 are what disableMotionAdaptDeinterlace() itself writes,
    // and the one table disagreeing is the one carrying deinterlacer-on values.
    CHECK(Wire.field(4, 0x4D, 4, 1) == 1);   // RFF_ADR_ADD_2
    CHECK(Wire.field(4, 0x4D, 5, 2) == 3);   // RFF_REQ_SEL
    CHECK(Wire.field(4, 0x59, 0, 10) == 1);  // RFF_FETCH_NUM
    CHECK(Wire.field(4, 0x42, 2, 1) == 1);   // WFF_FF_STA_INV
}

TEST_CASE("neighbours sharing a byte survive the fields beside them")
{
    FreshChip chip;

    // Do six fields written one at a time into one byte all survive? A whole-byte
    // write carrying one field's value clears its neighbours, and
    // read-modify-write is what prevents it. s4_42 carries WFF_SAFE_GUARD at bit
    // 3 and WFF_FF_HALF_REQ at bit 1 from the derived half, and four more at bits
    // 4-7 from the transcribed one.
    CHECK(Wire.field(4, 0x42, 3, 1) == 1);
    CHECK(Wire.field(4, 0x42, 1, 1) == 0);
    CHECK(Wire.field(4, 0x42, 0, 1) == ((Poison >> 0) & 1));  // WFF_ENABLE, nobody's
    CHECK(Wire.field(4, 0x42, 4, 1) == 0);  // WFF_VRST_FF_RST
    CHECK(Wire.field(4, 0x42, 5, 1) == 1);  // WFF_ADR_ADD_2
    CHECK(Wire.field(4, 0x42, 6, 1) == 1);  // WFF_REQ_OVER
    CHECK(Wire.field(4, 0x42, 7, 1) == 0);  // WFF_FF_STATUS_SEL

    // s4_21: CAP_SAFE_GUARD_EN is bit 5, the rest came over with the
    // graduation. Bits 0 and 4 are nobody's and must stay at the poison.
    CHECK(Wire.field(4, 0x21, 5, 1) == 1);
    CHECK(Wire.field(4, 0x21, 1, 1) == 1);  // CAP_FF_HALF_REQ
    CHECK(Wire.field(4, 0x21, 2, 1) == 0);  // CAP_BUF_STA_INV
    CHECK(Wire.field(4, 0x21, 3, 1) == 0);  // CAP_DOUBLE_BUFFER
    CHECK(Wire.field(4, 0x21, 6, 1) == 1);  // CAP_VRST_FFRST_EN
    CHECK(Wire.field(4, 0x21, 7, 1) == 0);  // CAP_ADR_ADD_2
    CHECK(Wire.field(4, 0x21, 0, 1) == ((Poison >> 0) & 1));
    CHECK(Wire.field(4, 0x21, 4, 1) == ((Poison >> 4) & 1));

    // s4_4a: WFF_LINE_FLIP at bit 4 is derived, WFF_YUV_DEINTERLACE at bit 0
    // and WFF_LAST_POP_CTL at bit 7 came over with the graduation.
    CHECK(Wire.field(4, 0x4A, 4, 1) == 1);
    CHECK(Wire.field(4, 0x4A, 0, 1) == 0);
    CHECK(Wire.field(4, 0x4A, 7, 1) == 0);
}

TEST_CASE("every register the subsystem owns is actually written")
{
    FreshChip chip;

    // Asked of the fake, not inferred from the value: a field whose owned value
    // equals the poison reads correct having never been touched. It does NOT
    // cover a field sharing a byte with another owned field -- s4_42, s4_21 and
    // s4_4a are touched by a neighbour regardless -- and those are covered by
    // value instead. docs/testing.md
    static const uint8_t Owned[] = {
        0x21,                          // CAP_SAFE_GUARD_EN
        0x24, 0x25, 0x26,              // CAP_SAFE_GUARD_A
        0x27, 0x28, 0x29,              // CAP_SAFE_GUARD_B
        0x2C, 0x2D,                    // PB_MAST_FLAG_REG, PB_GENERAL_FLAG_REG
        0x31, 0x32, 0x33,              // PB_CAP_BUF_STA_ADDR_A
        0x34, 0x35, 0x36,              // PB_CAP_BUF_STA_ADDR_B
        0x42,                          // WFF_SAFE_GUARD, WFF_FF_HALF_REQ
        0x44, 0x45, 0x46,              // WFF_SAFE_GUARD_A
        0x47, 0x48, 0x49,              // WFF_SAFE_GUARD_B
        0x4A,                          // WFF_LINE_FLIP
        0x4E, 0x4F,                    // RFF_MASTER_FLAG, RFF_GENERAL_FLAG
        0x51, 0x52, 0x53,              // RFF_WFF_STA_ADDR_A
        0x59, 0x5A,                    // RFF_FETCH_NUM
    };
    for (size_t i = 0; i < sizeof(Owned) / sizeof(Owned[0]); ++i) {
        CAPTURE(Owned[i]);
        CHECK(Wire.touched[4][Owned[i]]);
    }
}

TEST_CASE("the frame buffer stays inside segment 4")
{
    FreshChip chip;

    // Every field here is in the SDRAM controller's bank. A write landing
    // elsewhere means the segment pointer was misaimed, which is the failure
    // this fake exists to catch and which no value-based assertion can see.
    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 4) continue;
        for (int r = 0; r < 256; ++r) {
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}
