#include "RegisterSolution.h"

namespace Tv5725 {

RegisterSolution::RegisterSolution(uint16_t horizontalCapture, uint16_t verticalCapture, uint16_t linePx,
                   uint16_t frameLines, uint16_t activeStopH, uint16_t activeStopV)
{
    RasterFit fitH = AxisHorizontal.fitToRaster(horizontalCapture, linePx, 0, activeStopH);
    RasterFit fitV = AxisVertical.fitToRaster(verticalCapture, frameLines, 0, activeStopV);
    horizontalScale_ = fitH.scale();
    verticalScale_ = fitV.scale();
    horizontal_ = AxisHorizontal.solve(horizontalCapture, horizontalScale_, linePx, 0, activeStopH);
    vertical_ = AxisVertical.solve(verticalCapture, verticalScale_, frameLines, 0, activeStopV);
}

const AxisSolution &RegisterSolution::horizontal() const { return horizontal_; }

const AxisSolution &RegisterSolution::vertical() const { return vertical_; }

Scale RegisterSolution::horizontalScale() const { return horizontalScale_; }

Scale RegisterSolution::verticalScale() const { return verticalScale_; }

bool RegisterSolution::usable() const { return horizontal_.usable() && vertical_.usable(); }

}  // namespace Tv5725
