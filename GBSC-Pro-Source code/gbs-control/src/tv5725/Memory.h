#ifndef TV5725_MEMORY_H_
#define TV5725_MEMORY_H_

// The playback stage's burst structure: how much it pulls from SDRAM per
// request, and where it reads relative to where capture is writing.
//
//     PB_FETCH_NUM = max(FetchFloor, ceil(captureWidth / RequestsPerLine))
//
// Deriving the fetch from the capture holds the capture/fetch ratio fixed as
// the picture zooms, so no framing can walk into a tearing band.
// docs/investigations/hscale-tearing-characterisation.md

#include <stdint.h>

namespace Tv5725 {

class Memory {
public:
    // PB_FETCH_NUM and PB_CAP_OFFSET are ten bits each.
    static const uint16_t FetchMin = 128;
    static const uint16_t FetchMax = 512;
    static const uint16_t OffsetMax = 1023;

    // An output raster nobody has swept keeps upstream's value, rather than a
    // pair tuned for a raster it is not.
    static const uint16_t DefaultFetch = 256;

    // The bench's 1080p output, VDS_HSYNC_RST 1444. Fetch1080p is the anchor
    // the rule is checked against, not what gets written: hand-tuned to 250 at
    // capture 1009, where the rule independently gives 253.
    static const uint16_t Line1080p = 1445;
    static const uint16_t Fetch1080p = 250;
    static const uint16_t Offset1080p = 250;

    // Measured: capture/fetch was clean to 4.04 and tore from 4.28, so 4 sits
    // on the safe side by about 1%. The one number to tune if this is wrong.
    static const uint16_t RequestsPerLine = 4;

    // capture/4 keeps falling as the zoom goes in, past anything anyone has
    // run the part at. This stops where the measurement stops.
    static const uint16_t FetchFloor = 150;

    // Takes the capture as well as the raster because the fetch has to cover
    // the source pixels the line needs, and only the framing knows how many.
    static uint16_t fetchFor(uint16_t outputLinePx, uint16_t captureWidth);

    // A measured pair is used whole; otherwise PB_CAP_OFFSET = PB_FETCH_NUM + 4,
    // the firmware's own invariant. Not critical -- clean across 190..256
    // against a fetch of 204.
    static uint16_t offsetFor(uint16_t outputLinePx);
};

}  // namespace Tv5725

#endif  // TV5725_MEMORY_H_
