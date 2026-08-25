#ifndef TV5725_SLOT_TEXT_H_
#define TV5725_SLOT_TEXT_H_

// The slot table as lines of text, one record a line:
//
//   3 311@50 = 364 8525 740 8553
//
// The slot, then the record FramingLine owns -- the source and the four
// proportions in ten-thousandths of the capturable region.
//
// Reading and writing the file belongs to the caller. This turns one line into
// a record and one record into a line, which is what makes the format testable
// on the host. docs/framing-presets.md

#include <stdint.h>

#include "SlotTable.h"

namespace Tv5725 {

class SlotText {
public:
    explicit SlotText(SlotTable &table);

    // One line of the file. A comment, a blank line or anything malformed is
    // skipped: a partial file must degrade for the lines it lacks only, never
    // for the whole of it.
    void readLine(const char *line);

    // One record as a line, NUL terminated. False when there is no such record,
    // or when it would not fit -- a truncated record reads back as a skipped
    // one, which is a lost tuning reported as nothing at all.
    bool writeLine(uint16_t index, char *out, uint8_t size) const;

private:
    SlotTable &table_;
};

}  // namespace Tv5725

#endif  // TV5725_SLOT_TEXT_H_
