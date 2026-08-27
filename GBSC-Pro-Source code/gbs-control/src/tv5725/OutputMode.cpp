#include "OutputMode.h"

#include "DisplayClock.h"

#include <math.h>

namespace Tv5725 {

const uint32_t OutputMode::WorkingCeilingHz;
const uint32_t OutputMode::EngineCeilingHz;
const uint16_t OutputMode::MaxHorizontalTotal;
const uint16_t OutputMode::FrontPorchMinPx;
const uint16_t OutputMode::HorizontalTotalMax;
const uint16_t OutputMode::VerticalTotalMax;

uint16_t OutputMode::horizontalTotalFor(uint32_t hz, uint16_t frameLines,
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

uint8_t OutputMode::clockDividerFor(uint16_t frameLines, float fieldRateHz,
                                 uint32_t ceilingHz)
{
    uint8_t best = 0;
    uint32_t bestHz = 0;

    for (uint8_t i = 0; i < DisplayClock::SeedCount; ++i) {
        uint8_t seed = DisplayClock::Seeds[i];
        uint32_t hz = DisplayClock::hzFor(seed);
        if (hz == 0 || hz > ceilingHz)
            continue;
        uint16_t total = horizontalTotalFor(hz, frameLines, fieldRateHz);
        if (total == 0)
            continue;
        // A line the part cannot finish producing is worse than a narrower one:
        // the excess wraps to the start of the line as repeated picture.
        if (total > MaxHorizontalTotal)
            continue;
        if (hz >= bestHz) {
            bestHz = hz;
            best = seed;
        }
    }
    return best;
}

const uint16_t OutputMode::PalNtscSplitHz;

const OutputMode *OutputMode::forPreference(PresetPreference presetPreference)
{
    if (presetPreference == Output1080P)
        return &Mode1080p;
    if (presetPreference == Output1024P)
        return &Mode1024p;
    if (presetPreference == Output960P)
        return &Mode960p;
    if (presetPreference == Output720P)
        return &Mode720p;

    // 480p and 576p are separate preferences: a preference names a resolution
    // and nothing else, so either is selectable whatever the source runs at.
    // Choosing between them by field rate is matchPresetSource's job, next to
    // the 960 against 1024 swap it already makes.
    if (presetPreference == Output480P)
        return &Mode480p;
    if (presetPreference == Output576P)
        return &Mode576p;

    return 0;
}

const OutputMode *OutputMode::forFrameHeight(uint16_t frameLines)
{
    if (frameLines == Mode1080p.frameLines())
        return &Mode1080p;
    if (frameLines == Mode1024p.frameLines())
        return &Mode1024p;
    if (frameLines == Mode960p.frameLines())
        return &Mode960p;
    if (frameLines == Mode720p.frameLines())
        return &Mode720p;
    if (frameLines == Mode576p.frameLines())
        return &Mode576p;
    if (frameLines == Mode480p.frameLines())
        return &Mode480p;
    return 0;
}

OutputMode::OutputMode(uint16_t activeLines, float syncNs, float backPorchNs,
                       uint16_t vsyncLines, uint16_t vBackPorchLines,
                       uint16_t vFrontPorchLines)
    : activeLines_(activeLines), syncNs_(syncNs), backPorchNs_(backPorchNs),
      vsyncLines_(vsyncLines), vBackPorchLines_(vBackPorchLines),
      vFrontPorchLines_(vFrontPorchLines) {}

uint16_t OutputMode::activeLines() const { return activeLines_; }

uint16_t OutputMode::frameLines() const
{
    return activeLines_ + vFrontPorchLines_ + vsyncLines_ + vBackPorchLines_;
}

OutputTimings OutputMode::solve(float fieldRateHz, uint32_t ceilingHz) const
{
    OutputTimings solved;

    uint8_t divider = OutputMode::clockDividerFor(frameLines(), fieldRateHz,
                                               ceilingHz);
    if (divider == 0)
        return solved;

    uint16_t horizontalTotal = OutputMode::horizontalTotalFor(DisplayClock::hzFor(divider),
                                              frameLines(), fieldRateHz);
    if (horizontalTotal == 0)
        return solved;

    solved.divider = divider;
    solved.horizontalTotal = horizontalTotal;
    solved.verticalTotal = frameLines();
    solved.fieldRate = fieldRateHz;

    // Converted at the clock the line will actually run at, not at the seed's
    // nominal frequency: the seed is a starting point and the raster is the
    // truth. They differ by up to 0.5%, under a pixel here.
    float clockHz = (float)horizontalTotal * (float)frameLines() * fieldRateHz;

    long width = lrintf(syncNs_ * clockHz / 1e9f);
    if (width < 1)
        width = 1;
    long porch = lrintf(backPorchNs_ * clockHz / 1e9f);
    if (porch < 0)
        porch = 0;

    solved.hsyncStart = 0;
    solved.hsyncStop = (uint16_t)width;
    solved.activeStart = (uint16_t)(width + porch);
    solved.activeStop = FrontPorchMinPx < horizontalTotal
                            ? (uint16_t)(horizontalTotal - FrontPorchMinPx)
                            : solved.activeStart;

    solved.vsyncStart = 0;
    solved.vsyncStop = vsyncLines_;
    solved.activeLinesStart = (uint16_t)(vsyncLines_ + vBackPorchLines_);
    solved.activeLinesStop = (uint16_t)(solved.verticalTotal - vFrontPorchLines_);

    return solved;
}

// CEA-861 sync and back porch as TIMES, so they port to a raster that is not the
// standard's:
//
//   1080p  44 px, 148 px at 148.5 MHz   ->  296.30, 996.63 ns
//   720p   40 px, 220 px at 74.25 MHz   ->  538.72, 2962.96 ns
//
// The front porch is not among them. It is the one the standard varies to absorb
// the field rate -- 1080p50 runs 528 px against 1080p60's 88 -- and the encoder
// generates its own HDMI blanking from what it samples, so what the far end of
// the line needs is the board's own floor, FrontPorchMinPx.
//
// Vertical is in lines, which need no conversion, and is the STANDARD's in full
// -- active, front porch, sync, back porch:
//
//   1080p  CEA-861   1080 + 4 + 5 + 36 = 1125
//   720p   CEA-861    720 + 5 + 5 + 20 =  750
//
// RD-5725-1.1 wants total-1 in VDS_VSYNC_RST, so a total written there directly
// runs one line long -- which is what the shipped tables did, all six of them.
//
// 1024p and 960p are VESA DMT rather than CEA-861, converted the same way at
// their own 108 MHz pixel clock:
//
//   1024p  112 px, 248 px         ->  1037.04, 2296.30 ns
//    960p  112 px, 312 px         ->  1037.04, 2888.89 ns
//    576p   64 px,  68 px at 27   ->  2370.37, 2518.52 ns
//    480p   62 px,  60 px at 27   ->  2296.30, 2222.22 ns
//
// Arguments are (activeLines, syncNs, backPorchNs, vsync, vBackPorch, vFrontPorch).
const OutputMode Mode1080p(1080, 296.2963f, 996.6330f, 5, 36, 4);    // 1125
const OutputMode Mode1024p(1024, 1037.0370f, 2296.2963f, 3, 38, 1);  // 1066
const OutputMode Mode960p(960, 1037.0370f, 2888.8889f, 3, 36, 1);    // 1000
const OutputMode Mode720p(720, 538.7205f, 2962.9630f, 5, 20, 5);     //  750
const OutputMode Mode576p(576, 2370.3704f, 2518.5185f, 5, 39, 5);    //  625
const OutputMode Mode480p(480, 2296.2963f, 2222.2222f, 6, 30, 9);    //  525

}  // namespace Tv5725
