#include "RegisterSolution.h"

namespace Tv5725 {

RegisterSolution::RegisterSolution(uint16_t captureH, uint16_t captureV, uint16_t linePx,
                   uint16_t frameLines, uint16_t activeStopH, uint16_t activeStopV)
{
    RasterFit fitH = AxisHorizontal.fitToRaster(captureH, linePx, 0, activeStopH);
    RasterFit fitV = AxisVertical.fitToRaster(captureV, frameLines, 0, activeStopV);
    horizontalScale_ = fitH.scale();
    verticalScale_ = fitV.scale();
    h_ = AxisHorizontal.solve(captureH, horizontalScale_, linePx, 0, activeStopH);
    v_ = AxisVertical.solve(captureV, verticalScale_, frameLines, 0, activeStopV);
}

const AxisSolution &RegisterSolution::h() const { return h_; }

const AxisSolution &RegisterSolution::v() const { return v_; }

Scale RegisterSolution::horizontalScale() const { return horizontalScale_; }

Scale RegisterSolution::verticalScale() const { return verticalScale_; }

bool RegisterSolution::usable() const { return h_.usable() && v_.usable(); }

}  // namespace Tv5725
