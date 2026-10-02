#ifndef OSD_ROW_H_
#define OSD_ROW_H_

#include <stdint.h>

namespace Osd {

// One row of the television overlay, composed whole before any of it is sent.
// The part has no back buffer, so a cell written twice shows the first value:
// composing here is what makes a redraw go straight from the old row to the new
// one. docs/osd-menu.md
//
// Pure -- it reaches no bus. Osd::OSD is what sends a composed row to the
// STV9426.
class Row {
public:
    static const uint8_t Columns = 28;

    // The font is ASCII from 0x21 up, so text needs no translation. Two
    // characters are not where ASCII puts it: 0x20 is an accented letter rather
    // than a space, and the hyphen is at 0x3e.
    static const char Hyphen = 0x3e;

    Row(char symbol, char colour);

    void cell(uint8_t column, char symbol, char colour);

    // A space is not written at all: the bar is already there and a space has
    // no glyph to put over it.
    void text(uint8_t column, const char *text, char colour);

    // Right-aligned in `width` columns starting at `column`, blank to the left.
    void number(uint8_t column, uint16_t value, uint8_t width, char colour);

    char symbolAt(uint8_t column) const;
    char colourAt(uint8_t column) const;

private:
    char symbol_[Columns];
    char colour_[Columns];
};

}  // namespace Osd

#endif  // OSD_ROW_H_
