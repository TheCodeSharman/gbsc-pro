#ifndef TV5725_CAPTURE_WINDOW_H_
#define TV5725_CAPTURE_WINDOW_H_

// The rectangle grabbed out of the source's, and the thing pan and zoom move.
#include <stdint.h>

#include "VideoSourceLine.h"
#include "ActiveImage.h"
#include "SourceMeasurement.h"
#include "HsyncPulse.h"
#include "SourceTiming.h"
#include "BlankingTiming.h"

namespace Tv5725 {

/* 
    CaptureWindwow calculates the Input Formatter registers needed to capture
    the aperture the PanAndZoom wants.
*/
class CaptureWindow {
public:
    CaptureWindow();

    // IF_LINE_ST. Chosen, not derived -- nothing explains 64.
    static const uint16_t ProgressiveStart = 64;

    // The line the framing is placed on, per axis. False when the source has not
    // settled far enough to derive a window from.
    //
    // The measurement is an ARGUMENT, not a read. PLLAD_LAT is what loads the
    // divider into the ADC PLL, so between a write and the latch the register
    // reports a value the chip is not using.
    //
    // The sync duty and polarity arrive in the READING, taken by the layer that
    // measures. The line count and the field rate come off the measurement,
    // which is what keeps the two cross-checked against each other rather than
    // against a value some caller chose. docs/firmware-geometry-engine.md
    //
    // `timing` is the published raster the MEASUREMENT matched. It is not
    // resolved here: the three values that identify it are all measured, and a
    // path that plays the source out rather than scaling it never reaches this
    // call at all. docs/video-source-acquisition.md
    bool setSource(const SourceMeasurement &source, const HsyncPulse &reading,
                   const SourceTiming &timing, bool lineDoubled);

    void setFraming(const PanAndZoom &wanted);
    const PanAndZoom &framing() const;

    const BlankingTiming &horizontal() const;
    const BlankingTiming &vertical() const;

    // The horizontal line knows what the hsync pulse takes off its head; the
    // vertical does not, because nothing has measured the vsync equivalent and
    // a guess there would crop picture rather than blanking.
    const VideoSourceLine &horizontalLine() const;
    const VideoSourceLine &verticalLine() const;

    // The capturable region this axis offers, which is the denominator the
    // framing's proportions are taken against.
    // The span the framing is a proportion of: the WHOLE line, which is the
    // same part of the source in either scan mode. What the capture path can
    // actually reach inside it is VideoSourceLine's own business.
    uint16_t lineUnitsOn(const Axis &axis) const;

    // The first and last units of the line a capture window may occupy. What
    // lies outside them is the capture path's own exclusion, not the framing's.
    uint16_t firstUnitOn(const Axis &axis) const;
    uint16_t reachOn(const Axis &axis) const;

    bool usable() const;

private:
    VideoSourceLine horizontalLine_, verticalLine_;
    SourceTiming timing_;
    ActiveImage image_;
    BlankingTiming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_CAPTURE_WINDOW_H_
