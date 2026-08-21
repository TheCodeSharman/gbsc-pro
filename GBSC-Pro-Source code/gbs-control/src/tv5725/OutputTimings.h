#ifndef TV5725_OUTPUT_TIMINGS_H_
#define TV5725_OUTPUT_TIMINGS_H_

// One solved output raster: every timing register an OutputMode produces for a
// measured field rate.

#include <stdint.h>

namespace Tv5725 {

// A solved raster: every output timing register, from a mode and a field rate.
class OutputTimings {
public:
    OutputTimings();

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

#endif  // TV5725_OUTPUT_TIMINGS_H_
