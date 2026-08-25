#include "SlotTable.h"

namespace Tv5725 {

const uint16_t SlotTable::Records;

SlotTable::SlotTable() : count_(0)
{
    for (uint16_t i = 0; i < Records; ++i)
        slots_[i] = 0;
}

int16_t SlotTable::indexOf(uint8_t slot, const SourceKey &key) const
{
    if (!key.valid())
        return -1;
    for (uint16_t i = 0; i < count_; ++i)
        if (slots_[i] == slot && keys_[i] == key)
            return (int16_t)i;
    return -1;
}

bool SlotTable::find(uint8_t slot, const SourceKey &key, PanAndZoom *into) const
{
    const int16_t at = indexOf(slot, key);
    if (at < 0)
        return false;
    if (into)
        *into = framings_[at];
    return true;
}

bool SlotTable::remember(uint8_t slot, const SourceKey &key,
                         const PanAndZoom &framing)
{
    if (!key.valid())
        return false;

    const int16_t at = indexOf(slot, key);
    if (at >= 0) {
        framings_[at] = framing;
        return true;
    }
    if (count_ >= Records)
        return false;

    slots_[count_] = slot;
    keys_[count_] = key;
    framings_[count_] = framing;
    ++count_;
    return true;
}

void SlotTable::removeAt(uint16_t index)
{
    // The last record moves into the hole: the order carries no meaning, and
    // shuffling the tail would cost more than it says.
    slots_[index] = slots_[count_ - 1];
    keys_[index] = keys_[count_ - 1];
    framings_[index] = framings_[count_ - 1];
    --count_;
}

bool SlotTable::forget(uint8_t slot)
{
    bool removed = false;
    for (uint16_t i = 0; i < count_;) {
        if (slots_[i] == slot) {
            removeAt(i);
            removed = true;
        } else {
            ++i;
        }
    }
    return removed;
}

uint16_t SlotTable::count() const { return count_; }

uint8_t SlotTable::slotAt(uint16_t index) const { return slots_[index]; }

const SourceKey &SlotTable::keyAt(uint16_t index) const { return keys_[index]; }

const PanAndZoom &SlotTable::framingAt(uint16_t index) const
{
    return framings_[index];
}

void SlotTable::clear() { count_ = 0; }

}  // namespace Tv5725
