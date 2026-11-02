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
        const SourceKey stored = entries_[i].key();
        if (!(stored == key))
            continue;
        const float apart = key.distanceTo(stored);
        if (best < 0 || apart < nearest) {
            best = (int16_t)i;
            nearest = apart;
        }
    }
    return best;
}

int16_t FramingTable::routeIndexOf(const SourceKey &key) const
{
    if (!key.valid())
        return -1;

    int16_t best = -1;
    float nearest = 0.0f;
    for (uint16_t i = 0; i < count_; ++i) {
        const SourceKey stored = entries_[i].key();
        if (!entries_[i].route().stored()
            || !key.sameApartFromSyncWidth(stored))
            continue;
        const float apart = key.distanceTo(stored);
        if (best < 0 || apart < nearest) {
            best = (int16_t)i;
            nearest = apart;
        }
    }
    return best;
}

bool FramingTable::routeAcross(const SourceKey &key, Route route)
{
    bool changed = false;
    for (uint16_t i = 0; i < count_; ++i) {
        if (entries_[i].route() == route
            || !key.sameApartFromSyncWidth(entries_[i].key()))
            continue;
        entries_[i].reroute(route);
        changed = true;
    }
    return changed;
}

void FramingTable::collapseOnto(uint16_t kept, const SourceKey &identified)
{
    uint16_t out = 0;
    for (uint16_t i = 0; i < count_; ++i) {
        if (i != kept && entries_[i].key() == identified)
            continue;
        if (out != i)
            entries_[out] = entries_[i];
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
        *into = entries_[at].framing();
    if (shape)
        *shape = entries_[at].shape();
    return true;
}

bool FramingTable::findRoute(const SourceKey &key, Route *route) const
{
    const int16_t at = routeIndexOf(key);
    if (at < 0)
        return false;
    if (route)
        *route = entries_[at].route();
    return true;
}

bool FramingTable::remember(const SourceKey &key, const PanAndZoom &framing,
                            Aspect shape, Route route)
{
    if (!key.valid())
        return false;

    const int16_t at = indexOf(key);
    if (at < 0 && count_ >= Entries)
        return false;

    if (at >= 0) {
        entries_[at].reframe(framing, shape, route);
        collapseOnto((uint16_t)at, key);
    } else {
        entries_[count_] = FramingEntry(key, framing, shape, route);
        ++count_;
    }

    // A route nobody chose leaves the choice where it is: this is also the
    // write a framing press makes, and it carries the route it is not
    // changing rather than defaulting it.
    if (route.stored())
        routeAcross(key, route);

    moved();
    return true;
}

bool FramingTable::adopt(const FramingTable &other)
{
    bool whole = true;
    for (uint16_t i = 0; i < other.count(); ++i)
        if (!remember(other.keyAt(i), other.framingAt(i), other.aspectAt(i),
                      other.routeAt(i)))
            whole = false;
    return whole;
}

bool FramingTable::forget(const SourceKey &key)
{
    const bool cleared = routeAcross(key, Route());

    const int16_t at = indexOf(key);
    if (at < 0) {
        if (cleared)
            moved();
        return cleared;
    }

    // The last entry moves into the hole: the order carries no meaning, and
    // shuffling the tail would cost more than it says.
    entries_[at] = entries_[count_ - 1];
    --count_;
    moved();
    return true;
}

uint16_t FramingTable::count() const { return count_; }

uint16_t FramingTable::revision() const { return revision_; }

void FramingTable::moved() { ++revision_; }

SourceKey FramingTable::keyAt(uint16_t index) const
{
    return entries_[index].key();
}

PanAndZoom FramingTable::framingAt(uint16_t index) const
{
    return entries_[index].framing();
}

Aspect FramingTable::aspectAt(uint16_t index) const
{
    return entries_[index].shape();
}

Route FramingTable::routeAt(uint16_t index) const
{
    return entries_[index].route();
}

void FramingTable::clear()
{
    if (count_ == 0)
        return;
    count_ = 0;
    moved();
}

}  // namespace Tv5725
