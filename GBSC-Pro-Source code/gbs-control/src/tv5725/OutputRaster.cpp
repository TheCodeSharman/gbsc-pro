#include "OutputRaster.h"

#include <math.h>

#include "DisplayClock.h"

namespace Tv5725 {

// doctest's CHECK binds each operand to a `const &`, which is an ODR-use.
// Same reason as Memory.cpp.
const uint32_t OutputRaster::WorkingCeilingHz;
const uint16_t OutputRaster::HorizontalTotalMax;
const uint16_t OutputRaster::VerticalTotalMax;

RasterSolution::RasterSolution()
    : horizontalTotal(0), verticalTotal(0), divider(0), hsyncStart(0), hsyncStop(0),
      vsyncStart(0), vsyncStop(0), activeStart(0), activeLinesStart(0),
      fieldRate(0.0f) {}

bool RasterSolution::usable() const
{
    return horizontalTotal != 0 && verticalTotal != 0 && divider != 0 && fieldRate > 0.0f;
}

uint32_t RasterSolution::demandedHz() const
{
    if (!usable())
        return 0;
    return (uint32_t)((float)horizontalTotal * (float)verticalTotal * fieldRate);
}

uint16_t RasterSolution::activeWidth() const
{
    return activeStart >= horizontalTotal ? 0 : (uint16_t)(horizontalTotal - activeStart);
}

uint16_t OutputRaster::horizontalTotalFor(uint32_t hz, uint16_t frameLines,
                                 float fieldRateHz)
{
    if (hz == 0 || frameLines == 0 || fieldRateHz <= 0.0f)
        return 0;

    // Clocks per frame, then per line. Float because the field rate is MEASURED
    // rather than nominal -- 50.02 Hz is a normal reading, and truncating it to 50
    // moves the answer by a pixel.
    float perLine = ((float)hz / fieldRateHz) / (float)frameLines;
    if (perLine < 1.0f)
        return 0;

    // Truncation IS the floor, and the floor is what the budget asks for.
    uint32_t horizontalTotal = (uint32_t)perLine;

    // Refused rather than wrapped: VDS_HSYNC_RST is twelve bits and a wrapped
    // value rolls the picture instead of failing.
    if (horizontalTotal > HorizontalTotalMax)
        return 0;
    return (uint16_t)horizontalTotal;
}

uint8_t OutputRaster::dividerFor(uint16_t frameLines, float fieldRateHz,
                                 uint32_t ceilingHz)
{
    uint8_t best = 0;
    uint32_t bestHz = 0;

    for (uint8_t i = 0; i < DisplayClock::SeedCount; ++i) {
        uint8_t seed = DisplayClock::Seeds[i];
        uint32_t hz = DisplayClock::hzFor(seed);
        if (hz == 0 || hz > ceilingHz)
            continue;
        if (horizontalTotalFor(hz, frameLines, fieldRateHz) == 0)
            continue;
        if (hz >= bestHz) {
            bestHz = hz;
            best = seed;
        }
    }
    return best;
}

OutputMode::OutputMode(uint16_t frameLines, float syncNs, float backPorchNs,
                       uint16_t vsyncLines, uint16_t vBackPorchLines)
    : frameLines_(frameLines), syncNs_(syncNs), backPorchNs_(backPorchNs),
      vsyncLines_(vsyncLines), vBackPorchLines_(vBackPorchLines) {}

uint16_t OutputMode::frameLines() const { return frameLines_; }

RasterSolution OutputMode::solve(float fieldRateHz, uint32_t ceilingHz) const
{
    RasterSolution solved;

    uint8_t divider = OutputRaster::dividerFor(frameLines_, fieldRateHz,
                                               ceilingHz);
    if (divider == 0)
        return solved;

    uint16_t horizontalTotal = OutputRaster::horizontalTotalFor(DisplayClock::hzFor(divider),
                                              frameLines_, fieldRateHz);
    if (horizontalTotal == 0)
        return solved;

    solved.divider = divider;
    solved.horizontalTotal = horizontalTotal;
    solved.verticalTotal = frameLines_;
    solved.fieldRate = fieldRateHz;

    // Converted at the clock the line will actually run at, not at the seed's
    // nominal frequency: the seed is a starting point and the raster is the
    // truth. They differ by up to 0.5%, under a pixel here.
    float clockHz = (float)horizontalTotal * (float)frameLines_ * fieldRateHz;

    long width = lrintf(syncNs_ * clockHz / 1e9f);
    if (width < 1)
        width = 1;
    long porch = lrintf(backPorchNs_ * clockHz / 1e9f);
    if (porch < 0)
        porch = 0;

    // The pulse starts at 0 and the front porch is whatever is left at the end of
    // the line, which is how CEA absorbs the field-rate difference: 1080p50 and
    // 1080p60 have the same sync and back porch and differ only in front porch.
    solved.hsyncStart = 0;
    solved.hsyncStop = (uint16_t)width;
    solved.activeStart = (uint16_t)(width + porch);

    solved.vsyncStart = 0;
    solved.vsyncStop = vsyncLines_;
    solved.activeLinesStart = (uint16_t)(vsyncLines_ + vBackPorchLines_);

    return solved;
}

// CEA-861 sync and back porch as TIMES, so they port to a raster that is not the
// standard's:
//
//   1080p  44 px and 148 px at 148.5 MHz  ->  296.30 ns, 996.63 ns
//   720p   40 px and 220 px at 74.25 MHz  ->  538.72 ns, 2962.96 ns
//
// Vertical is in lines, which need no conversion. 1080p is 5 and 36; the shipped
// tables already ship a 5-line vsync, so the vertical was standard already.
//
// Frame heights are the tables' own, where every mode's PAL and NTSC pair agree.
const OutputMode Mode1080p(1126, 296.2963f, 996.6330f, 5, 36);
const OutputMode Mode720p(751, 538.7205f, 2962.9630f, 5, 20);

}  // namespace Tv5725
