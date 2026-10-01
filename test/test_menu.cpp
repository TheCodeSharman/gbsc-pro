// Host-compiled unit tests for the described OSD menu -- `make -C test menu`.
//
// The menu the remote drives, as a tree rather than as a branch per state.
// Navigation is the whole point: the old form wrote every Up and Down target
// out by hand at each branch, so the two were not symmetric and splicing a
// state out meant choosing what each inbound key should reach.
// docs/osd-menu.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "SolvedEngine.h"

class Print {};

#include <string>
#include <vector>

#include "../GBSC-Pro-Source code/gbs-control/options.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Controls.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Nudge.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/Menu.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuCommand.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuContext.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuTree.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/OSD.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuCursor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuItem.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuPad.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuPage.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MenuRenderer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelection.h"

using namespace Osd;

// A solved engine and a set of preferences, which is what an item reads to say
// what it is currently set to.
struct Panel {
    SolvedEngine solved;
    Print console;
    Tv5725::Controls controls;
    userOptions options;
    MenuContext context;

    Panel() : controls(solved.engine, console), options(), context(controls, options) {}
};

// A tree with one submenu, which is the smallest shape that can tell descending
// from moving.
static const MenuItem Colour[] = {
    MenuItem::choice("Line filter", 'm', NULL),
    MenuItem::choice("Peaking", 'p', NULL),
};

static const MenuItem System[] = {
    MenuItem::choice("Frame lock", 'f', NULL),
    MenuItem::choice("Auto gain", 'g', NULL),
    MenuItem::choice("Deinterlace", 'd', NULL),
    MenuItem::action("Restart", 'R'),
    MenuItem::action("Info", 'I'),
};

