#ifndef TV5725_OUTPUT_TIMING_H_
#define TV5725_OUTPUT_TIMING_H_

// The OutputMode rendered in VDS units: the standard re-expressed in pixels of
// the clock this board actually runs, at the source's measured field rate. The
// pair to SourceTiming, which is the raster the SOURCE runs.
//
// Two rendering rules, and they are not interchangeable. The sync pulse is the
// standard's DURATION, because the encoder needs it to arrive when the standard
// says whatever clock the line runs at. The active window is the standard's
// FRACTION of the line, because the encoder resamples the line into the
// standard's active pixel count -- and our raster overruns the standard's by a
// different factor in every mode, 1920 against CEA's 2200 and 2026 against
// DMT's 1688, which a duration cannot express. OutputMode::solve() applies
// both.

#include <stdint.h>

namespace Tv5725 {

class OutputTiming {
public:
    OutputTiming();

    uint16_t horizontalTotal, verticalTotal;
    uint8_t divider;
    uint16_t hsyncStart, hsyncStop;
    uint16_t vsyncStart, vsyncStop;

    // First active pixel and line: the end of sync plus the back porch. This is
    // what the geometry engine should bound the picture by, not the raster total.
    uint16_t activeStart, activeLinesStart;

    // One past the last active pixel and line: the total less the front porch. The
    // picture must end here, not at the raster's edge -- a display window taken
    // right up to VDS_HSYNC_RST leaves too little blanking and the colours come
    // out wrong. Vertically 41..1121 for 1080p, which is the encoder window
    // measured on the bench.
    uint16_t activeStop, activeLinesStop;

    float fieldRate;

    // False when anything upstream was unmeasurable. **CHECK THIS BEFORE
    // WRITING.** A raster written from a measurement that did not happen is how
    // the screen goes dark with every register still reading correct.
    bool usable() const;

    // The clock this raster asks for. The Si5351 is steered here, so it is the
    // real display clock rather than the seed's nominal frequency.
    uint32_t demandedHz() const;

    uint16_t activeWidth() const;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_TIMING_H_
