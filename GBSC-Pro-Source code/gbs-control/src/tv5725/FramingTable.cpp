#include "FramingTable.h"

namespace Tv5725 {

const uint16_t FramingTable::Entries;

FramingTable::FramingTable() : count_(0), revision_(0) {}

int16_t FramingTable::indexOf(const SourceKey &key) const
{
    if (!key.valid())
        return -1;

    int16_t best = -1;
    float nearest = 0.0f;
    for (uint16_t i = 0; i < count_; ++i) {
        if (!(keys_[i] == key))
            continue;
        const float apart = key.distanceTo(keys_[i]);
        if (best < 0 || apart < nearest) {
            best = (int16_t)i;
            nearest = apart;
        }
    }
    return best;
}

void FramingTable::collapseOnto(uint16_t kept, const SourceKey &identified)
{
    uint16_t out = 0;
    for (uint16_t i = 0; i < count_; ++i) {
        if (i != kept && keys_[i] == identified)
            continue;
        if (out != i) {
            keys_[out] = keys_[i];
            framings_[out] = framings_[i];
            aspects_[out] = aspects_[i];
        }
        ++out;
    }
    count_ = out;
}

bool FramingTable::find(const SourceKey &key, PanAndZoom *into,
                        Aspect *shape) const
{
    const int16_t at = indexOf(key);
    if (at < 0)
        return false;
    if (into)
        *into = framings_[at];
    if (shape)
        *shape = aspects_[at];
    return true;
}

bool FramingTable::remember(const SourceKey &key, const PanAndZoom &framing,
                            Aspect shape)
{
    if (!key.valid())
        return false;

    const int16_t at = indexOf(key);
    if (at >= 0) {
        framings_[at] = framing;
        aspects_[at] = shape;
        collapseOnto((uint16_t)at, key);
        moved();
        return true;
    }
    if (count_ >= Entries)
        return false;

    keys_[count_] = key;
    framings_[count_] = framing;
    aspects_[count_] = shape;
    ++count_;
    moved();
    return true;
}

bool FramingTable::adopt(const FramingTable &other)
{
    bool whole = true;
    for (uint16_t i = 0; i < other.count(); ++i)
        if (!remember(other.keyAt(i), other.framingAt(i), other.aspectAt(i)))
            whole = false;
    return whole;
}

bool FramingTable::forget(const SourceKey &key)
{
    const int16_t at = indexOf(key);
    if (at < 0)
        return false;

    // The last entry moves into the hole: the order carries no meaning, and
    // shuffling the tail would cost more than it says.
    keys_[at] = keys_[count_ - 1];
    framings_[at] = framings_[count_ - 1];
    aspects_[at] = aspects_[count_ - 1];
    --count_;
    moved();
    return true;
}

uint16_t FramingTable::count() const { return count_; }

uint16_t FramingTable::revision() const { return revision_; }

void FramingTable::moved() { ++revision_; }

const SourceKey &FramingTable::keyAt(uint16_t index) const { return keys_[index]; }

const PanAndZoom &FramingTable::framingAt(uint16_t index) const
{
    return framings_[index];
}

Aspect FramingTable::aspectAt(uint16_t index) const { return aspects_[index]; }

void FramingTable::clear()
{
    if (count_ == 0)
        return;
    count_ = 0;
    moved();
}

}  // namespace Tv5725
