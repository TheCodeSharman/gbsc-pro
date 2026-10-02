#include "Row.h"

#include <stddef.h>

namespace Osd {

const uint8_t Row::Columns;
const char Row::Hyphen;

Row::Row(char symbol, char colour)
{
    for (uint8_t column = 0; column < Columns; ++column) {
        symbol_[column] = symbol;
        colour_[column] = colour;
    }
}

void Row::cell(uint8_t column, char symbol, char colour)
{
    if (column >= Columns)
        return;
    symbol_[column] = symbol;
    colour_[column] = colour;
}

void Row::text(uint8_t column, const char *text, char colour)
{
    for (uint8_t i = 0; text != NULL && text[i] != '\0'; ++i) {
        if (text[i] == ' ')
            continue;
        cell((uint8_t)(column + i), text[i] == '-' ? Hyphen : text[i], colour);
    }
}

void Row::number(uint8_t column, uint16_t value, uint8_t width, char colour)
{
    for (uint8_t place = width; place > 0; --place) {
        cell((uint8_t)(column + place - 1), (char)('0' + value % 10), colour);
        value /= 10;
        if (value == 0)
            break;
    }
}

char Row::symbolAt(uint8_t column) const
{
    return column < Columns ? symbol_[column] : '\0';
}

char Row::colourAt(uint8_t column) const
{
    return column < Columns ? colour_[column] : '\0';
}

}  // namespace Osd
