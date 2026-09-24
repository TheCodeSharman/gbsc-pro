#ifndef CLOCK_RATE_AGREEMENT_H
#define CLOCK_RATE_AGREEMENT_H

namespace Clock {

// Whether two measurements of the same frame rate are close enough that either
// can be acted on.
//
// The display clock is set to the RATIO of two of these, and nothing
// re-measures until the next solve -- so a pair that agrees and is wrong beats
// against the source for as long as the boot runs. Measured over six restarts,
// three of them shook. docs/known-issues.md
class RateAgreement {
public:
    // **THE PART QUANTISES, AND ONE STEP IS ONE SOURCE LINE.** Measured at
    // 800x600@60: the field period is 2652636 CPU ticks and the wrong readings
    // sit at multiples of 4224 away, which is that field divided by its 627
    // lines. So a pair one step apart is two different readings of one rate
    // rather than a rate either of them states, and the bound has to fall
    // between one line and the spread two good samples show.
    //
    // There is room for both: 48 samples across four mode changes, at 50 Hz and
    // at 60 Hz, were identical to the milli-hertz, and 97 consecutive
    // measurements of one source spread 0.00015%. One line is 0.159% at 627
    // lines, 0.089% at 1125. This sits three hundred times above the spread and
    // under one line for any source up to about two thousand.
    //
    // A pair that never agrees is not a failure: the caller declines to steer
    // and the next solve measures again, which is the better of the two
    // outcomes.
    static constexpr float RelativeTolerance = 0.0005f;

    // Whether two counts are the same measurement.
    //
    // **THE ONE DEFINITION**, shared with anything comparing a fresh rate
    // against a settled one. A caller using == instead re-arms a mode change on
    // every field of an alternating source, and each one costs a sync-type
    // probe and a re-solve.
    // ../../../../docs/known-issues.md
    static bool agree(float oneHz, float otherHz);
};

}  // namespace Clock

#endif  // CLOCK_RATE_AGREEMENT_H
