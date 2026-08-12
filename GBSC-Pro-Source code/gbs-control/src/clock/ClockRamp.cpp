#include "ClockRamp.h"

namespace Clock {

const uint32_t ClockRamp::StepHz;
const uint32_t ClockRamp::RampLimitHz;

bool ClockRamp::ramps(uint32_t fromHz, uint32_t toHz)
{
    if (fromHz == toHz)
        return false;
    uint32_t delta = fromHz > toHz ? fromHz - toHz : toHz - fromHz;
    return delta < RampLimitHz;
}

uint32_t ClockRamp::advance(uint32_t currentHz, uint32_t targetHz)
{
    if (!ramps(currentHz, targetHz))
        return targetHz;

    // The original's two branches, which used the same step and the same
    // threshold in each direction. Reproduced as one.
    if (currentHz > targetHz + StepHz)
        return currentHz - StepHz;
    if (currentHz + StepHz < targetHz)
        return currentHz + StepHz;
    return targetHz;
}

uint32_t ClockRamp::preloadFor(uint32_t targetHz)
{
    if (targetHz == 108000000u)
        return 87000000u;
    if (targetHz == 40500000u)
        return 48500000u;
    return 0;
}

}  // namespace Clock