static const MenuItem Root[] = {
    MenuItem::action("Input", 'i'),
    MenuItem::submenu("Colour", Colour, 2),
    MenuItem::action("Reset", 'r'),
    MenuItem::submenu("System", System, 5),
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

TEST_CASE("a level longer than the window shows the first three until the cursor leaves them")
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

TEST_CASE("the window turns a page rather than scrolling by a row")
{
    MenuCursor cursor(System, 5);
    cursor.down();
    cursor.down();
    cursor.down();

    CHECK(std::string(cursor.page().labelAt(0)) == "Restart");
    CHECK(std::string(cursor.page().labelAt(1)) == "Info");
    CHECK(cursor.page().rows() == 2);
    CHECK(cursor.page().selected() == 0);
}

TEST_CASE("wrapping to the end lands on the last page")
{
    MenuCursor cursor(System, 5);
    cursor.up();

    CHECK(std::string(cursor.page().labelAt(1)) == "Info");
    CHECK(cursor.page().selected() == 1);
}

TEST_CASE("a page is numbered within its level, which is what the overlay counts")
{
    MenuCursor cursor(System, 5);
    CHECK(cursor.page().number() == 1);
    CHECK_FALSE(cursor.page().hasPreviousPage());
    CHECK(cursor.page().hasNextPage());

    cursor.down();
    cursor.down();
    cursor.down();
    CHECK(cursor.page().number() == 2);
    CHECK(cursor.page().hasPreviousPage());
    CHECK_FALSE(cursor.page().hasNextPage());
}

TEST_CASE("a level that fits is one page with nothing either side of it")
{
    MenuCursor cursor(Colour, 2);

    CHECK(cursor.page().number() == 1);
    CHECK_FALSE(cursor.page().hasPreviousPage());
    CHECK_FALSE(cursor.page().hasNextPage());
}


// --- What gets drawn
//
// The television overlay and the panel draw the same page differently, so the
// page is handed to a renderer rather than drawn where the cursor lives. A host
// test records what would be drawn and needs neither device.

struct DrawnRow {
    uint8_t index;
    std::string label;
    std::string value;
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

static void recordRow(const MenuPage &page, uint8_t index, const char *value)
{
    DrawnRow row;
    row.index = index;
    row.label = page.labelAt(index);
    row.value = value != NULL ? value : "";
    row.selected = index == page.selected();
    Drawn.push_back(row);
}

static void recordEnd() { ++Ended; }

static const MenuRenderer Recorder(recordBegin, recordRow, recordEnd);

TEST_CASE("a renderer is told each row in view and which one is selected")
{
    Panel panel;
    MenuCursor cursor(System, 5);
    cursor.down();

    Recorder.draw(cursor.page(), panel.context);

    REQUIRE(Drawn.size() == MenuPage::Rows);
    CHECK(Drawn[0].label == "Frame lock");
    CHECK_FALSE(Drawn[0].selected);
    CHECK(Drawn[1].label == "Auto gain");
    CHECK(Drawn[1].selected);
    CHECK_FALSE(Drawn[2].selected);
}

TEST_CASE("a row carries the position it occupies, not the position in the level")
{
    Panel panel;
    MenuCursor cursor(System, 5);
    cursor.down();
    cursor.down();
    cursor.down();
    REQUIRE(std::string(cursor.current().label()) == "Restart");

    Recorder.draw(cursor.page(), panel.context);

    REQUIRE(Drawn.size() == 2);
    CHECK(Drawn[0].index == 0);
    CHECK(Drawn[0].label == "Restart");
    CHECK(Drawn[0].selected);
}

TEST_CASE("a redraw is bracketed, so a device that buffers knows when to flush")
{
    Panel panel;
    MenuCursor cursor(Colour, 2);
    const int begun = Begun;
    const int ended = Ended;

    Recorder.draw(cursor.page(), panel.context);

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
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    REQUIRE(std::string(menu.cursor().current().label()) == "Line filter");

    CHECK(menu.press(Menu::KeyOk).letter() == 'm');
    CHECK(std::string(menu.cursor().current().label()) == "Line filter");
    CHECK(menu.cursor().depth() == 2);
}

TEST_CASE("Ok on a Submenu descends and asks for nothing")
{
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.press(Menu::KeyDown);

    CHECK_FALSE(menu.press(Menu::KeyOk).asked());
    CHECK(menu.cursor().depth() == 2);
}

TEST_CASE("the Menu key opens from closed and leaves by the level it entered")
{
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
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

TEST_CASE("closing erases what was drawn")
{
    // The overlay keeps what was written to it, so a menu that stops drawing
    // stays on the screen. Measured on the unit before this: Exit left the bar
    // and all three rows over the picture.
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.drawIfNeeded();
    REQUIRE(Drawn.size() == MenuPage::Rows);

    menu.press(Menu::KeyExit);
    CHECK(menu.needsRedraw());

    menu.drawIfNeeded();
    CHECK(Drawn.empty());
    CHECK_FALSE(menu.needsRedraw());
}

TEST_CASE("Exit leaves from any depth")
{
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    REQUIRE(menu.cursor().depth() == 2);

    menu.press(Menu::KeyExit);
    CHECK_FALSE(menu.isOpen());
}

TEST_CASE("a closed menu draws nothing and answers no key but Menu")
{
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    const int begun = Begun;

    CHECK_FALSE(menu.press(Menu::KeyOk).asked());
    CHECK_FALSE(menu.press(Menu::KeyDown).asked());
    menu.drawIfNeeded();
    CHECK(Begun == begun);
    CHECK_FALSE(menu.isOpen());
}

TEST_CASE("a press marks a redraw rather than drawing, so the bus stays in loop()")
{
    // The STV9426 is on the ESP's I2C bus, and a press arrives from a network
    // callback. Register access is deferred to loop() for exactly this reason.
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    const int begun = Begun;

    menu.press(Menu::KeyDown);
    CHECK(Begun == begun);
    CHECK(menu.needsRedraw());

    menu.drawIfNeeded();
    CHECK(Begun == begun + 1);
    CHECK(Drawn[1].selected);
    CHECK_FALSE(menu.needsRedraw());
}

TEST_CASE("a press that moves nothing still marks a redraw, because a value may have moved")
{
    // An Ok on a Choice queues a letter that loop() acts on, so the row's value
    // is stale until the next draw.
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.drawIfNeeded();

    menu.press(Menu::KeyOk);
    CHECK(menu.needsRedraw());
}

TEST_CASE("reopening starts at the top rather than where it was left")
{
    Panel panel;
    Menu menu(Root, RootCount, Recorder, panel.context);
    menu.open();
    menu.press(Menu::KeyDown);
    menu.press(Menu::KeyOk);
    menu.press(Menu::KeyExit);

    menu.open();
    CHECK(menu.cursor().depth() == 1);
    CHECK(std::string(menu.cursor().current().label()) == "Input");
}


// --- Which command surface a letter belongs to
//
// The board has two: /uc? reaches handleType2Command() and /sc? the switch in
// loop(). Four letters mean different things in each -- 'Z' toggles matched
// presets on one and bumps VDS_Y_OFST on the other -- so a letter alone cannot
// say what an Ok asked for.

static const MenuItem Surfaces[] = {
    MenuItem::choice("Line filter", 'm', NULL),
    MenuItem::serialChoice("Peaking", 'f', NULL),
};

TEST_CASE("an Ok names the surface its letter belongs to")
{
    Panel panel;
    Menu menu(Surfaces, 2, Recorder, panel.context);
    menu.open();

    const MenuCommand filter = menu.press(Menu::KeyOk);
    CHECK(filter.letter() == 'm');
    CHECK(filter.queue() == MenuCommand::UserCommand);

    menu.press(Menu::KeyDown);
    const MenuCommand peaking = menu.press(Menu::KeyOk);
    CHECK(peaking.letter() == 'f');
    CHECK(peaking.queue() == MenuCommand::SerialCommand);
}

TEST_CASE("a press that asks for nothing yields no letter")
{
    Panel panel;
    Menu menu(Surfaces, 2, Recorder, panel.context);
    menu.open();

    CHECK_FALSE(menu.press(Menu::KeyDown).asked());
}


// --- A pad
//
// Four directions on one row rather than a level of four items: Ok hands the
// arrows to the picture and Menu takes them back. A direction asks for a
// control and the way it goes -- never for a letter, the /sc? geometry letters
// being stated in output pixels where a tap asks for one granule.

static const MenuPad Move(MenuCommand::nudge(Tv5725::Nudge::VerticalPan, +1),
                          MenuCommand::nudge(Tv5725::Nudge::VerticalPan, -1),
                          MenuCommand::nudge(Tv5725::Nudge::HorizontalPan, +1),
                          MenuCommand::nudge(Tv5725::Nudge::HorizontalPan, -1));

static const MenuItem Screen[] = {
    MenuItem::pad("Move", Move),
    MenuItem::choice("Line filter", 'm', NULL),
};

TEST_CASE("Ok on a pad hands it the arrows rather than asking for anything")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();

    CHECK_FALSE(menu.press(Menu::KeyOk).asked());
    CHECK(menu.isAdjusting());
}

TEST_CASE("each arrow asks for the control and direction the pad gives it")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE(menu.press(Menu::KeyOk).asked() == false);

    const MenuCommand up = menu.press(Menu::KeyUp);
    CHECK(up.queue() == MenuCommand::GeometryNudge);
    CHECK(up.control() == Tv5725::Nudge::VerticalPan);
    CHECK(up.direction() == +1);
    CHECK(up.asked());

    CHECK(menu.press(Menu::KeyDown).direction() == -1);
    CHECK(menu.press(Menu::KeyLeft).control() == Tv5725::Nudge::HorizontalPan);
    CHECK(menu.press(Menu::KeyLeft).direction() == +1);
    CHECK(menu.press(Menu::KeyRight).direction() == -1);
}

TEST_CASE("Ok gives the arrows back to the level")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE(menu.press(Menu::KeyOk).asked() == false);
    REQUIRE(menu.isAdjusting());

    CHECK_FALSE(menu.press(Menu::KeyOk).asked());
    CHECK_FALSE(menu.isAdjusting());
    menu.press(Menu::KeyDown);
    CHECK(std::string(menu.cursor().current().label()) == "Line filter");
}

