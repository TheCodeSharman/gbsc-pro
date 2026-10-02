// Host-compiled tests for Osd::Row -- `make -C test osd-row`.
//
// One row of the television overlay, composed whole before any of it is sent.
// Pure: it holds 28 cells and nothing else, so every case here reads the cells
// back rather than recording writes.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/osd/Row.h"

using namespace Osd;

namespace {

const char Bar = 0x11;
const char Body = 0x17;

// What a row reads as, with a space for every cell left at the fill.
std::string textOf(const Row &row, char fill = Bar)
{
    std::string text;
    for (uint8_t column = 0; column < Row::Columns; ++column) {
        const char symbol = row.symbolAt(column);
        text += symbol == fill ? ' ' : symbol == Row::Hyphen ? '-' : symbol;
    }
    return text;
}

}  // namespace

TEST_CASE("a row starts as the fill it was made with, every cell")
{
    Row row(Bar, Bar);

    for (uint8_t column = 0; column < Row::Columns; ++column) {
        REQUIRE(row.symbolAt(column) == Bar);
        REQUIRE(row.colourAt(column) == Bar);
    }
}

TEST_CASE("text lands at the column it was given")
{
    Row row(Bar, Bar);

    row.text(3, "Info:", Body);

    CHECK(textOf(row) == "   Info:                    ");
    CHECK(row.colourAt(3) == Body);
}

TEST_CASE("a space writes nothing, so the bar shows through it")
{
    // The part has no transparent glyph: a space written as a character would
    // paint a block over the bar. Osd_Display() skipped them for the same
    // reason.
    Row row(Bar, Bar);

    row.text(0, "a b", Body);

    CHECK(row.symbolAt(1) == Bar);
    CHECK(row.colourAt(1) == Bar);
}

TEST_CASE("the hyphen is the one character the font does not put where ASCII does")
{
    Row row(Bar, Bar);

    row.text(0, "-", Body);

    CHECK(row.symbolAt(0) == Row::Hyphen);
}

TEST_CASE("text that would run off the end is cut rather than wrapping")
{
    Row row(Bar, Bar);

    row.text((uint8_t)(Row::Columns - 2), "abcd", Body);

    CHECK(row.symbolAt((uint8_t)(Row::Columns - 2)) == 'a');
    CHECK(row.symbolAt((uint8_t)(Row::Columns - 1)) == 'b');
    CHECK(row.symbolAt(0) == Bar);
}

TEST_CASE("a number is right-aligned in the width it was given")
{
    Row row(Bar, Bar);

    row.number(0, 50, 3, Body);

    CHECK(textOf(row) == " 50                         ");
}

TEST_CASE("a number wider than its field shows its least significant digits")
{
    // Nothing truncates on the left by accident: a count that outgrew its field
    // would otherwise show a leading digit and read as a smaller number.
    Row row(Bar, Bar);

    row.number(0, 1234, 3, Body);

    CHECK(textOf(row) == "234                         ");
}

TEST_CASE("a zero is one digit, not a blank field")
{
    Row row(Bar, Bar);

    row.number(0, 0, 3, Body);

    CHECK(textOf(row) == "  0                         ");
}
