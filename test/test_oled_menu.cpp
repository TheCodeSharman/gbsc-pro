// Host-compiled unit tests for the OLED icon menu's storage -- `make -C test
// oled-menu`.
//
// The item pool is the largest static allocation in the firmware, and free heap
// is what bounds OTA. A child array belongs to the parents that have children
// rather than to every item in the pool. docs/osd-menu.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string.h>

#include "../GBSC-Pro-Source code/gbs-control/OLEDMenuItem.h"
#include "../GBSC-Pro-Source code/gbs-control/OLEDMenuNav.h"

namespace
{
void reset(OLEDMenuItem *items, size_t count)
{
    memset(items, 0, count * sizeof(OLEDMenuItem));
    for (size_t i = 0; i < count; ++i)
    {
        items[i].imageWidth = OLED_MENU_WIDTH;
        items[i].imageHeight = 16;
    }
}
}

TEST_CASE("an item does not carry a parent's child array")
{
    CHECK(sizeof(OLEDMenuItem) < OLED_MENU_MAX_SUBITEMS_NUM * sizeof(OLEDMenuItem *));
}

TEST_CASE("a parent exposes its children in registration order")
{
    OLEDMenuItem items[4];
    reset(items, 4);
    OLEDMenuChildBlocks blocks;

    for (int i = 1; i < 4; ++i)
    {
        CHECK(items[0].addSubItem(&items[i], blocks));
    }

    REQUIRE(items[0].numSubItem == 3);
    CHECK(items[0].subItems[0] == &items[1]);
    CHECK(items[0].subItems[1] == &items[2]);
    CHECK(items[0].subItems[2] == &items[3]);
}

TEST_CASE("a leaf costs no child block")
{
    OLEDMenuItem items[8];
    reset(items, 8);
    OLEDMenuChildBlocks blocks;
    const uint8_t before = blocks.freeBlocks();

    for (int i = 1; i < 8; ++i)
    {
        CHECK(items[0].addSubItem(&items[i], blocks));
    }

    CHECK(blocks.freeBlocks() == before - 1);
}

TEST_CASE("a cleared parent returns its block")
{
    OLEDMenuItem items[2];
    reset(items, 2);
    OLEDMenuChildBlocks blocks;
    const uint8_t before = blocks.freeBlocks();

    items[0].addSubItem(&items[1], blocks);
    items[0].clearSubItems(blocks);

    CHECK(blocks.freeBlocks() == before);
}

TEST_CASE("a page that re-registers its children does not exhaust the pool")
{
    OLEDMenuItem items[2];
    reset(items, 2);
    OLEDMenuChildBlocks blocks;

    for (int visit = 0; visit < OLED_MENU_MAX_PARENTS_NUM * 4; ++visit)
    {
        items[0].clearSubItems(blocks);
        REQUIRE(items[0].addSubItem(&items[1], blocks));
    }

    CHECK(items[0].numSubItem == 1);
}

TEST_CASE("a parent past the last block is refused rather than served")
{
    OLEDMenuItem parents[OLED_MENU_MAX_PARENTS_NUM + 1];
    OLEDMenuItem child;
    reset(parents, OLED_MENU_MAX_PARENTS_NUM + 1);
    reset(&child, 1);
    OLEDMenuChildBlocks blocks;

    for (int i = 0; i < OLED_MENU_MAX_PARENTS_NUM; ++i)
    {
        REQUIRE(parents[i].addSubItem(&child, blocks));
    }

    CHECK_FALSE(parents[OLED_MENU_MAX_PARENTS_NUM].addSubItem(&child, blocks));
}

TEST_CASE("a child past the block's width is refused rather than written")
{
    OLEDMenuItem items[OLED_MENU_MAX_SUBITEMS_NUM + 2];
    reset(items, OLED_MENU_MAX_SUBITEMS_NUM + 2);
    OLEDMenuChildBlocks blocks;

    for (int i = 0; i < OLED_MENU_MAX_SUBITEMS_NUM; ++i)
    {
        REQUIRE(items[0].addSubItem(&items[i + 1], blocks));
    }

    CHECK_FALSE(items[0].addSubItem(&items[OLED_MENU_MAX_SUBITEMS_NUM + 1], blocks));
    CHECK(items[0].numSubItem == OLED_MENU_MAX_SUBITEMS_NUM);
}


// --- driving the tree without the knob
//
// The icon tree answers to the encoder alone -- /menu?key= is the DESCRIBED
// menu and reaches a different tree -- so nothing a session can run walks the
// pages that register their children on entry. Those are the pages the item
// pool is sized for, and exhausting it calls panicAndDisable(), which turns
// the menu off rather than failing where a test would see it.

TEST_CASE("every gesture the knob has is named")
{
    OLEDMenuNav nav = OLEDMenuNav::IDLE;

    REQUIRE(oledNavFor("up", &nav));
    CHECK(nav == OLEDMenuNav::UP);
    REQUIRE(oledNavFor("down", &nav));
    CHECK(nav == OLEDMenuNav::DOWN);
    REQUIRE(oledNavFor("enter", &nav));
    CHECK(nav == OLEDMenuNav::ENTER);
}

TEST_CASE("there is no fourth gesture to name")
{
    // Scrolling past the first item selects the status bar, where ENTER leaves
    // the level, so these three reach every page. A route offering `back`
    // would be inventing a gesture the hardware does not have.
    OLEDMenuNav nav = OLEDMenuNav::ENTER;

    CHECK_FALSE(oledNavFor("back", &nav));
    CHECK_FALSE(oledNavFor("exit", &nav));
    CHECK_FALSE(oledNavFor("", &nav));
    CHECK_FALSE(oledNavFor(NULL, &nav));
}

TEST_CASE("a key the knob has no gesture for leaves the nav alone")
{
    // Not written back as IDLE: the caller queues what it is handed, and a
    // silent IDLE is a press the unit answers 200 to and never makes.
    OLEDMenuNav nav = OLEDMenuNav::DOWN;

    CHECK_FALSE(oledNavFor("sideways", &nav));
    CHECK(nav == OLEDMenuNav::DOWN);
}
