#ifndef TV5725_REGISTER_SOLUTION_H_
#define TV5725_REGISTER_SOLUTION_H_

#include <stdint.h>

#include "Axis.h"

namespace Tv5725 {

// Computes every output register from the capture windows and the raster, both
// axes at once. The caller must clear VDS_?SCALE_BYPS.
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

#endif  // TV5725_REGISTER_SOLUTION_H_
