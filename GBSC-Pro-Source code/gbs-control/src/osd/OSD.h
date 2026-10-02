#ifndef OSD_OSD_H_
#define OSD_OSD_H_

// The menu on the television, drawn by the STV9426. Three rows of 28 character
// cells, and a row is written whole -- the chain painted each label at a fixed
// column, so removing an option's value left its label behind and the page
// unreflowed. docs/osd-menu.md

#include <stdint.h>

#include "MenuPage.h"
#include "MenuRenderer.h"
#include "Row.h"

namespace Osd {

class OSD {
public:
    // A cell is two writes: the symbol at an odd address and its colour at the
    // even one below it, both in the page byte that selects the row.
    // OSD_parameters() is what supplies this on the board.
    typedef void (*WriteCell)(char address, char page, char value);

    // Where a row's four fields sit. The cursor takes the first column, so a
    // label is inset by one; the value ends two columns short, leaving the last
    // to the page indicator and the one before it as a gutter.
    static const uint8_t LabelColumn = 1;
    static const uint8_t ValueLastColumn = 25;
    static const uint8_t IndicatorColumn = 27;

    // The cursor on the selected row and, after a label, the mark that the item
    // leads somewhere -- one glyph for both, as the chain draws it. The other
    // two are the page arrows at the last column.
    static const char Arrow = 0x15;
    static const char PreviousPage = 0x06;
    static const char NextPage = 0x16;

    // The cluster a pad draws in place of a value, as the chain drew it.
    static const char PadLeft = 0x03;
    static const char PadUp = 0x08;
    static const char PadDown = 0x18;
    static const char PadRight = 0x13;

    // A cell is written twice, and the same value means different things at the
    // two addresses: at the even one it is the colour, at the odd one a glyph.
    // Background is both -- a filled block in the bar's colour, which is what a
    // row is filled with. Clear turns a cell off altogether, which is what
    // OSD_Cut_0x01() writes to erase the overlay.
    static const char Background = 0x11;
    static const char Selected = 0x16;
    static const char Unselected = 0x17;
    static const char Clear = (char)0xc0;

    // A row the engine cannot serve, drawn rather than hidden so the level
    // keeps its shape. The low three bits are the glyph's colour, so this is a
    // dark glyph on the bar where the other two are yellow and white.
    static const char Unavailable = 0x10;

    // The last column's strip is the level's, not the row's, so it keeps one
    // colour whichever row is selected.
    static const char Indicator = 0x12;

    // The cursor's cell is filled -- a dark glyph on a solid block -- where the
    // rest of the selected row is written over the bar in Selected.
    static const char Cursor = 0x60;

    // The one STV9426 on the board, or a recorder in a host test.
    static void writeThrough(WriteCell write);

    // A composed row onto the part. The one writer of the overlay, so every
    // screen composes a Row and hands it here.
    //
    // ONLY THE CELLS THAT MOVED. These screens are drawn from loop() on every
    // pass they are up, and a row is 56 writes on the bus the acquisition
    // shares: measured, redrawing two unchanged rows took a register read from
    // 0.03 s to 1.2..5.2 s and starved OTA. The part has no back buffer, so a
    // cell rewritten with what it already holds is a flicker as well as a cost.
    static void send(uint8_t index, const Row &row);

    // Whatever is on the part is no longer what was last sent -- after a clear,
    // or a bring-up. The next send writes every cell again.
    static void forget();

    static const MenuRenderer &renderer();

private:
    static void row(const MenuPage &page, uint8_t index, const char *value);
    static void end(const MenuPage &page);

    static void putValue(Row &line, uint8_t from, const char *value,
                         char colour);
    static void putIndicator(Row &line, const MenuPage &page, uint8_t index);

    static WriteCell write_;
    static Row sent_[MenuPage::Rows];
    static bool known_[MenuPage::Rows];
};

}  // namespace Osd

#endif  // OSD_OSD_H_