TEST_CASE("Menu leaves the pad rather than the level it is on")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE(menu.press(Menu::KeyOk).asked() == false);

    menu.press(Menu::KeyMenu);
    CHECK_FALSE(menu.isAdjusting());
    CHECK(menu.isOpen());
}

TEST_CASE("Exit closes from inside a pad")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE(menu.press(Menu::KeyOk).asked() == false);

    menu.press(Menu::KeyExit);
    CHECK_FALSE(menu.isOpen());
    CHECK_FALSE(menu.isAdjusting());
}

TEST_CASE("the page drawn while a pad has the arrows says so")
{
    // The device draws the pad -- the overlay its four arrows -- and nothing
    // else on the page changes, so the page is what carries it.
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    CHECK_FALSE(menu.page().adjusting());

    menu.press(Menu::KeyOk);
    CHECK(menu.page().adjusting());
}

TEST_CASE("reopening leaves the arrows with the level")
{
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE(menu.press(Menu::KeyOk).asked() == false);
    menu.press(Menu::KeyExit);

    menu.open();
    CHECK_FALSE(menu.isAdjusting());
}


// ===== The described tree =====
//
// Above is the machinery over a tree of its own. Below is the tree the remote
// actually walks: every option reachable, each saying what it is set to, and an
// Ok yielding the letter the web and the serial console already send.

// The tree is walked rather than indexed, because an item's position is not what
// a case is about.
static const MenuItem *find(const MenuItem *items, uint8_t count, const char *label)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (std::string(items[i].label()) == label)
            return &items[i];
        const MenuItem *found = find(items[i].children(), items[i].childCount(), label);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static bool describes(const char *label)
{
    return find(MenuTree::root(), MenuTree::rootCount(), label) != NULL;
}

