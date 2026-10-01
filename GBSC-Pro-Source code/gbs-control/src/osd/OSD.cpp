#include "OSD.h"

#include <stddef.h>

#include "MenuPage.h"

namespace Osd {

const uint8_t OSD::Columns;
const uint8_t OSD::LabelColumn;
const uint8_t OSD::ValueLastColumn;
const uint8_t OSD::IndicatorColumn;
const char OSD::Hyphen;
const char OSD::Arrow;
const char OSD::PreviousPage;
const char OSD::NextPage;
const char OSD::Background;
const char OSD::Selected;
const char OSD::Unselected;
const char OSD::Clear;
const char OSD::Indicator;

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

// Up on the first row, the page number on the second and down on the third,
// which is the chain's strip. A level of one page has nothing to count.
void OSD::putIndicator(const MenuPage &page, uint8_t index)
{
    if (index == 0 && page.hasPreviousPage())
        putCell(index, IndicatorColumn, PreviousPage, Indicator);
    else if (index == 1 && (page.hasPreviousPage() || page.hasNextPage()))
        putCell(index, IndicatorColumn, (char)('0' + page.number()), Indicator);
    else if (index == 2 && page.hasNextPage())
        putCell(index, IndicatorColumn, NextPage, Indicator);
}

// Right-aligned at the far end, with a rule of hyphens leading to it from
// wherever the label stopped.
void OSD::putValue(uint8_t index, uint8_t from, const char *value,
                   char colour)
{
    const uint8_t length = lengthOf(value);
    if (length == 0 || length > ValueLastColumn)
        return;

    const uint8_t at = (uint8_t)(ValueLastColumn + 1 - length);
    for (uint8_t column = from; column < at; ++column)
        putCell(index, column, Hyphen, colour);
    putText(index, at, value, colour);
}

void OSD::row(const MenuPage &page, uint8_t index, const char *value)
{
    for (uint8_t column = 0; column < Columns; ++column)
        putCell(index, column, Background, Background);

    const bool selected = index == page.selected();
    const char colour = selected ? Selected : Unselected;
    const char *const label = page.labelAt(index);
    const uint8_t labelEnd = (uint8_t)(LabelColumn + lengthOf(label));

    if (selected)
        putCell(index, 0, Arrow, colour);
    putText(index, LabelColumn, label, colour);
    if (selected && page.leadsAt(index))
        putCell(index, labelEnd, Arrow, colour);
    putValue(index, labelEnd, value, colour);
    putIndicator(page, index);
}

void OSD::end() {}

const MenuRenderer &OSD::renderer()
{
    static const MenuRenderer instance(begin, row, end);
    return instance;
}

}  // namespace Osd
