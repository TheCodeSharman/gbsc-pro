#ifndef TV5725_SERRATION_MEASUREMENT_H
#define TV5725_SERRATION_MEASUREMENT_H

#include <stdint.h>

namespace Tv5725 {

// Measure whether the source's vertical interval carries serrations, and hold
// the answer.
//
// SP_H_PULSE_IGNOR has to sit above whatever a broadcast vertical interval puts
// at half-line spacing and below the source's own sync pulse, and across this
// bench's sources those two bounds DO NOT OVERLAP -- so no constant serves
// every source and the value is chosen from a measurement.
// docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md
class SerrationMeasurement {
public:
    // How the probe reads the source: apply the threshold that rejects
    // serrations, or the lower one that admits them, and report the line count
    // the sync processor then reaches.
    typedef uint16_t (*CountAtThreshold)(bool rejectsSerrations);

    // Serrated, as last measured or set. Never probes.
    static bool isSerrated();

    // What a caller choosing the threshold asks: serrated UNTIL MEASURED
    // OTHERWISE. The two errors are not equal -- the higher threshold costs an
    // unserrated source a dithering count until the probe runs, where the
    // lower one leaves a serrated source counting its vertical interval.
    static bool assumeSerrated();

    // Whether the held value came from a probe. A value merely set has not
    // been measured, so it must not suppress one.
    static bool isSet();

    static void set(bool serrated);

    static void forget();

    // The held answer, probing only if nothing has been measured yet.
    static bool serration(CountAtThreshold countAt);

    // Probe regardless, for the paths that have just moved the input.
    //
    // A reading no source produces establishes nothing: an unlatched divider
    // and a lost lock both report a count, and a difference between two of
    // those is arithmetic on noise. The probe then leaves the answer unset so
    // the next pass asks again.
    static bool probe(CountAtThreshold countAt);

    // How many lines the lower threshold must ADD before they are a vertical
    // interval rather than the count's own dither. Measured: the serrated
    // source adds nine, and the unserrated one that dithers worst spans four.
    static const uint8_t SerrationLines = 4;

    // How many readings each threshold is judged on. The count dithers, so one
    // pair can separate by as much as the interval does; the median of an odd
    // run of them cannot.
    static const uint8_t Samples = 5;

private:
    static bool serrated_;
    static bool set_;
};

}  // namespace Tv5725

#endif  // TV5725_SERRATION_MEASUREMENT_H
