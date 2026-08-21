#ifndef TV5725_REGISTER_SOLUTION_H_
#define TV5725_REGISTER_SOLUTION_H_

#include <stdint.h>

#include "Axis.h"

namespace Tv5725 {

// Computes every output register from the capture windows and the raster, both
// axes at once. The caller must clear VDS_?SCALE_BYPS.
class RegisterSolution {
public:
    // activeStopH and activeStopV are RasterSolution's, and 0 means the raster's
    // own edge -- see Axis::farBound.
    RegisterSolution(uint16_t horizontalCapture, uint16_t verticalCapture, uint16_t linePx,
             uint16_t frameLines, uint16_t activeStopH = 0,
             uint16_t activeStopV = 0);

    const AxisSolution &horizontal() const;
    const AxisSolution &vertical() const;
    Scale horizontalScale() const;
    Scale verticalScale() const;

    bool usable() const;

private:
    AxisSolution horizontal_, vertical_;
    Scale horizontalScale_, verticalScale_;
};

}  // namespace Tv5725

#endif  // TV5725_REGISTER_SOLUTION_H_
