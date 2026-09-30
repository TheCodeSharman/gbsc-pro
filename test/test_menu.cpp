// Host-compiled unit tests for the described OSD menu -- `make -C test menu`.
//
// The menu the remote drives, as a tree rather than as a branch per state.
// Navigation is the whole point: the old form wrote every Up and Down target
// out by hand at each branch, so the two were not symmetric and splicing a
// state out meant choosing what each inbound key should reach.
// docs/osd-menu.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../GBSC-Pro-Source code/gbs-control/src/osd/Menu.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuCursor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuItem.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuPage.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuRenderer.h"

using namespace Osd;

// A tree with one submenu, which is the smallest shape that can tell descending
// from moving.
static const MenuItem Colour[] = {
    MenuItem("Line filter", MenuItem::Choice, 'm', NULL, 0, NULL),
    MenuItem("Peaking", MenuItem::Choice, 'p', NULL, 0, NULL),
};

static const MenuItem System[] = {
    MenuItem("Frame lock", MenuItem::Choice, 'f', NULL, 0, NULL),
    MenuItem("Auto gain", MenuItem::Choice, 'g', NULL, 0, NULL),
    MenuItem("Deinterlace", MenuItem::Choice, 'd', NULL, 0, NULL),
    MenuItem("Restart", MenuItem::Action, 'R', NULL, 0, NULL),
    MenuItem("Info", MenuItem::Action, 'I', NULL, 0, NULL),
};

static const MenuItem Root[] = {
    MenuItem("Input", MenuItem::Action, 'i', NULL, 0, NULL),
    MenuItem("Colour", MenuItem::Submenu, 0, Colour, 2, NULL),
    MenuItem("Reset", MenuItem::Action, 'r', NULL, 0, NULL),
    MenuItem("System", MenuItem::Submenu, 0, System, 5, NULL),
};

static const uint8_t RootCount = 4;

TEST_CASE("a cursor starts on the first item of the level it was given")
{
    MenuCursor cursor(Root, RootCount);

    CHECK(cursor.index() == 0);
    CHECK(std::string(cursor.current().label()) == "Input");
}

TEST_CASE("down moves to the next item and up moves back")
{
    MenuCursor cursor(Root, RootCount);

    cursor.down();
    CHECK(std::string(cursor.current().label()) == "Colour");

    cursor.up();
    CHECK(std::string(cursor.current().label()) == "Input");
}

TEST_CASE("a level is a ring, so the ends join rather than stopping")
{
    // The root is described as a ring, and every Up/Down target used to be a
    // literal -- which is how 94's Down came to reach 103 rather than the
    // branch it arrived from.
    MenuCursor cursor(Root, RootCount);

    cursor.up();
    CHECK(std::string(cursor.current().label()) == "System");

    cursor.down();
    CHECK(std::string(cursor.current().label()) == "Input");
}

TEST_CASE("descending a submenu lands on its first child")
{
    MenuCursor cursor(Root, RootCount);
    cursor.down();
    REQUIRE(std::string(cursor.current().label()) == "Colour");

    CHECK(cursor.descend());
    CHECK(std::string(cursor.current().label()) == "Line filter");
    CHECK(cursor.depth() == 2);
}

TEST_CASE("an item with no children is not a level to descend into")
{
    MenuCursor cursor(Root, RootCount);
    REQUIRE(std::string(cursor.current().label()) == "Input");

    CHECK_FALSE(cursor.descend());
    CHECK(std::string(cursor.current().label()) == "Input");
    CHECK(cursor.depth() == 1);
}

TEST_CASE("ascending returns to the item that was descended from")
{
    // What the old form could not do: a state had no owner that knew its
    // neighbours, so coming back out landed wherever the branch said.
    MenuCursor cursor(Root, RootCount);
    cursor.down();
    REQUIRE(cursor.descend());
    cursor.down();
    REQUIRE(std::string(cursor.current().label()) == "Peaking");

    CHECK(cursor.ascend());
    CHECK(std::string(cursor.current().label()) == "Colour");
    CHECK(cursor.depth() == 1);
}

TEST_CASE("ascending from the root level reports there is nowhere to go")
{
    MenuCursor cursor(Root, RootCount);

    CHECK_FALSE(cursor.ascend());
    CHECK(cursor.depth() == 1);
}


// --- The three rows in view
//
// The overlay is three rows, and the chain cut its pages by hand -- handle_a
// carried ADC gain, Scanlines and Line filter, handle_b the next three. A
// window that follows the cursor is what removes that per-page bookkeeping.

TEST_CASE("a level shorter than the window shows every row")
{
    MenuCursor cursor(Colour, 2);
    const MenuPage page = cursor.page();

    CHECK(page.rows() == 2);
    CHECK(std::string(page.labelAt(0)) == "Line filter");
    CHECK(std::string(page.labelAt(1)) == "Peaking");
    CHECK(page.selected() == 0);
}

TEST_CASE("the selected row is the one the cursor is on")
{
    MenuCursor cursor(Colour, 2);
    cursor.down();

    CHECK(cursor.page().selected() == 1);
}

TEST_CASE("a level longer than the window shows the first rows until the cursor leaves them")
{
    MenuCursor cursor(System, 5);
    REQUIRE(cursor.page().rows() == MenuPage::Rows);

    CHECK(std::string(cursor.page().labelAt(0)) == "Frame lock");
    CHECK(cursor.page().selected() == 0);

    cursor.down();
    cursor.down();
    CHECK(std::string(cursor.page().labelAt(0)) == "Frame lock");
    CHECK(cursor.page().selected() == 2);
}

