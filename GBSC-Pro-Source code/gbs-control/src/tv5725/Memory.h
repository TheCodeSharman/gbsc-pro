#ifndef TV5725_MEMORY_H_
#define TV5725_MEMORY_H_

// The playback stage's burst structure: how much it pulls from SDRAM per
// request, and where it reads relative to where capture is writing.
//
//     PB_FETCH_NUM  = max(FetchFloor, ceil(captureWidth  / RequestsPerLine))
//     PB_CAP_OFFSET = max(FetchFloor, ceil(lineUnits     / RequestsPerLine))
//
// Deriving the fetch from the capture holds the capture/fetch ratio fixed as
// the picture zooms, so no framing can walk into a tearing band. The stride
// takes the WHOLE LINE instead, so it covers the fetch at every framing of it
// and moves only when the divider does.
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

    // The line stride: where playback starts the next line, in the same 64-bit
    // memory words as the fetch. Below the fetch, successive lines overlap and
    // each overwrites its predecessor's tail -- which shows as a green band
    // down the right of the picture.
    //
    // Takes the LINE, not the capture on screen. The fetch grows as the picture
    // zooms out, and moving the stride re-lays the buffer out under a picture
    // being read from it, so it is sized for a capture of the whole line: no
    // framing of that line can outgrow it, and it holds still through a zoom.
    //
    // Not the line less the hsync pulse, which is the widest capture actually
    // reachable: that comes from STATUS_SYNC_PROC_HLOW_LEN, a live measurement
    // that moves by a unit between solves, and a stride that follows it would
    // be rewritten on an arbitrary pad press.
    static uint16_t offsetFor(uint16_t lineUnits);
};

}  // namespace Tv5725

#endif  // TV5725_MEMORY_H_
