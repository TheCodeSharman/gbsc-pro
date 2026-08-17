// Host-compiled unit tests for the segment pointer in tw.h -- `make -C test
// segment-select`.
//
// The TV5725 has one 256-register window and a pointer at 0xF0 selecting which
// of six banks it shows. Every register access is therefore two steps: aim the
// pointer, then read or write. tw::SegmentedSlave::setSeg() caches where it
// last aimed and skips the write when the segment has not changed.
//
// That cache is never revalidated, so it is only as good as the assumption that
// nothing else can move the pointer. When something does, whole segments swap:
// measured 2026-08-09 with segment 1 holding segment 3's bytes at the same
// offsets and segment 3 holding segment 1's back.
//
// These tests do not model how the pointer gets disturbed. They pin the
// consequence every candidate shares: once it moves, nothing puts it back.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Tv5725.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

// Named through their owning subsystems rather than the GBS mixin, so this test
// does not need editing every time a block migrates.
using Tv5725::InputFormatter;
using Tv5725::VideoProcessor;

// Two registers at the same offset in different banks -- the pair the bench
// fault actually swapped. IF_HB_ST is the input formatter's set 0 horizontal
// blanking start; VDS_DIS_HB_ST is the display window's. Both live at 0x10.
static const uint8_t SharedOffset = 0x10;

TEST_CASE("a register write lands in that register's own segment")
{
    Wire.reset();

    SUBCASE("when nothing has disturbed the segment pointer") {
        InputFormatter::IF_HB_ST::write(258);
        VideoProcessor::VDS_DIS_HB_ST::write(1348);

        CHECK(Wire.bank[1][SharedOffset] == (258 & 0xFF));
        CHECK(Wire.bank[3][SharedOffset] == (1348 & 0xFF));
    }

    SUBCASE("even when the pointer moved after the last access") {
        // Establish the cache, then move the slave's pointer behind the
        // library's back. This stands in for whatever really moves it; the
        // point is that the library has no way to know it happened.
        InputFormatter::IF_HB_ST::write(258);
        Wire.segment = 3;

        InputFormatter::IF_HB_ST::write(2);

        CHECK(Wire.bank[1][SharedOffset] == 2);
        CHECK(Wire.bank[3][SharedOffset] == 0);
    }

    SUBCASE("and the next access after a disturbance is not a free pass") {
        // The first access after the pointer moves is the one that lands wrong,
        // and it stays wrong for as long as the caller keeps using the same
        // segment -- the cache agrees with itself every time. Two writes, so a
        // fix that only happens to re-aim once cannot pass.
        VideoProcessor::VDS_DIS_HB_ST::write(1348);
        Wire.segment = 1;

        VideoProcessor::VDS_DIS_HB_ST::write(1300);
        VideoProcessor::VDS_DIS_HB_ST::write(1262);

        CHECK(Wire.bank[3][SharedOffset] == (1262 & 0xFF));
        CHECK(Wire.bank[1][SharedOffset] == 0);
    }
}

TEST_CASE("a register read comes from that register's own segment")
{
    Wire.reset();

    // Distinct values at the same offset, so a read from the wrong bank cannot
    // pass by coincidence.
    Wire.bank[1][SharedOffset] = 0x02;
    Wire.bank[3][SharedOffset] = 0x44;

    InputFormatter::IF_HB_ST::read();
    Wire.segment = 3;

    CHECK((InputFormatter::IF_HB_ST::read() & 0xFF) == 0x02);
}

TEST_CASE("a register declared by its owning subsystem still aims its own segment")
{
    // The catalogue is migrating out of Tv5725::Tv5725 into the subsystem that
    // owns each block. A field carries its segment in its own type, so moving
    // the declaration to another class must not change where a write lands --
    // and a segment mis-transcribed during the move is silent everywhere else.
    Wire.reset();

    VideoProcessor::VDS_HSCALE::write(512);

    CHECK(Wire.bank[3][0x16] == (512 & 0xFF));
    CHECK(VideoProcessor::VDS_HSCALE::read() == 512);
    CHECK(Wire.bank[1][0x16] == 0);
}
