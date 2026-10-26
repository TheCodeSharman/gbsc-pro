#include "BusHistogram.h"

namespace Tv5725 {

const uint8_t BusHistogram::Slots;

BusHistogram::BusHistogram() { clear(); }

void BusHistogram::clear()
{
    count_ = 0;
    total_ = 0;
    lowest_ = 0;
    highest_ = 0;
    distinct_ = 0;
    overflowed_ = false;
    for (uint8_t slot = 0; slot < Slots; ++slot) {
        value_[slot] = 0;
        seen_[slot] = 0;
    }
}

void BusHistogram::add(uint16_t value)
{
    if (count_ == 0 || value < lowest_)
        lowest_ = value;
    if (count_ == 0 || value > highest_)
        highest_ = value;
    ++count_;
    total_ += value;

    for (uint8_t slot = 0; slot < distinct_; ++slot) {
        if (value_[slot] == value) {
            ++seen_[slot];
            return;
        }
    }
    if (distinct_ < Slots) {
        value_[distinct_] = value;
        seen_[distinct_] = 1;
        ++distinct_;
        return;
    }
    overflowed_ = true;
}

uint32_t BusHistogram::count() const { return count_; }

uint16_t BusHistogram::lowest() const { return lowest_; }

uint16_t BusHistogram::highest() const { return highest_; }

uint16_t BusHistogram::mean() const
{
    return count_ == 0 ? 0 : (uint16_t)(total_ / count_);
}

uint8_t BusHistogram::distinct() const { return distinct_; }

bool BusHistogram::overflowed() const { return overflowed_; }

uint16_t BusHistogram::valueAt(uint8_t slot) const
{
    return slot < distinct_ ? value_[slot] : 0;
}

uint32_t BusHistogram::countAt(uint8_t slot) const
{
    return slot < distinct_ ? seen_[slot] : 0;
}

}  // namespace Tv5725
