#ifndef TEST_MEASURED_SOURCE_H_
#define TEST_MEASURED_SOURCE_H_

// Driving Tv5725::SourceMeasurement the way the engine does.
//
// measure() is the one entry point, asked on every pass, and the cheap gate
// inside it needs SteadySamples agreeing counts before anything else is read at
// all. A case about what was MEASURED therefore has to get past the gate first,
// which is several passes rather than one call.

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceMeasurement.h"

inline Tv5725::SourceMeasurement::Reading
measurePastGate(Tv5725::SourceMeasurement &sampling)
{
    Tv5725::SourceMeasurement::Reading reading
        = Tv5725::SourceMeasurement::NotSteady;
    for (uint8_t pass = 0;
         pass < 2 * Tv5725::SourceMeasurement::SteadySamples; ++pass) {
        reading = sampling.measure();
        if (reading != Tv5725::SourceMeasurement::NotSteady)
            return reading;
    }
    return reading;
}

// A rate was measured, whether or not it has repeated often enough to size a
// raster from.
inline bool rateMeasured(Tv5725::SourceMeasurement::Reading reading)
{
    return reading == Tv5725::SourceMeasurement::Settling
        || reading == Tv5725::SourceMeasurement::Measured;
}

#endif  // TEST_MEASURED_SOURCE_H_
