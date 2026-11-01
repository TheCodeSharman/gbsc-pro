#include "FramingText.h"

#include "FramingLine.h"

namespace Tv5725 {

FramingText::FramingText(FramingTable &table) : table_(table) {}

void FramingText::readLine(const char *line)
{
    if (FramingLine::empty(line))
        return;

    const char *at = FramingLine::skipSpace(line);
    SourceKey key;
    PanAndZoom framing;
    Aspect shape;
    Route route;
    if (FramingLine::read(at, key, framing, shape, route))
        table_.remember(key, framing, shape, route);
}

bool FramingText::writeLine(uint16_t index, char *out, uint8_t size) const
{
    if (index >= table_.count())
        return false;
    return FramingLine::write(out, size, table_.keyAt(index),
                              table_.framingAt(index),
                              table_.aspectAt(index),
                              table_.routeAt(index)) > 0;
}

}  // namespace Tv5725
