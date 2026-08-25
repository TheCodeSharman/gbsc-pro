#ifndef TV5725_FRAMING_TEXT_H_
#define TV5725_FRAMING_TEXT_H_

// The framing table as lines of text, one source a line:
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
// Reading and writing the file belongs to the caller. This turns one line into
// an entry and one entry into a line, which is what makes the format testable
// on the host. docs/framing-presets.md

#include <stdint.h>

#include "FramingTable.h"

namespace Tv5725 {

class FramingText {
public:
    explicit FramingText(FramingTable &table);

    // One line of the file. A comment, a blank line or anything malformed is
    // skipped: a partial file must degrade for the lines it lacks only, never
    // for the whole of it.
    void readLine(const char *line);

    // One entry as a line, NUL terminated. False when there is no such entry,
    // or when it would not fit -- a truncated record reads back as a skipped
    // one, which is a lost tuning reported as nothing at all.
    bool writeLine(uint16_t index, char *out, uint8_t size) const;

private:
    FramingTable &table_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_TEXT_H_
