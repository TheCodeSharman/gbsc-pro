#include "SteadyRun.h"

namespace Tv5725 {

SteadyRun::SteadyRun(uint8_t samples)
    : samples_(samples), run_(0), high_(0), low_(0)
{
}

void SteadyRun::restart(uint16_t value)
{
    high_ = value;
    low_ = value;
    run_ = 0;
}

void SteadyRun::settle(uint16_t value)
{
    high_ = value;
    low_ = value;
    run_ = samples_;
}

void SteadyRun::reset() { restart(0); }

uint16_t SteadyRun::value() const { return high_; }

bool SteadyRun::alternated() const { return run_ >= samples_ && low_ != high_; }

bool SteadyRun::sample(uint16_t value)
{
    if (value != high_ && value != low_) {
        const bool widensByOne =
            run_ > 0 && low_ == high_
            && (value == (uint16_t)(high_ + 1) || (uint16_t)(value + 1) == high_);
        if (!widensByOne) {
            high_ = value;
            low_ = value;
            run_ = 1;
            return false;
        }
        if (value > high_)
            high_ = value;
        else
            low_ = value;
    }

    if (run_ < samples_)
        ++run_;
    return run_ >= samples_;
}

}  // namespace Tv5725