static const MenuItem &item(const char *label)
{
    const MenuItem *found = find(MenuTree::root(), MenuTree::rootCount(), label);
    REQUIRE(found != NULL);
    return *found;
}

// Descended from the root as the remote descends it: a cursor rooted at a level
// of its own is that level's root ring, which the overlay numbers.
static MenuCursor cursorInside(const char *label)
{
    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    while (std::string(cursor.current().label()) != label)
        cursor.down();
    REQUIRE(cursor.descend());
    return cursor;
}

TEST_CASE("an item says what its option is currently set to")
{
    Panel panel;

    panel.options.wantVdsLineFilter = 1;
    CHECK(std::string(item("Line filter").valueText(panel.context)) == "ON");

    panel.options.wantVdsLineFilter = 0;
    CHECK(std::string(item("Line filter").valueText(panel.context)) == "OFF");
}


// --- The root ring
//
// The chain's root was not a ring: Input had no Up and Reset Settings no Down,
// so the two ends were dead. A level that joins is the described form's
// navigation rather than a target written out per branch.
//
// Input, Screen Settings and the Sv-Av submenu are not here yet. Their items act
// by calling a sketch function -- InputVGA_mode(), the HC32 frame, the pan and
// zoom ramp -- rather than by asking for a letter, so describing them waits on
// each action reaching one command surface. docs/osd-menu.md

TEST_CASE("the root names every top-level page, in the order the remote walks them")
{
    const char *const expected[] = {
        "Input", "Output Resolution", "Screen Settings", "System Settings",
        "Picture Settings", "Reset Settings",
    };

    REQUIRE(MenuTree::rootCount() == sizeof(expected) / sizeof(expected[0]));
    for (uint8_t i = 0; i < MenuTree::rootCount(); ++i)
        CHECK(std::string(MenuTree::root()[i].label()) == expected[i]);
}

TEST_CASE("the root's ends join, which the chain's did not")
{
    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    REQUIRE(std::string(cursor.current().label()) == "Input");

    cursor.up();
    CHECK(std::string(cursor.current().label()) == "Reset Settings");
}


// --- Screen Settings
//
// Two pads rather than two levels: Ok hands the arrows to the picture, and the
// hold ramp behind a held key multiplies the granule each one asks for.

TEST_CASE("the screen pads name the geometry control each arrow moves")
{
    const MenuItem &screen = item("Screen Settings");
    REQUIRE(screen.childCount() == 2);

    const MenuItem &move = screen.children()[0];
    CHECK(std::string(move.label()) == "Move");
    REQUIRE(move.isPad());
    CHECK(move.pad().up().control() == Tv5725::Nudge::VerticalPan);
    CHECK(move.pad().up().direction() == +1);
    CHECK(move.pad().down().control() == Tv5725::Nudge::VerticalPan);
    CHECK(move.pad().down().direction() == -1);
    CHECK(move.pad().left().control() == Tv5725::Nudge::HorizontalPan);
    CHECK(move.pad().left().direction() == +1);
    CHECK(move.pad().right().control() == Tv5725::Nudge::HorizontalPan);
    CHECK(move.pad().right().direction() == -1);

    // The key follows the edge that moves: the picture is pinned at the top of
    // the active region, so Down grows it and Up shrinks it.
    const MenuItem &scale = screen.children()[1];
    CHECK(std::string(scale.label()) == "Scale");
    REQUIRE(scale.isPad());
    CHECK(scale.pad().up().control() == Tv5725::Nudge::VerticalZoom);
    CHECK(scale.pad().up().direction() == -1);
    CHECK(scale.pad().down().control() == Tv5725::Nudge::VerticalZoom);
    CHECK(scale.pad().down().direction() == +1);
    CHECK(scale.pad().left().control() == Tv5725::Nudge::HorizontalZoom);
    CHECK(scale.pad().left().direction() == -1);
    CHECK(scale.pad().right().control() == Tv5725::Nudge::HorizontalZoom);
    CHECK(scale.pad().right().direction() == +1);
}


// --- Input
//
// Selecting a source is not a letter on either command surface: it is
// pendingInputSelection, which /input?src= queues and loop() acts on, and it
// reaches the HC32's analog switches as well as ADC_INPUT_SEL. So the menu asks
// for it by naming the source.

