#include "FramingText.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace Tv5725 {

namespace {

const float Whole = 10000.0f;

const char *skipSpace(const char *at)
{
    while (*at == ' ' || *at == '\t')
        ++at;
    return at;
}

// strtol accepts a leading sign and stops at the first character it cannot
// use, so a field has to be checked for having consumed a digit at all --
// "a b c d" otherwise parses as four zeroes.
bool number(const char *&at, long &into)
{
    at = skipSpace(at);
    char *end = 0;
    const long value = strtol(at, &end, 10);
    if (end == at)
        return false;
    at = end;
    into = value;
    return true;
}

float proportionOf(long tenThousandths)
{
    return (float)tenThousandths / Whole;
}

long tenThousandthsOf(float proportion)
{
    return lrintf(proportion * Whole);
}

}  // namespace

FramingText::FramingText(FramingTable &table) : table_(table) {}

void FramingText::readLine(const char *line)
{
    const char *at = skipSpace(line);
    if (*at == '\0' || *at == '#')
        return;

    long lines = 0, rate = 0;
    if (!number(at, lines))
        return;
    at = skipSpace(at);
    if (*at++ != '@')
        return;
    if (!number(at, rate))
        return;
    at = skipSpace(at);
    if (*at++ != '=')
        return;

    long value[4];
    for (int i = 0; i < 4; ++i)
        if (!number(at, value[i]))
            return;

    // The key rejects a count or a rate no source runs, which is what a line
    // written while the source was settling holds.
    const SourceKey key((uint16_t)lines, (float)rate);
    if (!key.valid())
        return;

    table_.remember(key, PanAndZoom(proportionOf(value[0]), proportionOf(value[1]),
                                    proportionOf(value[2]), proportionOf(value[3])));
}

bool FramingText::writeLine(uint16_t index, char *out, uint8_t size) const
{
    if (index >= table_.count() || size == 0)
        return false;

    const SourceKey &key = table_.keyAt(index);
    const PanAndZoom &framing = table_.framingAt(index);

    const int written = snprintf(
        out, size, "%u@%u = %ld %ld %ld %ld",
        (unsigned)key.lines(), (unsigned)key.rateBucket(),
        tenThousandthsOf(framing.originOn(AxisHorizontal)),
        tenThousandthsOf(framing.extentOn(AxisHorizontal)),
        tenThousandthsOf(framing.originOn(AxisVertical)),
        tenThousandthsOf(framing.extentOn(AxisVertical)));

    return written > 0 && written < (int)size;
}

}  // namespace Tv5725
