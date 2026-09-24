#include "OutputTiming.h"

namespace Tv5725 {

OutputTiming::OutputTiming()
    : horizontalTotal(0), verticalTotal(0), divider(0), hsyncStart(0), hsyncStop(0),
      vsyncStart(0), vsyncStop(0), activeStart(0), activeLinesStart(0),
      activeStop(0), activeLinesStop(0), fieldRate(0.0f) {}

bool OutputTiming::usable() const
{
    return horizontalTotal != 0 && verticalTotal != 0 && divider != 0 && fieldRate > 0.0f;
}

uint32_t OutputTiming::demandedHz() const
{
    if (!usable())
        return 0;
    return (uint32_t)((float)horizontalTotal * (float)verticalTotal * fieldRate);
}

uint16_t OutputTiming::activeWidth() const
{
    return activeStart >= activeStop ? 0 : (uint16_t)(activeStop - activeStart);
}

}  // namespace Tv5725
