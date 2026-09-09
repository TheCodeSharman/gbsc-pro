// Host-compiled unit tests for Tv5725::FramingTable -- `make -C test framing-table`.
//
// The framing a user tuned, kept against the source it was tuned for. Pure: no
// bus, no filesystem, so every rule in docs/framing-presets.md that is about
// the table rather than about flash is checked here.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FramingTable.h"

float getSourceFieldRate(bool) { return 0.0f; }
void tv5725Log(const char *) {}

using namespace Tv5725;

static const SourceKey Bench(311, 50.08f);
static const PanAndZoom Framed(0.0364f, 0.8525f, 0.0740f, 0.8553f);

TEST_CASE("a source nobody has framed has no entry")
{
    FramingTable table;

    CHECK_FALSE(table.find(Bench, 0));
}

TEST_CASE("what was remembered comes back")
{
    FramingTable table;
    REQUIRE(table.remember(Bench, Framed));

    PanAndZoom found;
    REQUIRE(table.find(Bench, &found));
    CHECK(found == Framed);
}

TEST_CASE("the same source remembered twice replaces its entry")
{
    // Otherwise re-tuning a source fills the table with its own history and
    // evicts everything else.
    FramingTable table;
    const PanAndZoom retuned(0.1f, 0.5f, 0.2f, 0.6f);

    REQUIRE(table.remember(Bench, Framed));
    REQUIRE(table.remember(Bench, retuned));

    PanAndZoom found;
    REQUIRE(table.find(Bench, &found));
    CHECK(found == retuned);
    CHECK(table.count() == 1);
}

TEST_CASE("a source the key cannot identify is not stored")
{
    // A settling source passes through line counts inside no standard at all,
    // and an entry keyed on one of those would be recalled by the next source
    // that happens to settle through it.
    FramingTable table;

    CHECK_FALSE(table.remember(SourceKey(97, 50.08f), Framed));
    CHECK(table.count() == 0);
}

TEST_CASE("adjacent sources keep their own framings")
{
    FramingTable table;
    const SourceKey sixty(311, 60.05f);
    const PanAndZoom other(0.2f, 0.5f, 0.1f, 0.7f);

    REQUIRE(table.remember(Bench, Framed));
    REQUIRE(table.remember(sixty, other));

    PanAndZoom found;
    REQUIRE(table.find(Bench, &found));
    CHECK(found == Framed);
    REQUIRE(table.find(sixty, &found));
    CHECK(found == other);
}

TEST_CASE("a full table refuses rather than discarding a tuning")
{
    // Silently dropping the oldest entry loses work the user did, with nothing
    // said. Refusing is visible and the user can clear one.
    FramingTable table;
    for (uint16_t i = 0; i < FramingTable::Entries; ++i)
        REQUIRE(table.remember(SourceKey((uint16_t)(200 + i), 50.0f), Framed));

    CHECK(table.count() == FramingTable::Entries);
    CHECK_FALSE(table.remember(SourceKey(900, 50.0f), Framed));

    // And every earlier tuning is still there.
    PanAndZoom found;
    CHECK(table.find(SourceKey(200, 50.0f), &found));
}

TEST_CASE("a full table still takes a re-tune of a source already in it")
{
    FramingTable table;
    for (uint16_t i = 0; i < FramingTable::Entries; ++i)
        REQUIRE(table.remember(SourceKey((uint16_t)(200 + i), 50.0f), Framed));

    const PanAndZoom retuned(0.1f, 0.5f, 0.2f, 0.6f);
    CHECK(table.remember(SourceKey(200, 50.0f), retuned));
}

TEST_CASE("forgetting a source frees its place")
{
    FramingTable table;
    REQUIRE(table.remember(Bench, Framed));

    CHECK(table.forget(Bench));
    CHECK(table.count() == 0);
    CHECK_FALSE(table.find(Bench, 0));
}

// --- the revision -----------------------------------------------------------
//
// What says a flash write is owed. A press must not write flash, so the caller
// debounces on this rather than paying for a read every tick -- and BOTH the
// engine and whoever loads the file mutate the table, so a counter held by
// either one misses the other's change.

TEST_CASE("a fresh table has not moved")
{
    FramingTable table;
    FramingTable other;

    CHECK(table.revision() == other.revision());
}

TEST_CASE("remembering, replacing and forgetting each move the revision")
{
    FramingTable table;
    const uint16_t fresh = table.revision();

    REQUIRE(table.remember(Bench, Framed));
    const uint16_t stored = table.revision();
    CHECK(stored != fresh);

    // Re-tuning the same source replaces the entry rather than adding one, and
    // the file still owes a write.
    REQUIRE(table.remember(Bench, PanAndZoom()));
    const uint16_t retuned = table.revision();
    CHECK(retuned != stored);

    REQUIRE(table.forget(Bench));
    CHECK(table.revision() != retuned);
}

TEST_CASE("a refused change does not move the revision")
{
    // Nothing was stored, so nothing is owed. A revision that moved anyway
    // would make every rejected press cost a flash write.
    FramingTable table;
    REQUIRE(table.remember(Bench, Framed));
    const uint16_t stored = table.revision();

    CHECK_FALSE(table.remember(SourceKey(), Framed));
    CHECK(table.revision() == stored);

    CHECK_FALSE(table.forget(SourceKey(0, 0.0f)));
    CHECK(table.revision() == stored);
}

TEST_CASE("a full table refuses a new source without moving the revision")
{
    FramingTable table;
    for (uint16_t i = 0; i < FramingTable::Entries; ++i)
        REQUIRE(table.remember(SourceKey((uint16_t)(200 + i), 50.0f), Framed));
    const uint16_t full = table.revision();

    CHECK_FALSE(table.remember(SourceKey(999, 50.0f), Framed));
    CHECK(table.revision() == full);
}

TEST_CASE("clearing a table that held something moves the revision")
{
    FramingTable table;
    REQUIRE(table.remember(Bench, Framed));
    const uint16_t stored = table.revision();

    table.clear();
    CHECK(table.revision() != stored);

    // An empty table cleared again is not a change, so nothing is owed.
    const uint16_t emptied = table.revision();
    table.clear();
    CHECK(table.revision() == emptied);
}
