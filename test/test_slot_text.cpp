// Host-compiled unit tests for Tv5725::SlotText -- `make -C test slot-text`.
//
// The slot table as lines of text. Pure, so the format's tolerance rules -- a
// malformed line is skipped rather than fatal, a truncated record is refused
// rather than written -- are checked here rather than against a filesystem.
// docs/framing-presets.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SlotText.h"

float getSourceFieldRate(bool) { return 0.0f; }
void tv5725Log(const char *) {}

using namespace Tv5725;

static const SourceKey Bench(311, 50.08f);
static const PanAndZoom Wide(0.0364f, 0.8525f, 0.0740f, 0.8553f);

TEST_CASE("a record survives being written out and read back")
{
    SlotTable written;
    REQUIRE(written.remember(3, Bench, Wide));

    char line[72];
    REQUIRE(SlotText(written).writeLine(0, line, sizeof(line)));

    SlotTable read;
    SlotText(read).readLine(line);

    PanAndZoom back;
    REQUIRE(read.find(3, Bench, &back));
    CHECK(back.originOn(AxisHorizontal) == doctest::Approx(0.0364f).epsilon(0.001));
    CHECK(back.extentOn(AxisVertical) == doctest::Approx(0.8553f).epsilon(0.001));
}

TEST_CASE("a whole file of records reads back")
{
    const char *file[] = {
        "# slot framings",
        "",
        "3 311@50 = 364 8525 740 8553",
        "4 525@60 = 1000 7000 500 9000",
    };
    SlotTable read;
    for (const char *line : file)
        SlotText(read).readLine(line);

    CHECK(read.count() == 2);
    CHECK(read.find(3, Bench, 0));
    CHECK(read.find(4, SourceKey(525, 60.0f), 0));
}

TEST_CASE("a line that is not a record is skipped, not fatal")
{
    SlotTable read;
    SlotText text(read);

    text.readLine("3 311@50 = 364 8525 740 8553");
    for (const char *bad : {"311@50 = 364 8525 740 8553",  // no slot
                            "3 311@50 = 364 8525 740",     // short
                            "3 311 50 = 364 8525 740 8553",// no @
                            "3 311@50 364 8525 740 8553",  // no =
                            "3 a@b = c d e f",
                            "4096 311@50 = 364 8525 740 8553",
                            "3 0@0 = 364 8525 740 8553"})
        text.readLine(bad);

    CHECK(read.count() == 1);
    CHECK(read.find(3, Bench, 0));
}

TEST_CASE("a record that would not fit is refused rather than truncated")
{
    SlotTable written;
    REQUIRE(written.remember(3, Bench, Wide));

    char line[16];
    CHECK_FALSE(SlotText(written).writeLine(0, line, sizeof(line)));

    SUBCASE("and so is one with no room for even the slot") {
        char tiny[1];
        CHECK_FALSE(SlotText(written).writeLine(0, tiny, sizeof(tiny)));
        CHECK(tiny[0] == '\0');
    }

    SUBCASE("and so is an index no record has") {
        char big[72];
        CHECK_FALSE(SlotText(written).writeLine(1, big, sizeof(big)));
    }
}

TEST_CASE("the slot survives the round trip whatever its number")
{
    for (unsigned slot = 0; slot < 72; ++slot) {
        SlotTable written;
        REQUIRE(written.remember((uint8_t)slot, Bench, Wide));

        char line[72];
        REQUIRE(SlotText(written).writeLine(0, line, sizeof(line)));

        SlotTable read;
        SlotText(read).readLine(line);
        CHECK(read.find((uint8_t)slot, Bench, 0));
    }
}
