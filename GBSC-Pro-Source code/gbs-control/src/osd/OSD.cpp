#include "OSD.h"

#include <stddef.h>

#include "MenuPage.h"

namespace Osd {

const uint8_t OSD::Columns;
const char OSD::Hyphen;
const char OSD::Background;
const char OSD::Selected;
const char OSD::Unselected;
const char OSD::Clear;

OSD::WriteCell OSD::write_ = NULL;

namespace {

// The page byte that selects a row. Not consecutive, and the second is 0x02
// rather than 0x01.
const char Pages[MenuPage::Rows] = { 0x00, 0x02, 0x03 };

uint8_t lengthOf(const char *text)
{
    uint8_t length = 0;
    while (text != NULL && text[length] != '\0')
        ++length;
    return length;
}

}  // namespace

void OSD::writeThrough(WriteCell write) { write_ = write; }

void OSD::putCell(uint8_t index, uint8_t column, char symbol,
                             char colour)
{
    if (write_ == NULL || index >= MenuPage::Rows || column >= Columns)
        return;
    const char address = (char)(1 + 2 * column);
    write_(address, Pages[index], symbol);
    write_((char)(address - 1), Pages[index], colour);
}

// Every row, because the overlay keeps what was written to it: a level shorter
// than the window would otherwise leave the previous level's last row painted.
// Cleared rather than blanked, so a row the page does not fill draws nothing
// instead of a bar of background across the picture.
void OSD::begin()
{
    for (uint8_t index = 0; index < MenuPage::Rows; ++index)
        for (uint8_t column = 0; column < Columns; ++column)
            putCell(index, column, Clear, Clear);
}

// A space is not written at all, as Osd_Display() does not write one either:
// the bar is already there and a space has no glyph to put over it.
void OSD::putText(uint8_t index, uint8_t at, const char *text,
                             char colour)
{
    for (uint8_t i = 0; text != NULL && text[i] != '\0'; ++i) {
        if (text[i] == ' ')
            continue;
        putCell(index, (uint8_t)(at + i), text[i] == '-' ? Hyphen : text[i],
                colour);
    }
}

void OSD::row(uint8_t index, const char *label, const char *value,
                         bool selected)
{
    for (uint8_t column = 0; column < Columns; ++column)
        putCell(index, column, Background, Background);

    const char colour = selected ? Selected : Unselected;
    const uint8_t valueLength = lengthOf(value);
    putText(index, 0, label, colour);
    if (valueLength != 0 && valueLength < Columns)
        putText(index, (uint8_t)(Columns - valueLength), value, colour);
}

void OSD::end() {}

const MenuRenderer &OSD::renderer()
{
    static const MenuRenderer instance(begin, row, end);
    return instance;
}

}  // namespace Osd
