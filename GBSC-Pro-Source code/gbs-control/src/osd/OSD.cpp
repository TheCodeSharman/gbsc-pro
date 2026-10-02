#include "OSD.h"

#include <stddef.h>

#include "MenuPage.h"

namespace Osd {

const uint8_t OSD::LabelColumn;
const uint8_t OSD::ValueLastColumn;
const uint8_t OSD::IndicatorColumn;
const char OSD::Arrow;
const char OSD::PreviousPage;
const char OSD::NextPage;
const char OSD::PadLeft;
const char OSD::PadUp;
const char OSD::PadDown;
const char OSD::PadRight;
const char OSD::Background;
const char OSD::Selected;
const char OSD::Unselected;
const char OSD::Clear;
const char OSD::Unavailable;
const char OSD::Indicator;
const char OSD::Cursor;

OSD::WriteCell OSD::write_ = NULL;

// What the part was last given, per row. Filled with Clear rather than the bar,
// so a first send that happens to be blank still writes.
Row OSD::sent_[MenuPage::Rows] = { Row(Clear, Clear), Row(Clear, Clear),
                                   Row(Clear, Clear) };
bool OSD::known_[MenuPage::Rows] = { false, false, false };

namespace {

// The page byte that selects a row. Not consecutive, and the second is 0x02
// rather than 0x01.
const char Pages[MenuPage::Rows] = { 0x00, 0x02, 0x03 };

const char PadArrows[] = { OSD::PadLeft, OSD::PadUp, OSD::PadDown,
                           OSD::PadRight, '\0' };

uint8_t lengthOf(const char *text)
{
    uint8_t length = 0;
    while (text != NULL && text[length] != '\0')
        ++length;
    return length;
}

}  // namespace

void OSD::writeThrough(WriteCell write) { write_ = write; }

void OSD::send(uint8_t index, const Row &line)
{
    if (write_ == NULL || index >= MenuPage::Rows)
        return;

    const bool all = !known_[index];
    for (uint8_t column = 0; column < Row::Columns; ++column) {
        if (!all && line.symbolAt(column) == sent_[index].symbolAt(column)
            && line.colourAt(column) == sent_[index].colourAt(column))
            continue;
        const char address = (char)(1 + 2 * column);
        write_(address, Pages[index], line.symbolAt(column));
        write_((char)(address - 1), Pages[index], line.colourAt(column));
    }

    sent_[index] = line;
    known_[index] = true;
}

void OSD::forget()
{
    for (uint8_t index = 0; index < MenuPage::Rows; ++index)
        known_[index] = false;
}

// Up on the first row, the page number on the second and down on the third,
// which is the chain's strip. A level of one page has nothing to count.
void OSD::putIndicator(Row &line, const MenuPage &page, uint8_t index)
{
    if (index == 0 && page.hasPreviousPage())
        line.cell(IndicatorColumn, PreviousPage, Indicator);
    else if (index == 1 && (page.hasPreviousPage() || page.hasNextPage()))
        line.cell(IndicatorColumn, (char)('0' + page.number()), Indicator);
    else if (index == 2 && page.hasNextPage())
        line.cell(IndicatorColumn, NextPage, Indicator);
}

// Right-aligned at the far end, with a rule of hyphens leading to it from
// wherever the label stopped.
void OSD::putValue(Row &line, uint8_t from, const char *value, char colour)
{
    const uint8_t length = lengthOf(value);
    if (length == 0 || length > ValueLastColumn)
        return;

    const uint8_t at = (uint8_t)(ValueLastColumn + 1 - length);
    for (uint8_t column = from; column < at; ++column)
        line.cell(column, Row::Hyphen, colour);
    line.text(at, value, colour);
}

void OSD::row(const MenuPage &page, uint8_t index, const char *value)
{
    Row line(Background, Background);

    const bool selected = index == page.selected();
    const char colour = !page.availableAt(index) ? Unavailable
                        : selected               ? Selected
                                                 : Unselected;
    const char *const label = page.labelAt(index);

    // The root ring carries its position, which is the level with nothing above
    // it; the chain kept the number inside the label string.
    uint8_t labelAt = LabelColumn;
    if (selected)
        line.cell(0, Arrow, Cursor);
    if (page.title() == NULL) {
        line.cell(labelAt, (char)('0' + page.positionAt(index)), colour);
        labelAt = (uint8_t)(labelAt + 2);
    }
    line.text(labelAt, label, colour);
    const uint8_t labelEnd = (uint8_t)(labelAt + lengthOf(label));

    uint8_t from = labelEnd;
    if (selected && page.descendsAt(index))
        line.cell(from++, Arrow, colour);
    putValue(line, from, selected && page.adjusting() ? PadArrows : value,
             colour);
    putIndicator(line, page, index);

    send(index, line);
}

// The rows the page does not fill, and only those: the overlay keeps what was
// written to it, so a level shorter than the window would leave the previous
// level's last row painted. Cleared rather than blanked, so it draws nothing
// instead of a bar of background across the picture -- and after the rows
// rather than before them, a cell cleared ahead of being repainted being a
// flicker on a part with no back buffer.
void OSD::end(const MenuPage &page)
{
    Row blank(Clear, Clear);
    for (uint8_t index = page.rows(); index < MenuPage::Rows; ++index)
        send(index, blank);
}

const MenuRenderer &OSD::renderer()
{
    static const MenuRenderer instance(NULL, row, end);
    return instance;
}

}  // namespace Osd
