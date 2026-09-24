#include "SteadyRun.h"

namespace Tv5725 {

const uint8_t SteadyRun::CollapseSamples;

SteadyRun::SteadyRun(uint8_t samples)
    : samples_(samples), run_(0), same_(0), high_(0), low_(0), latest_(0)
{
}

bool SteadyRun::agree(uint16_t a, uint16_t b)
{
    return (a > b ? a - b : b - a) <= 1;
}

void SteadyRun::restart(uint16_t value)
{
    high_ = value;
    low_ = value;
    latest_ = value;
    run_ = 0;
    same_ = 0;
}

void SteadyRun::settle(uint16_t value)
{
    high_ = value;
    low_ = value;
    latest_ = value;
    run_ = samples_;
    same_ = 0;
}

void SteadyRun::reset() { restart(0); }

uint16_t SteadyRun::value() const { return high_; }

bool SteadyRun::settled() const { return run_ >= samples_; }

bool SteadyRun::alternated() const { return settled() && low_ != high_; }

bool SteadyRun::sample(uint16_t value)
{
    same_ = value == latest_ && same_ < 0xFF ? (uint8_t)(same_ + 1) : 1;
    latest_ = value;

    if (value != high_ && value != low_) {
        const bool widensByOne = run_ > 0 && low_ == high_ && agree(value, high_);
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

    // A widened pair holds BOTH values an interlaced field alternates between,
    // and nothing else narrows it -- a steady stream matches one end and takes
    // the branch above no further. So a source that alternates once reads as
    // interlaced for the life of the run. A long enough run of one value is
    // what says the alternation stopped.
    if (low_ != high_ && same_ >= CollapseSamples) {
        high_ = value;
        low_ = value;
    }

    if (run_ < samples_)
        ++run_;
    return run_ >= samples_;
}

}  // namespace Tv5725
