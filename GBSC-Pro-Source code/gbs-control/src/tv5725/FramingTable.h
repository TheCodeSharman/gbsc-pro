#ifndef TV5725_FRAMING_TABLE_H_
#define TV5725_FRAMING_TABLE_H_

// The framing a user tuned, kept against the source it was tuned for, so
// leaving a source and coming back restores it with nobody touching a control.
//
// Pure: it holds entries and answers lookups. Reading and writing the file is
// somebody else's job, because the arithmetic is testable on the host and the
// filesystem is not. docs/framing-presets.md

#include <stdint.h>

#include "PanAndZoom.h"
#include "SourceKey.h"

namespace Tv5725 {

class FramingTable {
public:
    // Bounded because it lives in the ESP8266's globals. Refusing when full is
    // the stated policy: dropping the oldest entry loses work the user did with
    // nothing said, where a refusal is visible and one can be cleared.
    static const uint16_t Entries = 16;

    FramingTable();

    // The framing stored against this source, if there is one. `into` may be
    // null when only the presence matters.
    bool find(const SourceKey &key, PanAndZoom *into) const;

    // Replaces an entry for the same source rather than adding one, so
    // re-tuning does not fill the table with its own history. False when the
    // key identifies nothing, or when the table is full and this is new.
    bool remember(const SourceKey &key, const PanAndZoom &framing);

    bool forget(const SourceKey &key);

    uint16_t count() const;

    // For whoever writes the file out.
    const SourceKey &keyAt(uint16_t index) const;
    const PanAndZoom &framingAt(uint16_t index) const;

    void clear();

private:
    int16_t indexOf(const SourceKey &key) const;

    SourceKey keys_[Entries];
    PanAndZoom framings_[Entries];
    uint16_t count_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_TABLE_H_
