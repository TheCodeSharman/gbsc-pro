#ifndef TV5725_FRAMING_LINE_H_
#define TV5725_FRAMING_LINE_H_

// One stored framing as text, and back:
//
//   311@50 = 364 8525 740 8553
//
// The line count and the field-rate bucket name the source; the four numbers
// are the origin and extent of each axis in ten-thousandths of the capturable
// region. Integers because the ESP's printf has no %f, and ten-thousandths
// because one input unit is at least eight of them on any line this chip
// captures -- so the window a framing describes survives the round trip exactly
// even though the float does not.
//
// The grammar has one owner because more than one file is written in it: the
// per-source table and the numbered slots, which prefix the same record with a
// slot. docs/framing-presets.md

#include <stdint.h>

#include "PanAndZoom.h"
#include "SourceKey.h"

namespace Tv5725 {

class FramingLine {
public:
    // A comment or a blank line, which a caller skips rather than rejects.
    static bool empty(const char *line);

    // Reads one record, leaving `at` past it. False on anything malformed or on
    // a key no source runs, which is what a line written while the source was
    // settling holds.
    static bool read(const char *&at, SourceKey &key, PanAndZoom &framing);

    // The characters written, or a negative number when it would not fit -- a
    // truncated record reads back as a skipped one, which is a lost tuning
    // reported as nothing at all.
    static int write(char *out, uint8_t size, const SourceKey &key,
                     const PanAndZoom &framing);

    // Past spaces and tabs. Callers need it to find what follows a record.
    static const char *skipSpace(const char *at);

    // One unsigned field, leaving `at` past it.
    static bool number(const char *&at, long &into);

private:
    static float proportionOf(long tenThousandths);
    static long tenThousandthsOf(float proportion);
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_LINE_H_