TEST_CASE("each input asks for the selection rather than for a letter")
{
    struct Row { const char *label; VideoSourceSelection::Id source; };
    const Row expected[] = {
        { "RGBs", VideoSourceSelection::Rgbs },
        { "RGsB", VideoSourceSelection::RgsB },
        { "VGA", VideoSourceSelection::Vga },
        { "YPBPR", VideoSourceSelection::Ypbpr },
        { "SV", VideoSourceSelection::SVideo },
        { "AV", VideoSourceSelection::Composite },
    };

    const MenuItem &page = item("Input");
    REQUIRE(page.childCount() == sizeof(expected) / sizeof(expected[0]));
    for (uint8_t i = 0; i < page.childCount(); ++i) {
        CHECK(std::string(page.children()[i].label()) == expected[i].label);
        CHECK(page.children()[i].okCommand().queue()
              == MenuCommand::InputSelection);
        CHECK(page.children()[i].okCommand().source() == expected[i].source);
    }
}

TEST_CASE("an input selection is something asked for, as a letter is")
{
    Panel panel;
    const MenuItem &page = item("Input");
    Menu menu(page.children(), page.childCount(), Recorder, panel.context);
    menu.open();

    const MenuCommand asked = menu.press(Menu::KeyOk);
    CHECK(asked.asked());
    CHECK(asked.source() == VideoSourceSelection::Rgbs);
}


// --- Output Resolution
//
// Six resolutions, each already a /uc? letter. Pass Through is deliberately not
// a seventh: it is not a resolution -- OutputChoice cannot express it -- and the
// option behind it is the upscaling preference under System Settings, so a
// second label for one option is a divergence with no reason.

TEST_CASE("each resolution asks for the letter the web and the console send")
{
    struct Row { const char *label; char letter; };
    const Row expected[] = {
        { "1920x1080", 's' }, { "1280x1024", 'p' }, { "1280x960", 'f' },
        { "1280x720", 'g' },  { "768x576", 'j' },   { "720x480", 'h' },
    };

    const MenuItem &page = item("Output Resolution");
    REQUIRE(page.childCount() == sizeof(expected) / sizeof(expected[0]));
    for (uint8_t i = 0; i < page.childCount(); ++i) {
        CHECK(std::string(page.children()[i].label()) == expected[i].label);
        CHECK(page.children()[i].okCommand().letter() == expected[i].letter);
        CHECK(page.children()[i].okCommand().queue() == MenuCommand::UserCommand);
    }
}


// --- Picture Settings

TEST_CASE("every picture option says what it is set to")
{
    Panel panel;

    panel.options.enableAutoGain = 1;
    CHECK(std::string(item("ADC gain").valueText(panel.context)) == "ON");

    panel.options.wantScanlines = 0;
    CHECK(std::string(item("Scanlines").valueText(panel.context)) == "OFF");

    panel.options.wantPeaking = 1;
    CHECK(std::string(item("Peaking").valueText(panel.context)) == "ON");

    panel.options.wantStepResponse = 0;
    CHECK(std::string(item("Step response").valueText(panel.context)) == "OFF");
}

TEST_CASE("an option whose letter is a serial command says so")
{
    // Peaking, step response and automatic gain are handled by the switch in
    // loop() rather than by handleType2Command(), and their letters mean other
    // things there.
    CHECK(item("Peaking").okCommand().queue() == MenuCommand::SerialCommand);
    CHECK(item("Step response").okCommand().queue() == MenuCommand::SerialCommand);
    CHECK(item("ADC gain").okCommand().queue() == MenuCommand::SerialCommand);
}

TEST_CASE("Left and Right step an adjustable option")
{
    CHECK(item("ADC gain").nextCommand().letter() == 'n');
    CHECK(item("ADC gain").previousCommand().letter() == 'o');
    CHECK(item("ADC gain").nextCommand().queue() == MenuCommand::UserCommand);

    CHECK(item("Scanlines").nextCommand().letter() == 'K');
    CHECK(item("Scanlines").previousCommand().letter() == 'K');
}


// --- System Settings

TEST_CASE("every system option says what it is set to")
{
    Panel panel;

    panel.options.enableFrameTimeLock = 1;
    CHECK(std::string(item("Frame Time Lock").valueText(panel.context)) == "ON");

    panel.options.frameTimeLockMethod = 1;
    CHECK(std::string(item("Lock Method").valueText(panel.context)) == "Vtotal only");
    panel.options.frameTimeLockMethod = 0;
    CHECK(std::string(item("Lock Method").valueText(panel.context)) == "Vtotal+VSST");

    panel.options.enableCalibrationADC = 0;
    CHECK(std::string(item("ADC calibration").valueText(panel.context)) == "OFF");

    panel.options.deintMode = 1;
    CHECK(std::string(item("Deinterlace").valueText(panel.context)) == "Bob");
    panel.options.deintMode = 0;
    CHECK(std::string(item("Deinterlace").valueText(panel.context)) == "Adaptive");
}

