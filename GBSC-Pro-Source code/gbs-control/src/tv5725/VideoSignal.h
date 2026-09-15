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

    static constexpr float FieldRateMinHz = 15.0f;
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

    // How far a line rate may move while the source line count does not, in
    // parts per thousand. A gross-error net: the bench transient is 15.6% out
    // (57.9 Hz against a real 50.08) and a settled source drifts by tenths of
    // one, so anything between separates them.
    static const uint16_t RateTolerancePerMille = 50;

    // Whether two line rates are the same measurement -- RateTolerancePerMille
    // apart, which is what separates a source that moved from one being read
    // through a settling PLL.
    static bool ratesAgree(uint32_t a, uint32_t b);
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_SIGNAL_H_
