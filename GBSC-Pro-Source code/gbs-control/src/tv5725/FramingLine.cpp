#include "FramingLine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "FramingEntry.h"

namespace Tv5725 {

bool FramingLine::polarityFrom(char symbol, SourceKey::Polarity &into)
{
    switch (symbol) {
        case '+': into = SourceKey::Positive; return true;
        case '-': into = SourceKey::Negative; return true;
        case '?': into = SourceKey::Undetermined; return true;
        default:  return false;
    }
}

char FramingLine::symbolFor(SourceKey::Polarity polarity)
{
    switch (polarity) {
        case SourceKey::Positive: return '+';
        case SourceKey::Negative: return '-';
        default:                  return '?';
    }
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

// The key is quantised finer than a hertz, so a record rounded to one cannot be
// read back for a source sitting more than SourceIdentityPerThousand from an
// integer -- 800x600@60 runs DMT's 60.3168 and a RISC PC emits 50.474 behind one
// 320x256@50, which are 5.3 and 9.5 per thousand out.
bool FramingLine::rateFrom(const char *&at, float &into)
{
    long whole = 0;
    if (!number(at, whole))
        return false;

    float value = (float)whole;
    if (*at == '.') {
        ++at;
        long fraction = 0, scale = 1;
        while (*at >= '0' && *at <= '9') {
            fraction = fraction * 10 + (*at - '0');
            scale *= 10;
            ++at;
        }
        if (scale == 1)
            return false;
        value += (float)fraction / (float)scale;
    }

    into = value;
    return true;
}

// FramingEntry owns the unit, because the store holds the same record this
// writes: a proportion the grammar cannot state is one the table cannot hold.
float FramingLine::proportionOf(long tenThousandths)
{
    if (tenThousandths < 0)
        return 0.0f;
    return FramingEntry::proportionOf(
        tenThousandths > 0xFFFF ? 0xFFFF : (uint16_t)tenThousandths);
}

long FramingLine::tenThousandthsOf(float proportion)
{
    return (long)FramingEntry::tenThousandthsOf(proportion);
}

bool FramingLine::empty(const char *line)
{
    const char *at = skipSpace(line);
    return *at == '\0' || *at == '#';
}

bool FramingLine::moreOnLine(const char *at)
{
    const char *next = skipSpace(at);
    return *next != '\0' && *next != '#' && *next != '\n' && *next != '\r';
}

bool FramingLine::read(const char *&at, SourceKey &key, PanAndZoom &framing,
                       Aspect &shape, Route &route)
{
    long lines = 0, width = 0;
    float rate = 0.0f;
    if (!number(at, lines))
        return false;
    at = skipSpace(at);
    if (*at++ != '@')
        return false;
    if (!rateFrom(at, rate))
        return false;
    at = skipSpace(at);
    if (*at++ != '/')
        return false;
    if (!number(at, width))
        return false;
    at = skipSpace(at);
    SourceKey::Polarity hsync = SourceKey::Undetermined;
    SourceKey::Polarity vsync = SourceKey::Undetermined;
    if (!polarityFrom(*at, hsync))
        return false;
    ++at;
    if (!polarityFrom(*at, vsync))
        return false;
    ++at;
    at = skipSpace(at);
    if (*at++ != '=')
        return false;

    long value[4];
    for (int i = 0; i < 4; ++i)
        if (!number(at, value[i]))
            return false;

    const SourceKey read((uint16_t)lines, rate, proportionOf(width),
                         hsync, vsync);
    if (!read.valid())
        return false;

    // Absent and unreadable are not the same: a record from before the shape
    // existed ends after the fourth number, and one whose fifth field is
    // rubbish is malformed like any other line. The sixth, the route, is the
    // same bargain one field later.
    long shown = Aspect::Fill;
    if (moreOnLine(at) && (!number(at, shown) || shown < 0 || shown > 0xFFFF))
        return false;

    Route chosen;
    if (moreOnLine(at)) {
        long passesThrough = 0;
        if (!number(at, passesThrough) || passesThrough < 0 || passesThrough > 1)
            return false;
        chosen = Route::chosen(passesThrough != 0);
    }

    key = read;
    route = chosen;
    framing = PanAndZoom(proportionOf(value[0]), proportionOf(value[1]),
                         proportionOf(value[2]), proportionOf(value[3]));
    shape = Aspect((uint16_t)shown);
    return true;
}

int FramingLine::writeKey(char *out, uint8_t size, const SourceKey &key)
{
    if (size == 0)
        return -1;

    const unsigned long hundredths = (unsigned long)lrintf(key.rateHz() * 100.0f);
    const int written = snprintf(
        out, size, "%u@%lu.%02lu/%ld%c%c",
        (unsigned)key.lines(), hundredths / 100uL, hundredths % 100uL,
        tenThousandthsOf(key.syncWidth()),
        symbolFor(key.hsyncPolarity()), symbolFor(key.vsyncPolarity()));

    return written > 0 && written < (int)size ? written : -1;
}

int FramingLine::write(char *out, uint8_t size, const SourceKey &key,
                       const PanAndZoom &framing, Aspect shape, Route route)
{
    const int named = writeKey(out, size, key);
    if (named < 0)
        return -1;

    const int room = (int)size - named;
    const int written = snprintf(
        out + named, (size_t)room, " = %ld %ld %ld %ld %u",
        tenThousandthsOf(framing.originOn(AxisHorizontal)),
        tenThousandthsOf(framing.extentOn(AxisHorizontal)),
        tenThousandthsOf(framing.originOn(AxisVertical)),
        tenThousandthsOf(framing.extentOn(AxisVertical)),
        (unsigned)shape.tenThousandths());

    if (written <= 0 || written >= room)
        return -1;
    if (!route.stored())
        return named + written;

    // Only where the user chose, because the field's absence IS the third
    // state: writing a 0 for an unchosen source stores a choice nobody made.
    const int tail = snprintf(out + named + written, (size_t)(room - written),
                              " %u", route.passesThrough() ? 1u : 0u);
    if (tail <= 0 || tail >= room - written)
        return -1;
    return named + written + tail;
}

}  // namespace Tv5725
