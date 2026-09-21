#ifndef TV5725_SOURCE_KEY_H_
#define TV5725_SOURCE_KEY_H_

// What identifies a source, so a framing can be kept against it and stored
// against it. The line count and the field rate, because those are what
// this chip can see: it locks to sync edges and cannot know the pixel clock, so
// two modes differing only in that are one source here and one entry.
// docs/framing-presets.md

#include <stdint.h>

namespace Tv5725 {

// How far two field-rate readings may sit apart and still be the same source,
// in parts per thousand.
//
// **IT MUST NOT BE NARROWER THAN THE MOVEMENT TRIGGER**, which is asserted
// against SourceMeasurement in SourceKey.cpp. A rate change inside that trigger
// arms no mode change, so a key that moved there would swap the stored framing
// with no re-solve behind it.
//
// Nothing is lost at the wide end. AKF50's own line counts carrying more than
// one rate are 364, 449 and 525, and their rates sit 0.01% to 0.3% apart --
// modes differing only in pixel clock, which this chip cannot separate anyway.
// The frame time lock steers out what is left.
extern const uint16_t SourceIdentityPerMille;

// What the stored rate is quantised to, in steps per hertz. The key is what
// the output raster is generated from, so the quantisation lands in the raster
// whole: at a whole hertz, 60.317 is stored as 60 and the raster is 0.53%
// short, which is 11 px of a 2050 px line and went into every absolute
// geometry measured against it.
//
// Quantised at all so the raster is SOLVED ONCE per source, rather than
// re-solved on a reading that moved in its last digit. How fine that may be is
// a property of the instrument: measured with SamplingLog::rates(), 250
// samples a mode, the field rate is repeatable to 0.000% at both 311 x 50 and
// 627 x 60, so hundredths cost nothing.
// ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
const uint16_t RateStepsPerHz = 100;

// How far two sync-width readings may sit apart and still be the same source,
// as a fraction of the line.
//
// Wider than the reading moves and far narrower than the standards it has to
// tell apart. The reading dithers one ADC count while the source stands still
// -- 196 and 197 of 1606 over 2499 samples at 800x600@60, a spread of 0.0006 --
// and shifts 0.0019 when the source's sync type changes under it. The pair it
// exists to separate, DMT 640x480@60 and CEA 720x480p, sit 0.048 apart.
// ../../../docs/source-identity-and-framing-lookup.md
const float SyncWidthIdentity = 0.005f;

class SourceKey {
public:
    SourceKey();
    SourceKey(uint16_t sourceLines, float fieldRateHz, float syncWidth,
              bool vsyncPositive);

    // A count or a rate outside what any source runs identifies nothing, and
    // two of those are not each other: a settling source passes through counts
    // inside no standard at all.
    bool valid() const;

    uint16_t lines() const;

    // The hsync pulse as a fraction of the line. The count and the rate state
    // how often a line starts and nothing about how one is DIVIDED, and two
    // published standards share a count and a rate.
    float syncWidth() const;

    // Whether the source's vertical sync is positive-going, which is a property
    // of the MODE rather than of the arrangement carrying it.
    //
    // The horizontal polarity is not here and may not be. Measured across a
    // sync-type change on three modes, VSPOL agrees with what the mode states
    // on both types while HSPOL reads 0 on composite whatever the mode states:
    // the VIDC20's composite form on the HSync pin is a NOR, which has no
    // separate H line for the bit to report.
    // ../../../docs/source-identity-and-framing-lookup.md
    bool vsyncPositive() const;

    // A WHOLE NUMBER OF HERTZ, not the reading it was built from. The output
    // raster is generated from the key rather than from the measurement, so
    // what the key carries is what has to repeat: measured, the same unchanged
    // source reads 60.38 and 60.72 across mode changes, and a raster solved
    // from that moves 11 px between two solves of it.
    //
    // Nearest rather than truncated. Real modes are built to be "60 Hz" and
    // land on and just above the integers -- of the 63 the bench monitor
    // definition carries, 13 sit exactly on one -- so a boundary at the integer
    // runs through the middle of the cluster. At the half hertz it falls in the
    // gaps: 3 of the 63 come within 0.15 Hz of one, none of them a mode this
    // bench runs.
    float rateHz() const;

    bool operator==(const SourceKey &other) const;
    bool operator!=(const SourceKey &other) const;

private:
    uint16_t lines_;
    float rateHz_;
    float syncWidth_;
    bool vsyncPositive_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_KEY_H_