TEST_CASE("the clock generator reads the opposite way round to the option behind it")
{
    // The preference is disableExternalClockGenerator, and the row reports the
    // generator.
    Panel panel;

    panel.options.disableExternalClockGenerator = 0;
    CHECK(std::string(item("Clock generator").valueText(panel.context)) == "ON");

    panel.options.disableExternalClockGenerator = 1;
    CHECK(std::string(item("Clock generator").valueText(panel.context)) == "OFF");
}

TEST_CASE("the upscaling preference is on the menu, which the chain left unreachable")
{
    // Branch 96 drew it and its Ok was commented out, and nothing reached the
    // branch: the only route was /uc?x.
    Panel panel;

    panel.options.preferScalingRgbhv = 1;
    CHECK(std::string(item("Use upscaling").valueText(panel.context)) == "ON");
    CHECK(item("Use upscaling").okCommand().letter() == 'x');
}

TEST_CASE("restarting is on the menu, which the chain also left unreachable")
{
    // Branch 110's only inbound key was 109's Down, and 109 is commented out.
    CHECK(item("Restart").okCommand().letter() == 'a');
}


// --- The aspect ratio
//
// The item the extraction was for: one declaration rather than a sixth
// hand-wired state. docs/aspect-ratio.md

TEST_CASE("the aspect item names the shape the source is shown in")
{
    Panel panel;

    // The bench raster's own shape, which the source defaults to.
    REQUIRE(panel.solved.engine.aspect() == Tv5725::Aspect(Tv5725::Aspect::FourThree));
    CHECK(std::string(item("Aspect").valueText(panel.context)) == "4:3");

    REQUIRE(panel.solved.engine.setAspect(Tv5725::Aspect(Tv5725::Aspect::SixteenNine)));
    CHECK(std::string(item("Aspect").valueText(panel.context)) == "16:9");

    REQUIRE(panel.solved.engine.setAspect(Tv5725::Aspect(Tv5725::Aspect::Fill)));
    CHECK(std::string(item("Aspect").valueText(panel.context)) == "Fill");

    CHECK(item("Aspect").okCommand().letter() == 'G');
}


// --- The two options the doc records as dead

TEST_CASE("a dead option is not described")
{
    // PalForce60 had its standard-byte swap deleted and matchPresetSource never
    // had a consumer. Both survived in the chain because splicing a state out
    // meant choosing what each inbound key should reach; here they are absent.
    CHECK_FALSE(describes("Force 50 / 60Hz"));
    CHECK_FALSE(describes("Matched presets"));
}


// --- What a row is currently set to
//
// The value cannot be known without the context, and every path that draws a row
// needs one -- so it is passed at draw time rather than held by the tree.

TEST_CASE("a row carries what its option is currently set to")
{
    Panel panel;
    panel.options.wantVdsLineFilter = 1;
    panel.options.wantPeaking = 0;

    MenuCursor cursor = cursorInside("Picture Settings");
    while (std::string(cursor.current().label()) != "Line filter")
        cursor.down();

    Recorder.draw(cursor.page(), panel.context);

    REQUIRE(Drawn.size() == MenuPage::Rows);
    CHECK(Drawn[cursor.page().selected()].label == "Line filter");
    CHECK(Drawn[cursor.page().selected()].value == "ON");
}

TEST_CASE("a row with no value to show carries none")
{
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    REQUIRE(std::string(cursor.current().label()) == "Input");

    Recorder.draw(cursor.page(), panel.context);

    CHECK(Drawn[0].value == "");
}


// --- Which level the page is on
//
// The overlay and the panel both draw a breadcrumb, and the chain wrote one per
// branch: "Menu->Color", "Menu->System", "Menu->>>". The page names the item it
// was descended from and the renderer composes the rest.

TEST_CASE("a page names the item its level was descended from")
{
    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    bool named = cursor.page().title() != NULL;
    CHECK_FALSE(named);

    cursor.down();
    cursor.down();
    cursor.down();
    REQUIRE(std::string(cursor.current().label()) == "System Settings");
    REQUIRE(cursor.descend());

    CHECK(std::string(cursor.page().title()) == "System Settings");

    cursor.ascend();
    named = cursor.page().title() != NULL;
    CHECK_FALSE(named);
}


// --- The menu on the television
//
// Three rows of 28 character cells. The chain painted each label at a fixed
// column, so removing an option's ON/OFF field left its label behind and the
// page unreflowed; a row written whole cannot do that.

