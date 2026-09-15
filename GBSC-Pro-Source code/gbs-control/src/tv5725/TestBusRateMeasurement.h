#ifndef TV5725_TEST_BUS_RATE_MEASUREMENT_H_
#define TV5725_TEST_BUS_RATE_MEASUREMENT_H_

#include <stdint.h>

namespace Tv5725 {

// Measures the rate of a signal on the test bus: select what the debug pin
// carries, count its edges, convert to Hz.
class TestBusRateMeasurement {
public:
    static const uint8_t InputVsync = 0x0;
    static const uint8_t OutputVsync = 0x2;    // the VDS bus
    static const uint8_t SyncProcessorBus = 0xa;

    // Which signal the debug pin carries. It stays where the last measurement
    // left it, so anything reading the pin selects before it reads.
    static void select(uint8_t signal);

    // The source's field rate, timed off the sync processor's bus or the input
    // formatter's. 0 where no pulse arrives, which is also the no-lock answer.
    static float sourceFieldRateHz(bool useSyncProcessorBus);

    // The output's frame rate, timed off the VDS bus.
    static float outputFrameRateHz();

    // The ADC PLL's rate. Whole Hz, where the two above are floats: this one is
    // in megahertz, where a float's mantissa has already run out.
    static uint32_t pllRateHz();

private:
    static float rateFrom(uint32_t ticks);
    static float measureRateHz();
};

}  // namespace Tv5725

#endif  // TV5725_TEST_BUS_RATE_MEASUREMENT_H_
