#ifndef TV5725_CAPTURE_WINDOW_H_
#define TV5725_CAPTURE_WINDOW_H_

// The rectangle grabbed out of the source's, and the thing pan and zoom move.
#include <stdint.h>

#include "InputLine.h"
#include "PanAndZoom.h"
#include "SourceMeasurement.h"
#include "BlankingTiming.h"

namespace Tv5725 {

// The source presents a rectangle -- sync, porches, borders and picture -- and
// this is the rectangle taken from inside it. Both are held here because they
// are one concept: a window is only meaningful against the bounds it sits in,
// and a framing those bounds cannot realise is clamped on the way in, so
// framing() afterwards is what was achieved rather than what was asked.
//
// It holds the output raster too, because how far the window may be cropped
// depends on what the magnification can stretch back to fill.
class CaptureWindow {
public:
    CaptureWindow();

    // A 97/98 reading mid-preset-change is a measurement in progress, not a
    // mode: the smallest real ones the VDS scales are 262 and 312.
    static const uint16_t SourceVerticalTotalMin = 200;
    static const uint16_t SourceVerticalTotalMax = 1300;

    // Above this the source is a 50 Hz standard -- PAL-like ~312 lines, NTSC-like
    // ~262. Sanity-checks a measured field rate, which is transient during a
    // preset load where the line count is not.
    static const uint16_t PalVerticalTotalMin = 290;

    // IF_LINE_ST. Chosen, not derived -- nothing explains 64.
    static const uint16_t ProgressiveStart = 64;

    // Read the output rasters and the bounds the window sits in. False when the
    // source has not settled far enough to derive a window from.
    //
    // The sampling is an ARGUMENT, not a read. PLLAD_LAT is what loads the
    // divider into the ADC PLL, so between a write and the latch the register
    // reports a value the chip is not using.
    //
    // `fieldRateHz` is the raw measurement, not a defaulted one: it is here to
    // CONTRADICT the line count, and a fallback that always looks plausible
    // cannot. docs/firmware-geometry-engine.md.
    bool readRasters(const SourceMeasurement &sampling, float fieldRateHz);

    // In RGBHV bypass the VDS is out of the video path and there is nothing to
    // solve; both rasters read back as nearly zero.
    bool scaling() const;

    void setFraming(const PanAndZoom &wanted, float fieldRateHz);
    const PanAndZoom &framing() const;

    const BlankingTiming &horizontal() const;
    const BlankingTiming &vertical() const;

    // The horizontal line knows what the hsync pulse takes off its head; the
    // vertical does not, because nothing has measured the vsync equivalent and
    // a guess there would crop picture rather than blanking.
    const InputLine &horizontalLine() const;

    uint16_t linePx() const;
    uint16_t frameLines() const;

    bool usable() const;

private:
    InputLine horizontalLine_, verticalLine_;
    uint16_t linePx_;         // output raster total, horizontal
    uint16_t frameLines_;     // output raster total, vertical
    PanAndZoom framing_;
    BlankingTiming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_CAPTURE_WINDOW_H_
