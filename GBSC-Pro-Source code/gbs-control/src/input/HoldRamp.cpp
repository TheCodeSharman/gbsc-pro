#include "HoldRamp.h"

const uint32_t HoldRamp::RepeatCode;
const unsigned long HoldRamp::RunGapMs;
const uint8_t HoldRamp::DeadRepeats;
const int16_t HoldRamp::MaxMultiplier;
const uint16_t HoldRamp::RepeatsPerRate;

HoldRamp::HoldRamp() : lastKey_(0), lastAt_(0), run_(0), held_(false) {}

int16_t HoldRamp::multiplierFor(uint32_t key, unsigned long nowMs)
{
    if (key == RepeatCode) {
        if (!held_)
            return 1;       // a repeat with nothing before it means nothing
        key = lastKey_;
    }

    if (!held_ || key != lastKey_ || (nowMs - lastAt_) > RunGapMs)
        run_ = 0;

    lastKey_ = key;
    lastAt_ = nowMs;
    held_ = true;
    ++run_;

    uint16_t heldRepeats = run_ - 1;    // the first press is not a repeat
    if (heldRepeats <= DeadRepeats)
        return 1;
    return rateFor(heldRepeats - DeadRepeats);
}

uint32_t HoldRamp::resolve(uint32_t key) const
{
    return (key == RepeatCode && held_) ? lastKey_ : key;
}

void HoldRamp::release()
{
    held_ = false;
    run_ = 0;
}

int16_t HoldRamp::rateFor(uint16_t repeatsPastDeadTime)
{
    int16_t multiplier = 2;
    for (uint16_t doublings = (repeatsPastDeadTime - 1) / RepeatsPerRate;
         doublings > 0; --doublings) {
        multiplier *= 2;
        if (multiplier >= MaxMultiplier)
            return MaxMultiplier;
    }
    return multiplier;
}
