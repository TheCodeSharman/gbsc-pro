#ifndef TV5725_OUTPUT_IMAGE_H_
#define TV5725_OUTPUT_IMAGE_H_

// Where the captured picture lands inside the output raster, per axis: the two
// scales and the two blanking windows each axis is played out through. The
// raster it sits inside is OutputTimings.
//
// RD-5725-1.1 separates the two windows and the names follow it. The MEMORY
// pair, VDS_?B_ST/SP, is "used to get data from memory" -- the window the
// playback stage fetches through. The DISPLAY pair, VDS_DIS_?B_ST/SP, is the
// "final display" blanking, "used to clean the output data in blanking", which
// is the aperture the encoder sees.

#include <stdint.h>

#include "Axis.h"

namespace Tv5725 {

class OutputImage {
public:
    // Nothing solved: every axis reads unusable, which is what a path with no
    // geometry has.
    OutputImage();

    // The bounds are OutputTimings's. 0 for a stop means the raster's own edge
    // -- see Axis::farBound -- and 0 for a start means the write floor alone.
    OutputImage(uint16_t horizontalCapture, uint16_t verticalCapture,
                uint16_t linePx, uint16_t frameLines,
                uint16_t activeStopH = 0, uint16_t activeStopV = 0,
                uint16_t activeStartH = 0, uint16_t activeStartV = 0);

    // One axis: its two windows and what the scale produced.
    const AxisSolution &on(const Axis &axis) const;

    Scale scaleOn(const Axis &axis) const;

    bool usable() const;

private:
    AxisSolution horizontal_, vertical_;
    Scale horizontalScale_, verticalScale_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_IMAGE_H_
