#ifndef TV5725_VIDEO_SIGNAL_H_
#define TV5725_VIDEO_SIGNAL_H_

// What counts as a video signal, and the line rate a measurement of one
// implies.
//
// Every class that has to refuse a reading asks here, so the bounds have one
// owner: a count of 97 is what a preset load leaves behind rather than a mode,
// and a field rate outside the band is not something being fed to the part.
// None of it is a classification -- 50 and 60 are not special and a rate is
// whatever the source sends. They are gross-error nets.

#include <stdint.h>

namespace Tv5725 {

class VideoSignal {
public:
    // A 97/98 reading mid-preset-change is a measurement in progress, not a
    // mode: the smallest real ones the VDS scales are 262 and 312.
    static const uint16_t SourceVerticalTotalMin = 200;
    static const uint16_t SourceVerticalTotalMax = 1300;

    // The floor is a gross-error net and has to sit clear of BOTH sides of it.
    // No analog source this board takes sends fields slower than about 47 Hz;
    // what the board reads when it counts a pin the source is not driving
    // measures 15.31 to 21.92 Hz, taken at 640x480@60 on composite sync. A
    // floor of 15 admitted the whole of that band, and the line rate it implies
    // is what sizes the ADC PLL's crossover row.
    static constexpr float FieldRateMinHz = 30.0f;
    static constexpr float FieldRateMaxHz = 150.0f;

    // Whether a line count is something video runs at. The scan mode and the
    // sampling clock are chosen from the count, and both are needed before any
    // field rate has been measured, so this has to answer on its own.
    static bool countIsSource(uint16_t lines);

    static bool fieldRateIsSource(float fieldRateHz);

    // Whether the pair describes a signal at all. The gate, kept apart from the
    // arithmetic below because three of its callers want only this.
    static bool isVideo(uint16_t sourceLines, float fieldRateHz);

    // The field rate times the frame, where the frame is sourceLines + 1:
    // STATUS_SYNC_PROC_VTOTAL is zero based. Reading it as the frame makes this
    // 1/312 low on the bench source, which is the whole of the accuracy
    // HPERIOD_IF has over it.
    //
    // Says nothing about whether the pair was worth converting -- isVideo() is
    // that question.
    static uint32_t lineRateFor(uint16_t sourceLines, float fieldRateHz);

    // Whether two rates are within `perMille` of each other.
    //
    // **THE TOLERANCE IS THE CALLER'S AND THERE IS NO DEFAULT**, because the
    // sites asking this are not asking the same question: whether a reading is
    // trustworthy, whether a source moved, whether two measurements are one
    // source, and whether a source is the standard it claims are four different
    // quantities with four different spreads. One number answering all of them
    // was applied to a fifth -- a divider, which is a derived integer carrying
    // no measurement noise at all -- and forgave a deliberate 3.9% change.
    // ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
    static bool ratesAgree(uint32_t a, uint32_t b, uint16_t perMille);
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_SIGNAL_H_
