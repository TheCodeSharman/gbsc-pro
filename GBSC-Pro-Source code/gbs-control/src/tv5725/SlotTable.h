#ifndef TV5725_SLOT_TABLE_H_
#define TV5725_SLOT_TABLE_H_

// The framing a user stored in a numbered slot, against the source it was
// stored for. The per-source table remembers the last framing on its own; a
// slot is the one the user chose to keep and named, and there may be several
// for one source.
//
// Pure: it holds records and answers lookups. Reading and writing the file is
// somebody else's job. docs/framing-presets.md

#include <stdint.h>

#include "PanAndZoom.h"
#include "SourceKey.h"

namespace Tv5725 {

class SlotTable {
public:
    // Bounded because it lives in the ESP8266's globals, and refusing when full
    // is the same policy the per-source table states: dropping the oldest loses
    // work the user did with nothing said.
    static const uint16_t Records = 16;

    SlotTable();

    // The framing this slot holds for this source. `into` may be null when only
    // the presence matters.
    bool find(uint8_t slot, const SourceKey &key, PanAndZoom *into) const;

    // Replaces the record for the same slot and source rather than adding one.
    // False when the key identifies nothing, or when the table is full and this
    // is new.
    bool remember(uint8_t slot, const SourceKey &key, const PanAndZoom &framing);

    // Every record this slot holds, whatever source. False when it held none.
    bool forget(uint8_t slot);

    uint16_t count() const;

    // For whoever writes the file out.
    uint8_t slotAt(uint16_t index) const;
    const SourceKey &keyAt(uint16_t index) const;
    const PanAndZoom &framingAt(uint16_t index) const;

    void clear();

private:
    int16_t indexOf(uint8_t slot, const SourceKey &key) const;
    void removeAt(uint16_t index);

    uint8_t slots_[Records];
    SourceKey keys_[Records];
    PanAndZoom framings_[Records];
    uint16_t count_;
};

}  // namespace Tv5725

#endif  // TV5725_SLOT_TABLE_H_
