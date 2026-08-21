#include "VideoProcessorTimings.h"

namespace Tv5725 {

VideoProcessorTimings::VideoProcessorTimings(uint16_t horizontalCapture, uint16_t verticalCapture,
                                             uint16_t linePx, uint16_t frameLines,
                                             uint16_t activeStopH, uint16_t activeStopV)
{
    RasterFit fitH = AxisHorizontal.fitToRaster(horizontalCapture, linePx, 0, activeStopH);
    RasterFit fitV = AxisVertical.fitToRaster(verticalCapture, frameLines, 0, activeStopV);
    horizontalScale_ = fitH.scale();
    verticalScale_ = fitV.scale();

    AxisSolution horizontal =
        AxisHorizontal.solve(horizontalCapture, horizontalScale_, linePx, 0, activeStopH);
    AxisSolution vertical =
        AxisVertical.solve(verticalCapture, verticalScale_, frameLines, 0, activeStopV);

    memory_ = MemoryWindow(horizontal.memory(), vertical.memory());
    display_ = DisplayWindow(horizontal.display(), vertical.display());
    producedHorizontal_ = horizontal.produced();
    producedVertical_ = vertical.produced();
}

const MemoryWindow &VideoProcessorTimings::memory() const { return memory_; }

const DisplayWindow &VideoProcessorTimings::display() const { return display_; }

Scale VideoProcessorTimings::horizontalScale() const { return horizontalScale_; }

Scale VideoProcessorTimings::verticalScale() const { return verticalScale_; }

float VideoProcessorTimings::producedHorizontal() const { return producedHorizontal_; }

float VideoProcessorTimings::producedVertical() const { return producedVertical_; }

bool VideoProcessorTimings::usable() const
{
    return producedHorizontal_ > 0.0f && producedVertical_ > 0.0f;
}

}  // namespace Tv5725
