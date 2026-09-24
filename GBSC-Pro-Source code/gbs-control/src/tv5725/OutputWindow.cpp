#include "OutputWindow.h"

namespace Tv5725 {

OutputWindow::OutputWindow() {}

OutputWindow::OutputWindow(uint16_t horizontalCapture, uint16_t verticalCapture,
                         uint16_t linePx, uint16_t frameLines,
                         uint16_t activeStopH, uint16_t activeStopV,
                         uint16_t activeStartH, uint16_t activeStartV)
{
    RasterFit fitH = AxisHorizontal.fitToRaster(horizontalCapture, linePx, activeStartH, activeStopH);
    RasterFit fitV = AxisVertical.fitToRaster(verticalCapture, frameLines, activeStartV, activeStopV);
    horizontalScale_ = fitH.scale();
    verticalScale_ = fitV.scale();

    horizontal_ =
        AxisHorizontal.solve(horizontalCapture, horizontalScale_, linePx, activeStartH, activeStopH);
    vertical_ =
        AxisVertical.solve(verticalCapture, verticalScale_, frameLines, activeStartV, activeStopV);
}

const AxisSolution &OutputWindow::on(const Axis &axis) const
{
    return axis.vertical() ? vertical_ : horizontal_;
}

Scale OutputWindow::scaleOn(const Axis &axis) const
{
    return axis.vertical() ? verticalScale_ : horizontalScale_;
}

bool OutputWindow::usable() const
{
    return horizontal_.usable() && vertical_.usable();
}

}  // namespace Tv5725
