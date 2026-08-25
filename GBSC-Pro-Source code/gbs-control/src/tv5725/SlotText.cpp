#include "SlotText.h"

#include <stdio.h>

#include "FramingLine.h"

namespace Tv5725 {

SlotText::SlotText(SlotTable &table) : table_(table) {}

void SlotText::readLine(const char *line)
{
    if (FramingLine::empty(line))
        return;

    const char *at = FramingLine::skipSpace(line);
    long slot = 0;
    if (!FramingLine::number(at, slot) || slot < 0 || slot > 0xFF)
        return;

    SourceKey key;
    PanAndZoom framing;
    if (FramingLine::read(at, key, framing))
        table_.remember((uint8_t)slot, key, framing);
}

bool SlotText::writeLine(uint16_t index, char *out, uint8_t size) const
{
    if (index >= table_.count())
        return false;

    const int written =
        snprintf(out, size, "%u ", (unsigned)table_.slotAt(index));
    if (written <= 0 || written >= (int)size)
        return false;

    return FramingLine::write(out + written, (uint8_t)(size - written),
                              table_.keyAt(index), table_.framingAt(index)) > 0;
}

}  // namespace Tv5725
