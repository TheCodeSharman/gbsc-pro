#include "TestBusRateMeasurement.h"

#include <Arduino.h>   // delayMicroseconds(), a hardware settling time

#include "InputFormatter.h"
#include "SyncMeasurement.h"
#include "SyncProcessor.h"
#include "TestBus.h"
#include "Tv5725.h"

// The ESP's edge counter on the debug pin, defined by the sketch the way
// tv5725Log() is: the count comes from two ISRs reading the CPU's cycle
// counter, which this layer can neither reach nor host-compile. 0 ticks means
// no pulse arrived.
uint32_t debugPinPulseTicks();
uint32_t debugPinTicksPerSecond();

namespace Tv5725 {

// RD-5725-1.1 tabulates SP_TEST_MODULE's values and says nothing about
// SP_TEST_SIGNAL_SEL's, so the stage is named and the signal is not.
const uint8_t StageSignalFirst = 0;
const uint8_t CsSepSignal = 6;

float TestBusRateMeasurement::rateFrom(uint32_t ticks)
{
    if (ticks == 0)
        return 0;
    return (float)((double)debugPinTicksPerSecond() / (double)ticks);
}

// A sample that reports no ticks timed out rather than measuring 0 Hz, so it is
// worth one more. What the rate is worth afterwards is the caller's to judge:
// SourceMeasurement cross-checks it against the line count and requires two
// readings to agree, which is a test a second sample here cannot do.
float TestBusRateMeasurement::measureRateHz()
{
    uint32_t period = debugPinPulseTicks();
    if (period == 0)
        period = debugPinPulseTicks();

    return rateFrom(period);
}

float TestBusRateMeasurement::sourceFieldRateHz(bool useSyncProcessorBus)
{
    InputFormatter::IF_TEST_SEL::write(3);

    if (useSyncProcessorBus) {
        TestBus::select(SyncMeasurement::isCsync() ? TestBus::SyncProcessor
                                                   : TestBus::InputVsync);
        SyncProcessor::driveTestBus(SyncProcessor::TestModuleOutProc,
                                    StageSignalFirst);
    } else {
        TestBus::select(TestBus::InputVsync);
    }

    return measureRateHz();
}

float TestBusRateMeasurement::outputFrameRateHz()
{
    TestBus::select(TestBus::OutputVsync);

    return measureRateHz();
}

uint32_t TestBusRateMeasurement::pllRateHz()
{
    TestBus::select(TestBus::SyncProcessor);

    // The composite path watches the sync separator; the separate path watches
    // vertical sync activity, which on a composite source is not there to see.
    if (SyncMeasurement::isCsync())
        SyncProcessor::driveTestBus(SyncProcessor::TestModuleCsSep, CsSepSignal);
    else
        SyncProcessor::driveTestBus(SyncProcessor::TestModuleVsActDet,
                                    StageSignalFirst);

    delayMicroseconds(200);

    // Integer division, where the rates above are floats: this one is in MHz,
    // where a float's mantissa has already run out.
    const uint32_t period = debugPinPulseTicks();
    return period != 0 ? debugPinTicksPerSecond() / period : 0;
}

}  // namespace Tv5725