TEST_CASE("the window scrolls to keep the cursor in view")
{
    MenuCursor cursor(System, 5);
    cursor.down();
    cursor.down();
    cursor.down();

    CHECK(std::string(cursor.page().labelAt(0)) == "Auto gain");
    CHECK(std::string(cursor.page().labelAt(2)) == "Restart");
    CHECK(cursor.page().selected() == 2);
}

TEST_CASE("wrapping to the end brings the window with it")
{
    MenuCursor cursor(System, 5);
    cursor.up();

    CHECK(std::string(cursor.page().labelAt(2)) == "Info");
    CHECK(cursor.page().selected() == 2);
}


// --- What gets drawn
//
// The television overlay and the panel draw the same page differently, so the
// page is handed to a renderer rather than drawn where the cursor lives. A host
// test records what would be drawn and needs neither device.

struct DrawnRow {
    uint8_t index;
    std::string label;
    bool selected;
};

static std::vector<DrawnRow> Drawn;
static int Begun;
static int Ended;

static void recordBegin()
{
    Drawn.clear();
    ++Begun;
}

static void recordRow(uint8_t index, const char *label, const char *value,
                      bool selected)
{
    DrawnRow row;
    row.index = index;
    row.label = label;
    row.selected = selected;
    Drawn.push_back(row);
    (void)value;
}

static void recordEnd() { ++Ended; }

static const MenuRenderer Recorder(recordBegin, recordRow, recordEnd);

TEST_CASE("a renderer is told each row in view and which one is selected")
{
    MenuCursor cursor(System, 5);
    cursor.down();

    Recorder.draw(cursor.page());

    REQUIRE(Drawn.size() == MenuPage::Rows);
    CHECK(Drawn[0].label == "Frame lock");
    CHECK_FALSE(Drawn[0].selected);
    CHECK(Drawn[1].label == "Auto gain");
    CHECK(Drawn[1].selected);
    CHECK_FALSE(Drawn[2].selected);
}

TEST_CASE("a row carries the position it occupies, not the position in the level")
{
    MenuCursor cursor(System, 5);
    cursor.down();
    cursor.down();
    cursor.down();
    REQUIRE(std::string(cursor.current().label()) == "Restart");

    Recorder.draw(cursor.page());

    REQUIRE(Drawn.size() == MenuPage::Rows);
    CHECK(Drawn[0].index == 0);
    CHECK(Drawn[0].label == "Auto gain");
    CHECK(Drawn[2].index == 2);
    CHECK(Drawn[2].selected);
}

TEST_CASE("a redraw is bracketed, so a device that buffers knows when to flush")
{
    MenuCursor cursor(Colour, 2);
    const int begun = Begun;
    const int ended = Ended;

    Recorder.draw(cursor.page());

    CHECK(Begun == begun + 1);
    CHECK(Ended == ended + 1);
    CHECK(Drawn.size() == 2);
}


// --- A key press
//
// The menu never mutates an option. Ok on an item yields the letter the web and
// the serial console send for the same thing, and the sketch queues it; one
// action per option, three surfaces. docs/osd-menu.md

TEST_CASE("Ok on a Choice yields its letter and stays where it is")
{
    Menu menu(Root, RootCount, Recorder);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    REQUIRE(std::string(menu.cursor().current().label()) == "Line filter");

    CHECK(menu.press(Menu::KeyOk) == 'm');
    CHECK(std::string(menu.cursor().current().label()) == "Line filter");
    CHECK(menu.cursor().depth() == 2);
}

TEST_CASE("Ok on a Submenu descends and asks for nothing")
{
    Menu menu(Root, RootCount, Recorder);
    menu.open();
    menu.press(Menu::KeyDown);

    CHECK(menu.press(Menu::KeyOk) == 0);
    CHECK(menu.cursor().depth() == 2);
}

TEST_CASE("the Menu key opens from closed and leaves by the level it entered")
{
    Menu menu(Root, RootCount, Recorder);
    CHECK_FALSE(menu.isOpen());

    menu.press(Menu::KeyMenu);
    CHECK(menu.isOpen());

    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    REQUIRE(menu.cursor().depth() == 2);

    menu.press(Menu::KeyMenu);
    CHECK(menu.cursor().depth() == 1);
    CHECK(menu.isOpen());

    menu.press(Menu::KeyMenu);
    CHECK_FALSE(menu.isOpen());
}

TEST_CASE("Exit leaves from any depth")
{
    Menu menu(Root, RootCount, Recorder);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    REQUIRE(menu.cursor().depth() == 2);

    menu.press(Menu::KeyExit);
    CHECK_FALSE(menu.isOpen());
}

TEST_CASE("a closed menu draws nothing and answers no key but Menu")
{
    Menu menu(Root, RootCount, Recorder);
    const int begun = Begun;

    CHECK(menu.press(Menu::KeyOk) == 0);
    CHECK(menu.press(Menu::KeyDown) == 0);
    CHECK(Begun == begun);
    CHECK_FALSE(menu.isOpen());
}

TEST_CASE("every press that moves the cursor redraws")
{
    Menu menu(Root, RootCount, Recorder);
    menu.open();
    const int begun = Begun;

    menu.press(Menu::KeyDown);
    CHECK(Begun == begun + 1);
    CHECK(Drawn[1].selected);
}

TEST_CASE("reopening starts at the top rather than where it was left")
{
    Menu menu(Root, RootCount, Recorder);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    menu.press(Menu::KeyExit);

    menu.open();
    CHECK(menu.cursor().depth() == 1);
    CHECK(std::string(menu.cursor().current().label()) == "Input");
}
