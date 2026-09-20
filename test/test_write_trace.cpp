// Host-compiled unit tests for WriteTrace -- `make -C test write-trace`.
//
// The picture's position on the panel lands on one of two states per solve with
// every register identical, so the instrument has to be the SEQUENCE of writes
// and their spacing, and it has to be able to issue that sequence back at bus
// speed. An HTTP replay runs at tens of hertz through loop(), which is not the
// same event whatever bytes it carries.
// docs/investigations/the-pan-is-downstream-of-the-scaler.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/WriteTrace.h"

using Tv5725::WriteTrace;

static void record(uint32_t at, uint8_t reg, uint8_t first, uint8_t length = 1)
{
    uint8_t payload[WriteTrace::PayloadMax];
    for (uint8_t index = 0; index < length; ++index) payload[index] = first + index;
    WriteTrace::record(at, reg, payload, length);
}

TEST_CASE("nothing is recorded until the trace is armed")
{
    WriteTrace::stop();
    record(10, 0x40, 0xAA);

    CHECK(WriteTrace::count() == 0);
    CHECK(WriteTrace::seen() == 0);
}

TEST_CASE("an armed trace keeps what it was given, in order")
{
    WriteTrace::arm(100);
    record(110, 0xF0, 0x03);
    record(112, 0x04, 0x21, 2);

    REQUIRE(WriteTrace::count() == 1);
    CHECK(WriteTrace::at(0).reg == 0x04);
    CHECK(WriteTrace::at(0).segment == 3);
    CHECK(WriteTrace::at(0).length == 2);
    CHECK(WriteTrace::at(0).data[0] == 0x21);
    CHECK(WriteTrace::at(0).data[1] == 0x22);
    CHECK(WriteTrace::at(0).sinceArmMs == 12);
}

TEST_CASE("a segment select is the entries' segment, not an entry of its own")
{
    WriteTrace::arm(0);
    for (uint8_t pass = 0; pass < 4; ++pass) {
        record(pass, 0xF0, 0x02);
        record(pass, 0x10, pass);
    }

    CHECK(WriteTrace::count() == 4);
    CHECK(WriteTrace::seen() == 4);
    CHECK(WriteTrace::at(3).segment == 2);
}

TEST_CASE("arming again empties it")
{
    WriteTrace::arm(0);
    record(1, 0x10, 0x01);
    WriteTrace::arm(50);

    CHECK(WriteTrace::count() == 0);
    CHECK(WriteTrace::seen() == 0);
}

TEST_CASE("a full ring keeps the OLDEST, because the solve opens the sequence")
{
    WriteTrace::arm(0);
    for (uint16_t index = 0; index < WriteTrace::Capacity + 10; ++index)
        record(index, (uint8_t)(index % 0xF0), (uint8_t)index);

    CHECK(WriteTrace::count() == WriteTrace::Capacity);
    CHECK(WriteTrace::seen() == WriteTrace::Capacity + 10);
    CHECK(WriteTrace::overflowed());
    CHECK(WriteTrace::at(0).reg == 0x00);
}

TEST_CASE("a write wider than the payload is recorded as truncated")
{
    WriteTrace::arm(0);
    uint8_t block[16] = {0};
    WriteTrace::record(1, 0x00, block, sizeof(block));

    REQUIRE(WriteTrace::count() == 1);
    CHECK(WriteTrace::at(0).length == sizeof(block));
    CHECK(WriteTrace::at(0).truncated());
}

TEST_CASE("a replay issues the recorded bytes verbatim, to the same registers")
{
    Wire.reset();
    WriteTrace::arm(0);
    record(1, 0xF0, 0x03);
    record(2, 0x04, 0x21, 2);
    record(3, 0xF0, 0x01);
    record(4, 0x08, 0x55);

    WriteTrace::replay(0, WriteTrace::count());

    REQUIRE(Wire.trace.size() == 3);
    CHECK(Wire.trace[0].segment == 3);
    CHECK(Wire.trace[0].reg == 0x04);
    CHECK(Wire.trace[0].value == 0x21);
    CHECK(Wire.trace[1].segment == 3);
    CHECK(Wire.trace[1].reg == 0x05);
    CHECK(Wire.trace[1].value == 0x22);
    CHECK(Wire.trace[2].segment == 1);
    CHECK(Wire.trace[2].reg == 0x08);
    CHECK(Wire.trace[2].value == 0x55);
}

TEST_CASE("a replayed slice carries the segment it was recorded under")
{
    Wire.reset();
    WriteTrace::arm(0);
    record(1, 0xF0, 0x02);
    record(2, 0x10, 0x11);
    record(3, 0x11, 0x22);
    record(4, 0x12, 0x33);

    WriteTrace::replay(1, 2);

    REQUIRE(Wire.trace.size() == 1);
    CHECK(Wire.trace[0].segment == 2);
    CHECK(Wire.trace[0].reg == 0x11);
    CHECK(Wire.trace[0].value == 0x22);
}

TEST_CASE("a replay does not record itself")
{
    WriteTrace::arm(0);
    record(1, 0xF0, 0x02);
    record(2, 0x10, 0x11);
    const uint16_t held = WriteTrace::count();

    WriteTrace::replay(0, held);

    CHECK(WriteTrace::count() == held);
}

TEST_CASE("a rendered line is fixed width, so a byte offset names an entry")
{
    WriteTrace::arm(0);
    record(7, 0xF0, 0x00);
    record(7, 0x1F, 0x21, 2);

    char line[WriteTrace::LineBytes + 1];
    WriteTrace::line(line, 0);

    CHECK(std::string(line) == "0,000,00007,1F,02,2122....\n");
    CHECK(std::string(line).size() == WriteTrace::LineBytes);
}

TEST_CASE("a truncated entry renders its real length and no payload")
{
    WriteTrace::arm(0);
    uint8_t block[16] = {0};
    WriteTrace::record(3, 0x00, block, sizeof(block));

    char line[WriteTrace::LineBytes + 1];
    WriteTrace::line(line, 0);

    CHECK(std::string(line) == "0,000,00003,00,16,........\n");
}
