#ifndef OSD_OSD_H_
#define OSD_OSD_H_

// The menu on the television, drawn by the STV9426. Three rows of 28 character
// cells, and a row is written whole -- the chain painted each label at a fixed
// column, so removing an option's value left its label behind and the page
// unreflowed. docs/osd-menu.md

#include <stdint.h>

#include "MenuRenderer.h"

namespace Osd {

class OSD {
public:
    // A cell is two writes: the symbol at an odd address and its colour at the
    // even one below it, both in the page byte that selects the row.
    // OSD_parameters() is what supplies this on the board.
    typedef void (*WriteCell)(char address, char page, char value);

    static const uint8_t Columns = 28;

    // Where a row's four fields sit. The cursor takes the first column, so a
    // label is inset by one; the value ends two columns short, leaving the last
    // to the page indicator and the one before it as a gutter.
    static const uint8_t LabelColumn = 1;
    static const uint8_t ValueLastColumn = 25;
    static const uint8_t IndicatorColumn = 27;

    // The font is ASCII from 0x21 up, so a label needs no translation. Two
    // characters are not where ASCII puts it: 0x20 is an accented letter rather
    // than a space, and the hyphen is at 0x3e.
    static const char Hyphen = 0x3e;

    // The cursor on the selected row and, after a label, the mark that the item
    // leads somewhere -- one glyph for both, as the chain draws it. The other
    // two are the page arrows at the last column.
    static const char Arrow = 0x15;
    static const char PreviousPage = 0x06;
    static const char NextPage = 0x16;

    // A cell is written twice, and the same value means different things at the
    // two addresses: at the even one it is the colour, at the odd one a glyph.
    // Background is both -- a filled block in the bar's colour, which is how
    // background_up() paints a row. Clear turns a cell off altogether, which is
    // what OSD_Cut_0x01() writes to erase the overlay.
    static const char Background = 0x11;
    static const char Selected = 0x16;
    static const char Unselected = 0x17;
    static const char Clear = (char)0xc0;

    // The last column's strip is the level's, not the row's, so it keeps one
    // colour whichever row is selected.
    static const char Indicator = 0x12;

    // The one STV9426 on the board, or a recorder in a host test.
    static void writeThrough(WriteCell write);

    static const MenuRenderer &renderer();

private:
    static void begin();
    static void row(const MenuPage &page, uint8_t index, const char *value);
    static void end();

    static void putValue(uint8_t index, uint8_t from, const char *value,
                         char colour);
    static void putIndicator(const MenuPage &page, uint8_t index);

    static void putCell(uint8_t index, uint8_t column, char symbol,
                        char colour);
    static void putText(uint8_t index, uint8_t at, const char *text,
                        char colour);

    static WriteCell write_;
};

}  // namespace Osd

#endif  // OSD_OSD_H_
