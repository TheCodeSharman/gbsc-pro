#include "BlankingTiming.h"

namespace Tv5725 {

BlankingTiming::BlankingTiming() : stop_(0), start_(0) {}

BlankingTiming::BlankingTiming(uint16_t stop, uint16_t start) : stop_(stop), start_(start) {}

uint16_t BlankingTiming::stop() const { return stop_; }

uint16_t BlankingTiming::start() const { return start_; }

uint16_t BlankingTiming::width() const { return start_ > stop_ ? start_ - stop_ : 0; }

bool BlankingTiming::usable() const { return start_ > stop_; }

}  // namespace Tv5725
