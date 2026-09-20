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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"

// The FIRST reading past the gate, whether or not the rate has repeated. For a
// case about the settling itself.
// One pass of both halves, the way the acquisition layer drives them: the rate,
// then the duty through whatever clock is in force. A case wanting the clock
// INSTALLED between the two is an engine case, not a measurement one.
inline Tv5725::SourceMeasurement::MeasurementStatus
measureOnce(Tv5725::SourceMeasurement &sampling)
{
    const Tv5725::SourceMeasurement::MeasurementStatus rate = sampling.measureRate();
    if (rate != Tv5725::SourceMeasurement::Measured)
        return rate;
    return sampling.measureDuty();
}

inline Tv5725::SourceMeasurement::MeasurementStatus
measureToFirstReading(Tv5725::SourceMeasurement &sampling)
{
    Tv5725::SourceMeasurement::MeasurementStatus reading
        = Tv5725::SourceMeasurement::NotSteady;
    for (uint8_t pass = 0;
         pass < 2 * (Tv5725::SourceMeasurement::SteadySamples
                     + Tv5725::SourceMeasurement::LatchSettlePasses); ++pass) {
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
                     + Tv5725::SourceMeasurement::LatchSettlePasses); ++pass) {
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
