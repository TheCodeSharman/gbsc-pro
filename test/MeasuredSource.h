#ifndef TEST_MEASURED_SOURCE_H_
#define TEST_MEASURED_SOURCE_H_

// Driving Tv5725::SourceMeasurement the way the engine does.
//
// measure() is the one entry point, asked on every pass, and the cheap gate
// inside it needs SteadySamples agreeing counts before anything else is read at
// all. A case about what was MEASURED therefore has to get past the gate first,
// which is several passes rather than one call.
//
// Settling is passed through too: the rate a later reading is judged against is
// only ever a SETTLED one, so a case that wants a held rate has to reach
// Measured rather than stop at the first reading. RateMeasured is passed
// through as well -- the engine installs a sampling clock there, and a case
// that does not is asking for the duty through the divider already in force.

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"

// The retime stop the engine would write for what a measurement holds. The
// counter's origin carries the source's video lag in the SOURCE's own pixels,
// which only a published raster states, so a case asserting the register
// follows the rule resolves the raster the way the engine does.
inline uint16_t retimeStopWanted(const Tv5725::SourceMeasurement &sampling,
                                 uint16_t divider, uint8_t oversample)
{
    const Tv5725::SourceKey key(sampling.sourceLines(), sampling.fieldRateHz(),
                                sampling.hsync().sourceDuty(),
                                sampling.hsyncPolarity(),
                                sampling.vsyncPolarity());
    return Tv5725::SyncProcessor::retimeStopFor(
        divider, sampling.hsync(), oversample,
        Tv5725::SourceTiming::matching(key).pixelSamples(divider));
}

// The clock a pass is taken at. Nothing here is about the engine's cadence, so
// it moves a whole latch settle a pass and a helper loop spends one on the pass
// after the latch.
inline uint32_t &passClockMs()
{
    static uint32_t ms = 0;
    return ms;
}

// One pass of both halves, the way the acquisition layer drives them: the rate,
// then the duty through whatever clock is in force. A case wanting the clock
// INSTALLED between the two is an engine case, not a measurement one.
inline Tv5725::SourceMeasurement::MeasurementStatus
measureOnce(Tv5725::SourceMeasurement &sampling)
{
    passClockMs() += Tv5725::SourceMeasurement::LatchSettleMs;
    const Tv5725::SourceMeasurement::MeasurementStatus rate
        = sampling.measureRate(passClockMs());
    if (rate != Tv5725::SourceMeasurement::Measured)
        return rate;
    return sampling.measureDuty(passClockMs());
}

// THE RATE'S OWN ANSWER for this pass, for a case about the rate rather than
// about the whole measurement. The duty still runs underneath, because
// measureDuty() is what releases the rate to be read again -- a run of
// measureRate() calls alone reports the held rate back and re-reads nothing.
inline Tv5725::SourceMeasurement::MeasurementStatus
rateOnce(Tv5725::SourceMeasurement &sampling)
{
    passClockMs() += Tv5725::SourceMeasurement::LatchSettleMs;
    const Tv5725::SourceMeasurement::MeasurementStatus rate
        = sampling.measureRate(passClockMs());
    if (rate == Tv5725::SourceMeasurement::Measured)
        sampling.measureDuty(passClockMs());
    return rate;
}

// The FIRST reading past the gate, whether or not the rate has repeated. For a
// case about the settling itself.
inline Tv5725::SourceMeasurement::MeasurementStatus
measureToFirstReading(Tv5725::SourceMeasurement &sampling)
{
    Tv5725::SourceMeasurement::MeasurementStatus reading
        = Tv5725::SourceMeasurement::NotSteady;
    for (uint8_t pass = 0;
         pass < 2 * (Tv5725::SourceMeasurement::SteadySamples
                     + Tv5725::SourceMeasurement::RateAgreementAttempts); ++pass) {
        reading = measureOnce(sampling);
        if (reading != Tv5725::SourceMeasurement::NotSteady
            && reading != Tv5725::SourceMeasurement::ClockSettling)
            return reading;
    }
    return reading;
}

inline Tv5725::SourceMeasurement::MeasurementStatus
measurePastGate(Tv5725::SourceMeasurement &sampling)
{
    Tv5725::SourceMeasurement::MeasurementStatus reading
        = Tv5725::SourceMeasurement::NotSteady;
    for (uint8_t pass = 0;
         pass < 2 * (Tv5725::SourceMeasurement::SteadySamples
                     + Tv5725::SourceMeasurement::RateAgreementAttempts); ++pass) {
        reading = measureOnce(sampling);
        if (reading != Tv5725::SourceMeasurement::NotSteady
            && reading != Tv5725::SourceMeasurement::ClockSettling
            && reading != Tv5725::SourceMeasurement::Settling)
            return reading;
    }
    return reading;
}

// A rate was measured, whether or not it has repeated often enough to size a
// raster from.
inline bool rateMeasured(Tv5725::SourceMeasurement::MeasurementStatus reading)
{
    return reading == Tv5725::SourceMeasurement::Settling
        || reading == Tv5725::SourceMeasurement::Measured;
}

#endif  // TEST_MEASURED_SOURCE_H_
