#ifndef TV5725_VIDEO_PROCESSOR_TIMINGS_H_
#define TV5725_VIDEO_PROCESSOR_TIMINGS_H_

#include <stdint.h>

#include "Axis.h"
#include "DisplayWindow.h"
#include "MemoryWindow.h"

namespace Tv5725 {

// The two blanking windows the picture is placed by, and the scales that relate
// them. The raster they sit inside is OutputTimings.
class VideoProcessorTimings {
public:
    // The bounds are OutputTimings's. 0 for a stop means the raster's own edge
    // -- see Axis::farBound -- and 0 for a start means the write floor alone.
    VideoProcessorTimings(uint16_t horizontalCapture, uint16_t verticalCapture,
                          uint16_t linePx, uint16_t frameLines,
                          uint16_t activeStopH = 0, uint16_t activeStopV = 0,
                          uint16_t activeStartH = 0, uint16_t activeStartV = 0);

    const MemoryWindow &memory() const;
    const DisplayWindow &display() const;

    Scale horizontalScale() const;
    Scale verticalScale() const;

    // The width the scale produced before rounding to a register. No register
    // holds it, and it is what usable() turns on.
    float producedHorizontal() const;
    float producedVertical() const;

    bool usable() const;

private:
    MemoryWindow memory_;
    DisplayWindow display_;
    Scale horizontalScale_, verticalScale_;
    float producedHorizontal_, producedVertical_;
};

}  // namespace Tv5725

#endif  // TV5725_VIDEO_PROCESSOR_TIMINGS_H_
