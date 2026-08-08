#ifndef GEOMETRY_SOLUTION_H_
#define GEOMETRY_SOLUTION_H_

// Both axes at once: every output register from the capture windows and the
// raster alone.

#include <stdint.h>

#include "Axis.h"

namespace Tv5725 {

// Every output register from the capture windows and the raster alone. Nothing
// reads the windows or the scale it is about to replace -- compute the
// geometry, never inherit it. The caller must clear VDS_?SCALE_BYPS.
class RegisterSolution {
public:
    RegisterSolution(uint16_t captureH, uint16_t captureV, uint16_t linePx,
             uint16_t frameLines);

    const AxisSolution &h() const;
    const AxisSolution &v() const;
    Scale horizontalScale() const;
    Scale verticalScale() const;

    bool usable() const;

private:
    AxisSolution h_, v_;
    Scale horizontalScale_, verticalScale_;
};

}  // namespace Tv5725

#endif  // GEOMETRY_SOLUTION_H_