struct Cell {
    char address;
    char page;
    char value;
};

static std::vector<Cell> Cells;

static void recordCell(char address, char page, char value)
{
    Cell cell;
    cell.address = address;
    cell.page = page;
    cell.value = value;
    Cells.push_back(cell);
}

// The symbol at one cell, as rowText() renders it.
static char symbolAt(uint8_t row, uint8_t column);

// The symbols of one row, read back off the recorded writes in address order.
// The four glyphs that are not ASCII are written as stand-ins so a whole row
// reads as a line of text: a blank cell as a space, the cursor and the submenu
// marker as `>`, and the two page arrows as `^` and `v`.
static std::string rowText(uint8_t row)
{
    static const char Pages[] = { 0x00, 0x02, 0x03 };
    std::string text(OSD::Columns, ' ');
    for (size_t i = 0; i < Cells.size(); ++i) {
        if (Cells[i].page != Pages[row] || (Cells[i].address & 1) == 0)
            continue;
        const uint8_t column = (uint8_t)((Cells[i].address - 1) / 2);
        if (column >= OSD::Columns)
            continue;
        const char symbol = Cells[i].value;
        text[column] = symbol == OSD::Background || symbol == OSD::Clear ? ' '
                       : symbol == OSD::Arrow                           ? '>'
                       : symbol == OSD::PreviousPage                    ? '^'
                       : symbol == OSD::NextPage                        ? 'v'
                       : symbol == OSD::Hyphen                          ? '-'
                       : symbol == OSD::PadLeft                         ? '<'
                       : symbol == OSD::PadUp                           ? '^'
                       : symbol == OSD::PadDown                         ? 'v'
                       : symbol == OSD::PadRight                        ? '>'
                                                                        : symbol;
    }
    while (!text.empty() && text[text.size() - 1] == ' ')
        text.erase(text.size() - 1);
    return text;
}

static char symbolAt(uint8_t row, uint8_t column)
{
    const std::string text = rowText(row);
    return column < text.size() ? text[column] : ' ';
}

// Cells accumulate across draws, because the overlay keeps what was written to
// it: a helper that started from blanks could not tell a row that was blanked
// from one that was never written.
static void drawOnTelevision(const MenuPage &page, const MenuContext &context)
{
    OSD::writeThrough(recordCell);
    OSD::renderer().draw(page, context);
}

TEST_CASE("the last column counts the pages of the level")
{
    Panel panel;

    MenuCursor cursor = cursorInside("Picture Settings");
    REQUIRE(cursor.page().number() == 1);

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);
    CHECK(symbolAt(0, OSD::IndicatorColumn) == ' ');
    CHECK(symbolAt(1, OSD::IndicatorColumn) == '1');
    CHECK(symbolAt(2, OSD::IndicatorColumn) == 'v');

    cursor.down();
    cursor.down();
    cursor.down();
    REQUIRE(cursor.page().number() == 2);

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);
    CHECK(symbolAt(0, OSD::IndicatorColumn) == '^');
    CHECK(symbolAt(1, OSD::IndicatorColumn) == '2');
    CHECK(symbolAt(2, OSD::IndicatorColumn) == 'v');
}

TEST_CASE("a level that fits on one page leaves the last column alone")
{
    // The chain wrote the page character out per branch, so every level it drew
    // carried one. A level with nothing either side of it has nothing to count.
    Panel panel;
    MenuCursor cursor(Colour, 2);

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(symbolAt(0, OSD::IndicatorColumn) == ' ');
    CHECK(symbolAt(1, OSD::IndicatorColumn) == ' ');
}

TEST_CASE("the selected row carries a cursor at the first column, the label beside it")
{
    // The cursor is a glyph as well as the row colour, and every label is inset
    // by one to leave room for it.
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(0).substr(0, 8) == ">1 Input");
    CHECK(rowText(1).substr(0, 20) == " 2 Output Resolution");
}

TEST_CASE("the selected row marks an item that leads somewhere, just after its label")
{
    // The chain marked it at a column chosen per item, which is why removing an
    // option left the mark where the label used to end.
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    REQUIRE(std::string(cursor.current().label()) == "Input");

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(0) == ">1 Input>");
    CHECK(rowText(1) == " 2 Output Resolution       1");
}

TEST_CASE("an item that leads nowhere is not marked")
{
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    cursor.up();
    REQUIRE(std::string(cursor.current().label()) == "Reset Settings");

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(0) == " 4 System Settings         ^");
    CHECK(rowText(1) == " 5 Picture Settings        2");
    CHECK(rowText(2) == ">6 Reset Settings");
}

