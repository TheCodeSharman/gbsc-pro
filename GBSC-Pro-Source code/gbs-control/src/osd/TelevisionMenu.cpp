#include "TelevisionMenu.h"

#include <stddef.h>

#include "MenuPage.h"

namespace Osd {

const uint8_t TelevisionMenu::Columns;
const char TelevisionMenu::Hyphen;
const char TelevisionMenu::Background;
const char TelevisionMenu::Selected;
const char TelevisionMenu::Unselected;
const char TelevisionMenu::Clear;

TelevisionMenu::WriteCell TelevisionMenu::write_ = NULL;

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

void TelevisionMenu::writeThrough(WriteCell write) { write_ = write; }

void TelevisionMenu::putCell(uint8_t index, uint8_t column, char symbol,
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
void TelevisionMenu::begin()
{
    for (uint8_t index = 0; index < MenuPage::Rows; ++index)
        for (uint8_t column = 0; column < Columns; ++column)
            putCell(index, column, Clear, Clear);
}

// A space is not written at all, as Osd_Display() does not write one either:
// the bar is already there and a space has no glyph to put over it.
void TelevisionMenu::putText(uint8_t index, uint8_t at, const char *text,
                             char colour)
{
    for (uint8_t i = 0; text != NULL && text[i] != '\0'; ++i) {
        if (text[i] == ' ')
            continue;
        putCell(index, (uint8_t)(at + i), text[i] == '-' ? Hyphen : text[i],
                colour);
    }
}

void TelevisionMenu::row(uint8_t index, const char *label, const char *value,
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

void TelevisionMenu::end() {}

const MenuRenderer &TelevisionMenu::renderer()
{
    static const MenuRenderer instance(begin, row, end);
    return instance;
}

}  // namespace Osd
