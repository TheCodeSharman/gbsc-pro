// Host-compiled unit tests for Tv5725::SlotTable -- `make -C test slot-table`.
//
// The framing a user stored in a numbered slot, against the source it was
// stored for. Pure, so every rule about the table rather than about flash is
// checked here. docs/framing-presets.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SlotTable.h"

float getSourceFieldRate(bool) { return 0.0f; }
void tv5725Log(const char *) {}

using namespace Tv5725;

static const SourceKey Bench(311, 50.08f);
static const SourceKey Vga(525, 59.94f);

static const PanAndZoom Wide(0.0364f, 0.8525f, 0.0740f, 0.8553f);
static const PanAndZoom Narrow(0.2000f, 0.5000f, 0.1000f, 0.7000f);

TEST_CASE("a framing stored in a slot comes back from that slot")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));

    PanAndZoom back;
    REQUIRE(table.find(3, Bench, &back));
    CHECK(back == Wide);
}

TEST_CASE("a slot that was never stored holds nothing")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));

    CHECK_FALSE(table.find(4, Bench, 0));
}

TEST_CASE("two slots keep their own framing for the same source")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));
    REQUIRE(table.remember(4, Bench, Narrow));

    PanAndZoom back;
    REQUIRE(table.find(3, Bench, &back));
    CHECK(back == Wide);
    REQUIRE(table.find(4, Bench, &back));
    CHECK(back == Narrow);
}

TEST_CASE("one slot keeps a framing per source")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));
    REQUIRE(table.remember(3, Vga, Narrow));

    PanAndZoom back;
    REQUIRE(table.find(3, Vga, &back));
    CHECK(back == Narrow);
    REQUIRE(table.find(3, Bench, &back));
    CHECK(back == Wide);
}

TEST_CASE("storing over a slot replaces its record rather than adding one")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));
    REQUIRE(table.remember(3, Bench, Narrow));

    CHECK(table.count() == 1);
    PanAndZoom back;
    REQUIRE(table.find(3, Bench, &back));
    CHECK(back == Narrow);
}

TEST_CASE("a source no chip could measure is refused")
{
    SlotTable table;
    CHECK_FALSE(table.remember(3, SourceKey(), Wide));
    CHECK(table.count() == 0);
}

TEST_CASE("a full table refuses a new record and keeps every one it has")
{
    SlotTable table;
    for (uint16_t i = 0; i < SlotTable::Records; ++i)
        REQUIRE(table.remember((uint8_t)i, Bench, Wide));

    CHECK_FALSE(table.remember(SlotTable::Records, Bench, Wide));
    CHECK(table.count() == SlotTable::Records);
    CHECK(table.find(0, Bench, 0));

    SUBCASE("but re-storing one it already holds still lands") {
        CHECK(table.remember(0, Bench, Narrow));
        PanAndZoom back;
        REQUIRE(table.find(0, Bench, &back));
        CHECK(back == Narrow);
    }
}

TEST_CASE("forgetting a slot drops every source it held")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));
    REQUIRE(table.remember(3, Vga, Narrow));
    REQUIRE(table.remember(4, Bench, Narrow));

    CHECK(table.forget(3));
    CHECK_FALSE(table.find(3, Bench, 0));
    CHECK_FALSE(table.find(3, Vga, 0));

    SUBCASE("and leaves the other slots alone") {
        PanAndZoom back;
        REQUIRE(table.find(4, Bench, &back));
        CHECK(back == Narrow);
    }

    SUBCASE("forgetting one it does not hold says so") {
        CHECK_FALSE(table.forget(3));
    }
}

TEST_CASE("every record is reachable for whoever writes the file")
{
    SlotTable table;
    REQUIRE(table.remember(3, Bench, Wide));
    REQUIRE(table.remember(4, Vga, Narrow));

    REQUIRE(table.count() == 2);
    for (uint16_t i = 0; i < table.count(); ++i) {
        PanAndZoom back;
        REQUIRE(table.find(table.slotAt(i), table.keyAt(i), &back));
        CHECK(back == table.framingAt(i));
    }
}
