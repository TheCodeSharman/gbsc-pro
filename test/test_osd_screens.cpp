// Host-compiled tests for the two overlay screens -- `make -C test osd-screens`.
//
// Neither is a menu, so neither goes through MenuRenderer: each composes its
// rows and hands them to Osd::OSD, which is the one writer of the part. Every
// case here records the cells and reads the row back as text, so a failure
// names the row and shows both.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../GBSC-Pro-Source code/gbs-control/src/osd/InfoScreen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/MuteOverlay.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/OSD.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/Row.h"
#include "../GBSC-Pro-Source code/gbs-control/src/osd/VolumeOverlay.h"

using namespace Osd;

namespace {

struct Cell {
    char address, page, value;
};

std::vector<Cell> cells;

void record(char address, char page, char value)
{
    cells.push_back((Cell){ address, page, value });
}

// The page byte that selects a row, which is not consecutive.
const char Pages[] = { 0x00, 0x02, 0x03 };

// What a row reads as. A cell left at the background bar is a space, and the
// hyphen is the one glyph the font does not put where ASCII does.
std::string textOf(uint8_t row)
{
    std::string text(Row::Columns, ' ');
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].page != Pages[row] || (cells[i].address % 2) == 0)
            continue;
        const uint8_t column = (uint8_t)((cells[i].address - 1) / 2);
        if (column >= Row::Columns)
            continue;
        const char symbol = cells[i].value;
        text[column] = symbol == VolumeOverlay::Bar ? ' '
                       : symbol == Row::Hyphen      ? '-'
                                                    : symbol;
    }
    while (!text.empty() && text[text.size() - 1] == ' ')
        text.erase(text.size() - 1);
    return text;
}

char colourAt(uint8_t row, uint8_t column)
{
    char colour = '\0';
    for (size_t i = 0; i < cells.size(); ++i)
        if (cells[i].page == Pages[row]
            && cells[i].address == (char)(2 * column))
            colour = cells[i].value;
    return colour;
}

void start()
{
    cells.clear();
    OSD::writeThrough(record);
    OSD::forget();
}

InfoScreen::Report bench()
{
    InfoScreen::Report report;
    report.bypass = false;
    report.outputPx = 1920;
    report.outputLines = 1080;
    report.outputRateHz = 50;
    report.input = "vga";
    report.present = true;
    report.lines = 311;
    report.interlaced = false;
    report.fieldRateHz = 50;
    report.lineRateHz = 15625;
    return report;
}

}  // namespace

TEST_CASE("the volume overlay names what is being changed and shows the level")
{
    start();

    VolumeOverlay::draw(37);

    CHECK(textOf(0) == " Line input volume  37");
}

TEST_CASE("a level of zero is shown rather than left blank")
{
    start();

    VolumeOverlay::draw(0);

    CHECK(textOf(0) == " Line input volume   0");
}

TEST_CASE("the mute overlay says which way it went")
{
    start();
    VolumeOverlay::draw(37);
    MuteOverlay::draw(true);

    CHECK(textOf(0) == " MUTE ON");

    MuteOverlay::draw(false);

    CHECK(textOf(0) == " MUTE OFF");
}

TEST_CASE("the info screen says what is going out and what is coming in")
{
    start();

    InfoScreen::draw(bench());

    CHECK(textOf(0) == "Out: 1920x1080 vga   50Hz");
    CHECK(textOf(1) == "In: 311p 50Hz 15.6kHz");
}

// THE OUTPUT IS A RESOLUTION OR IT IS PASS-THROUGH, and the two are not the
// same kind of answer: bypass hands the source's own timing to the encoder, so
// there is no raster of ours to name and the sink reports the SOURCE's mode.
// Reading 0x0 there is what the mode's own active region gives.
TEST_CASE("pass-through is named rather than reported as a resolution of zero")
{
    start();
    InfoScreen::Report report = bench();
    report.bypass = true;
    report.outputPx = 0;
    report.outputLines = 0;

    InfoScreen::draw(report);

    CHECK(textOf(0) == "Out: Bypass    vga   50Hz");
}

// Nothing of ours is timing the output in pass-through: the frame rate is read
// off the VDS, which the video does not go through there. A rate of zero is the
// absence of a measurement rather than a measurement of zero.
TEST_CASE("an output nothing is timing shows no rate rather than zero")
{
    start();
    InfoScreen::Report report = bench();
    report.outputRateHz = 0;

    InfoScreen::draw(report);

    CHECK(textOf(0) == "Out: 1920x1080 vga");
}

// THE SCAN TYPE IS REPORTED FROM THE MEASUREMENT, NOT CLASSIFIED AGAIN. The
// chain asked STATUS_IF_INP_* -- the input formatter's SD classifier -- and
// printed `Err` for everything it did not recognise, which on the bench source
// is every frame.
TEST_CASE("an interlaced source is reported as interlaced")
{
    start();
    InfoScreen::Report report = bench();
    report.lines = 524;
    report.interlaced = true;
    report.fieldRateHz = 60;

    report.lineRateHz = 31469;

    InfoScreen::draw(report);

    CHECK(textOf(1) == "In: 524i 60Hz 31.5kHz");
}

TEST_CASE("a source nothing is measuring says so instead of reporting zeroes")
{
    start();
    InfoScreen::Report report = bench();
    report.present = false;

    InfoScreen::draw(report);

    CHECK(textOf(1) == "In:   no signal");
}

TEST_CASE("the row titles are drawn in the title colour and the rest in the body's")
{
    start();

    InfoScreen::draw(bench());

    CHECK(colourAt(0, 0) == InfoScreen::Title);
    CHECK(colourAt(1, 0) == InfoScreen::Title);
    CHECK(colourAt(0, 6) == InfoScreen::Body);
}

// A REDRAW THAT CHANGES NOTHING COSTS THE LOOP A SECOND. Both rows are 112
// writes on a bus the acquisition shares, and these screens are drawn from
// loop() on every pass they are up -- measured on the bench, a register read
// went from 0.03 s to 1.2..5.2 s with the info screen open, which starved OTA
// and read as a wedged firmware. The part has no back buffer either, so a cell
// rewritten with what it already holds is a flicker as well as a cost.
TEST_CASE("a screen redrawn with what it already shows writes nothing")
{
    start();
    InfoScreen::draw(bench());
    const size_t drawn = cells.size();
    REQUIRE(drawn > 0);

    InfoScreen::draw(bench());

    CHECK(cells.size() == drawn);

    InfoScreen::Report moved = bench();
    moved.lines = 625;
    InfoScreen::draw(moved);

    CHECK(cells.size() > drawn);
}

TEST_CASE("the volume overlay is not redrawn at an unchanged level either")
{
    start();
    VolumeOverlay::draw(37);
    const size_t drawn = cells.size();
    REQUIRE(drawn > 0);

    VolumeOverlay::draw(37);

    CHECK(cells.size() == drawn);

    VolumeOverlay::draw(36);

    CHECK(cells.size() > drawn);
}

TEST_CASE("every cell of a row is written, so nothing of the last screen survives")
{
    // The part has no back buffer and the chain wrote single cells, so a
    // shorter value left the previous one's tail on the row.
    start();

    InfoScreen::draw(bench());

    for (uint8_t column = 0; column < Row::Columns; ++column) {
        bool written = false;
        for (size_t i = 0; i < cells.size(); ++i)
            if (cells[i].page == Pages[0]
                && cells[i].address == (char)(1 + 2 * column))
                written = true;
        REQUIRE(written);
    }
}
