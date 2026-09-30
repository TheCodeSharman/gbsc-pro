#ifndef OSD_TELEVISION_MENU_H_
#define OSD_TELEVISION_MENU_H_

// The menu on the television, drawn by the STV9426. Three rows of 28 character
// cells, and a row is written whole -- the chain painted each label at a fixed
// column, so removing an option's value left its label behind and the page
// unreflowed. docs/osd-menu.md

#include <stdint.h>

#include "MenuRenderer.h"

namespace Osd {

class TelevisionMenu {
public:
    // A cell is two writes: the symbol at an odd address and its colour at the
    // even one below it, both in the page byte that selects the row.
    // OSD_parameters() is what supplies this on the board.
    typedef void (*WriteCell)(char address, char page, char value);

    static const uint8_t Columns = 28;

    // The font is ASCII from 0x21 up, so a label needs no translation. Two
    // characters are not where ASCII puts it: 0x20 is an accented letter rather
    // than a space, and the hyphen is at 0x3e.
    static const char Hyphen = 0x3e;

    // A cell is written twice, and the same value means different things at the
    // two addresses: at the even one it is the colour, at the odd one a glyph.
    // Background is both -- a filled block in the bar's colour, which is how
    // background_up() paints a row. Clear turns a cell off altogether, which is
    // what OSD_Cut_0x01() writes to erase the overlay.
    static const char Background = 0x11;
    static const char Selected = 0x16;
    static const char Unselected = 0x17;
    static const char Clear = (char)0xc0;

    // The one STV9426 on the board, or a recorder in a host test.
    static void writeThrough(WriteCell write);

    static const MenuRenderer &renderer();

private:
    static void begin();
    static void row(uint8_t index, const char *label, const char *value,
                    bool selected);
    static void end();

    static void putCell(uint8_t index, uint8_t column, char symbol,
                        char colour);
    static void putText(uint8_t index, uint8_t at, const char *text,
                        char colour);

    static WriteCell write_;
};

}  // namespace Osd

#endif  // OSD_TELEVISION_MENU_H_
