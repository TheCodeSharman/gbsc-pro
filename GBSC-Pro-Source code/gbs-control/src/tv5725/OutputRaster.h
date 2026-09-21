#ifndef TV5725_OUTPUT_RASTER_H_
#define TV5725_OUTPUT_RASTER_H_

// The output raster one axis of a capture is solved against: the whole line or
// frame, and one past the last pixel the picture may occupy.
//
// The two travel together because a capture is bounded by both -- it must be
// large enough to fill the raster at the magnification the axis allows, and
// small enough that what it produces still fits inside the porches.
// Zero means there is no solved raster, which is what bypass has.

#include <stdint.h>

namespace Tv5725 {

class OutputRaster {
public:
    OutputRaster(uint16_t total = 0, uint16_t activeStop = 0,
                 uint16_t activeStart = 0);

    uint16_t total() const;

    // OutputTimings::activeStop. Zero asks for the raster's own edge, which is
    // what a bypass or a custom preset gets: no porch is known to reserve.
    uint16_t activeStop() const;

    // Where the window OPENS, which is room the capture cannot use: the
    // picture starts after the mode's sync and back porch.
    uint16_t activeStart() const;

    bool solved() const;

private:
    uint16_t total_, activeStop_, activeStart_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_RASTER_H_