TEST_CASE("a rule of hyphens leads from the label to the value")
{
    Panel panel;
    panel.options.wantVdsLineFilter = 1;

    MenuCursor cursor = cursorInside("Picture Settings");
    while (std::string(cursor.current().label()) != "Line filter")
        cursor.down();

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(cursor.page().selected()) == ">Line filter------------ON v");
}

TEST_CASE("a row is written whole, so a shorter label leaves no tail behind")
{
    Panel panel;

    MenuCursor cursor = cursorInside("System Settings");
    REQUIRE(std::string(cursor.current().label()) == "Aspect");

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);
    REQUIRE(rowText(1) == " Use upscaling---------OFF 1");

    // A level whose second row is shorter, drawn over the same cells.
    MenuCursor second = cursorInside("Picture Settings");
    drawOnTelevision(second.page(), panel.context);

    CHECK(rowText(1) == " Scanlines-------------OFF 1");
}

TEST_CASE("a space inside a label is the font's blank, not its 0x20")
{
    // 0x20 draws an accented letter. The overlay blanks a cell with 0x00, which
    // OSD_symbols_1() is what says: it writes that value at every address.
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    for (size_t i = 0; i < Cells.size(); ++i)
        if ((Cells[i].address & 1) != 0)
            CHECK(Cells[i].value != 0x20);
}

TEST_CASE("a row the page does not fill is cleared rather than painted")
{
    // Painting it in the row colour leaves a bar of background across the
    // picture where there is no menu.
    Panel panel;

    const MenuItem *pair = item("System Settings").children();
    MenuCursor cursor(pair, 2);
    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    bool cleared = true;
    for (size_t i = 0; i < Cells.size(); ++i)
        if (Cells[i].page == 0x03 && (Cells[i].address & 1) == 0
            && Cells[i].value != OSD::Clear)
            cleared = false;
    CHECK(cleared);
}

TEST_CASE("the selected row is the only one in the highlight colour")
{
    Panel panel;

    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    cursor.down();
    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    // The colour of a row is the even address below each symbol.
    uint8_t highlighted = 0;
    static const char Pages[] = { 0x00, 0x02, 0x03 };
    for (uint8_t row = 0; row < MenuPage::Rows; ++row) {
        for (size_t i = 0; i < Cells.size(); ++i)
            if (Cells[i].page == Pages[row] && (Cells[i].address & 1) == 0
                && Cells[i].value == OSD::Selected) {
                ++highlighted;
                break;
            }
    }
    CHECK(highlighted == 1);
}

TEST_CASE("a page with fewer rows than the overlay blanks the rest")
{
    // The overlay keeps what was written to it, so a short level drawn over a
    // full one leaves the third row painted unless the draw blanks it.
    Panel panel;

    Cells.clear();
    const MenuItem &picture = item("Picture Settings");
    MenuCursor full = cursorInside("Picture Settings");
    drawOnTelevision(full.page(), panel.context);
    REQUIRE(rowText(2) != "");

    MenuCursor pair(picture.children(), 2);
    drawOnTelevision(pair.page(), panel.context);

    CHECK(rowText(2) == "");
}

TEST_CASE("a pad draws its four arrows where a value would go")
{
    // The chain drew the same four, at the column its own rule stopped at.
    Panel panel;
    Menu menu(Screen, 2, Recorder, panel.context);
    menu.open();
    REQUIRE_FALSE(menu.press(Menu::KeyOk).asked());

    Cells.clear();
    drawOnTelevision(menu.page(), panel.context);

    CHECK(rowText(0) == ">1 Move>--------------<^v>");
}

TEST_CASE("the root ring is numbered, as the chain numbered it")
{
    // The chain carried the number inside the label string -- "4 System
    // Settings" -- so it painted one on the root ring and nowhere else.
    Panel panel;
    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(0) == ">1 Input>");
    CHECK(rowText(1) == " 2 Output Resolution       1");

    cursor.down();
    cursor.down();
    cursor.down();
    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);
    CHECK(rowText(0) == ">4 System Settings>        ^");
}

TEST_CASE("a level below the root is not numbered")
{
    Panel panel;
    const MenuItem &input = item("Input");
    MenuCursor cursor(MenuTree::root(), MenuTree::rootCount());
    REQUIRE(cursor.descend());
    REQUIRE(cursor.current().label() == input.children()[0].label());

    Cells.clear();
    drawOnTelevision(cursor.page(), panel.context);

    CHECK(rowText(0) == ">RGBs");
}
