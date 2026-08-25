#include "FramingLine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace Tv5725 {

namespace {
const float Whole = 10000.0f;
}

const char *FramingLine::skipSpace(const char *at)
{
    while (*at == ' ' || *at == '\t')
        ++at;
    return at;
}

// strtol accepts a leading sign and stops at the first character it cannot
// use, so a field has to be checked for having consumed a digit at all --
// "a b c d" otherwise parses as four zeroes.
bool FramingLine::number(const char *&at, long &into)
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

float FramingLine::proportionOf(long tenThousandths)
{
    return (float)tenThousandths / Whole;
}

long FramingLine::tenThousandthsOf(float proportion)
{
    return lrintf(proportion * Whole);
}

bool FramingLine::empty(const char *line)
{
    const char *at = skipSpace(line);
    return *at == '\0' || *at == '#';
}

bool FramingLine::read(const char *&at, SourceKey &key, PanAndZoom &framing)
{
    long lines = 0, rate = 0;
    if (!number(at, lines))
        return false;
    at = skipSpace(at);
    if (*at++ != '@')
        return false;
    if (!number(at, rate))
        return false;
    at = skipSpace(at);
    if (*at++ != '=')
        return false;

    long value[4];
    for (int i = 0; i < 4; ++i)
        if (!number(at, value[i]))
            return false;

    const SourceKey read((uint16_t)lines, (float)rate);
    if (!read.valid())
        return false;

    key = read;
    framing = PanAndZoom(proportionOf(value[0]), proportionOf(value[1]),
                         proportionOf(value[2]), proportionOf(value[3]));
    return true;
}

int FramingLine::write(char *out, uint8_t size, const SourceKey &key,
                       const PanAndZoom &framing)
{
    if (size == 0)
        return -1;

    const int written = snprintf(
        out, size, "%u@%u = %ld %ld %ld %ld",
        (unsigned)key.lines(), (unsigned)key.rateBucket(),
        tenThousandthsOf(framing.originOn(AxisHorizontal)),
        tenThousandthsOf(framing.extentOn(AxisHorizontal)),
        tenThousandthsOf(framing.originOn(AxisVertical)),
        tenThousandthsOf(framing.extentOn(AxisVertical)));

    return written > 0 && written < (int)size ? written : -1;
}

}  // namespace Tv5725
