#ifndef TV5725_EQUALISATION_MEASUREMENT_H
#define TV5725_EQUALISATION_MEASUREMENT_H

#include <stdint.h>

namespace Tv5725 {

// Measure whether the source's vertical interval carries EQUALISING PULSES --
// pulses at HALF-LINE spacing -- and hold the answer.
//
// SP_H_PULSE_IGNOR has to sit above them and below the source's own sync pulse,
// and across this bench's sources those two bounds DO NOT OVERLAP, so no
// constant serves every source and the value is chosen from a measurement.
// docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md
//
// **IT CANNOT SEE SERRATION, AND MUST NOT BE READ AS IF IT COULD.** A serrated
// broad pulse is one long low period per line, far above either threshold, so
// it clears both and is counted once per line -- the counts agree and the
// source reads as carrying nothing. Only half-line pulses move the count.
//
// So this is a BROADCAST EQUALISING PULSE DETECTOR and nothing more. Those
// pulses serve interlace, so their presence is strong evidence of it -- on
// this bench it separates the two interlaced Wii modes from every progressive
// source, where SourceMeasurement::measureScanType()'s count alternation
// misses 576i -- but the two facts are not the same and must not be
// substituted for one another.
//
// The Wii at 480p is the case that proves the distinction: it reads no
// equalisation here, and its vertical sync IS serrated -- it loses none of its
// 525 lines, where the RISC PC's unserrated NOR composite loses exactly its
// vertical sync width. **So a source reading no equalisation may still need
// the coast**, and the pre coast must not be gated on this answer alone.
// docs/investigations/the-equalisation-probe-cannot-see-serration.md
class EqualisationMeasurement {
public:
    // How the probe reads the source: apply the threshold that rejects
    // equalising pulses, or the lower one that admits them, and report the
    // line count the sync processor then reaches.
    typedef uint16_t (*CountAtThreshold)(bool rejectsEqualisation);

    // Carries equalisation, as last measured or set. Never probes.
    static bool carriesEqualisation();

    // What a caller choosing the threshold asks: EQUALISED UNTIL MEASURED
    // OTHERWISE. The two errors are not equal -- the higher threshold costs an
    // unequalised source a dithering count until the probe runs, where the
    // lower one leaves an equalised source counting its vertical interval.
    static bool assumeEqualisation();

    // Whether the held value came from a probe. A value merely set has not
    // been measured, so it must not suppress one.
    static bool isSet();

    static void set(bool equalised);

    static void forget();

    // The held answer, probing only if nothing has been measured yet.
    static bool equalisation(CountAtThreshold countAt);

    // Probe regardless, for the paths that have just moved the input.
    //
    // A reading no source produces establishes nothing: an unlatched divider
    // and a lost lock both report a count, and a difference between two of
    // those is arithmetic on noise. The probe then leaves the answer unset so
    // the next pass asks again.
    static bool probe(CountAtThreshold countAt);

    // How many lines the lower threshold must ADD before they are equalising
    // pulses rather than the count's own dither. Measured: the Wii in 576i
    // adds nine and in 480i ten, where the unequalised source that dithers
    // worst spans four.
    static const uint8_t EqualisationLines = 4;

    // How many readings each threshold is judged on. The count dithers, so one
    // pair can separate by as much as the interval does; the median of an odd
    // run of them cannot.
    static const uint8_t Samples = 5;

private:
    static bool equalised_;
    static bool set_;
};

}  // namespace Tv5725

#endif  // TV5725_EQUALISATION_MEASUREMENT_H
