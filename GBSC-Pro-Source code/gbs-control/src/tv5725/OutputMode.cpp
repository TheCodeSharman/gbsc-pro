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
    if (presetPreference == Output480P)
        return &Mode480p;
    if (presetPreference == Output576P)
        return &Mode576p;

    return 0;
}

const OutputMode *OutputMode::forFrameHeight(uint16_t frameLines)
{
    if (frameLines == 0)
        return 0;
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

OutputMode::OutputMode(uint16_t activeLines, uint16_t syncPx, uint16_t backPorchPx,
                       uint16_t activePx, uint16_t totalPx, uint32_t standardHz,
                       uint16_t vsyncLines, uint16_t vBackPorchLines,
                       uint16_t vFrontPorchLines)
    : activeLines_(activeLines), syncPx_(syncPx), backPorchPx_(backPorchPx),
      activePx_(activePx), totalPx_(totalPx), standardHz_(standardHz),
      vsyncLines_(vsyncLines), vBackPorchLines_(vBackPorchLines),
      vFrontPorchLines_(vFrontPorchLines) {}

uint16_t OutputMode::scaled(uint16_t standardPx, float clockHz) const
{
    long px = lrintf((float)standardPx * clockHz / (float)standardHz_);
    return px < 0 ? 0 : (uint16_t)px;
}

uint16_t OutputMode::activeLines() const { return activeLines_; }

bool OutputMode::isBypass() const { return this == &ModeBypass; }

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

    // The sync pulse and back porch are the standard's DURATIONS: the encoder
    // measures the pulse and finds active video where our blanking ends, so
    // both have to arrive when the standard says, whatever clock the line runs
    // at.
    long width = scaled(syncPx_, clockHz);
    if (width < 1)
        width = 1;
    long porch = scaled(backPorchPx_, clockHz);

    solved.hsyncStart = 0;
    solved.hsyncStop = (uint16_t)width;
    solved.activeStart = (uint16_t)(width + porch);

    // The active window is the standard's FRACTION of the line, and that is a
    // different quantity from a duration. The encoder resamples the line into
    // the standard's active pixel count, so what it can carry is
    // horizontalTotal x activePx / totalPx and everything painted past that
    // falls off the end of its line. Our raster overruns the standard's by a
    // different factor in every mode -- 1920 against CEA's 2200, 2026 against
    // DMT's 1688 -- so a front porch stated as a time cannot express it.
    // docs/investigations/the-active-window-is-a-fraction-of-the-line.md
    long span = (long)horizontalTotal * activePx_ / totalPx_;
    long lastUsable = (long)horizontalTotal - FrontPorchMinPx;
    long stop = solved.activeStart + span;
    if (stop > lastUsable)
        stop = lastUsable;
    solved.activeStop = stop > (long)solved.activeStart ? (uint16_t)stop
                                                        : solved.activeStart;

    solved.vsyncStart = 0;
    solved.vsyncStop = vsyncLines_;
    solved.activeLinesStart = (uint16_t)(vsyncLines_ + vBackPorchLines_);
    solved.activeLinesStop = (uint16_t)(solved.verticalTotal - vFrontPorchLines_);

    return solved;
}

// Each mode is its STANDARD's own raster, in the standard's pixels at the
// standard's clock -- CEA-861 for 1080p/720p/480p/576p, VESA DMT for
// 1024p/960p. Two quantities come out of it and they are not the same kind:
//
//   the sync pulse and back porch are DURATIONS, converted to whatever clock
//   the line runs at, so the encoder sees them where the standard says;
//
//   the active width is a FRACTION of the line, activePx / totalPx, because the
//   encoder resamples the line into activePx samples however long it is.
//
// The front porch is therefore never stated: it is what the total leaves, and
// FrontPorchMinPx is the floor under it.
//
// Vertical is in lines, which need no conversion and no fraction -- the encoder
// counts real lines -- and is the standard's in full: active, front porch, sync,
// back porch.
//
//   1080p  CEA-861   1080 + 4 + 5 + 36 = 1125
//   720p   CEA-861    720 + 5 + 5 + 20 =  750
//
// RD-5725-1.1 wants total-1 in VDS_VSYNC_RST, so a total written there directly
// runs one line long -- which is what the shipped tables did, all six of them.
//
// Arguments are (activeLines, syncPx, backPorchPx, activePx, totalPx,
// standardHz, vsync, vBackPorch, vFrontPorch). Bypass has no active lines and no
// porches, so frameLines() is 0 and clockDividerFor() finds no divider -- which
// is what makes solve() fail usable() rather than return a plausible zero
// raster, and is why its zero standardHz is never divided by.
const OutputMode ModeBypass(0, 0, 0, 0, 0, 0, 0, 0, 0);

const OutputMode Mode1080p(1080, 44, 148, 1920, 2200, 148500000, 5, 36, 4);  // 1125
const OutputMode Mode1024p(1024, 112, 248, 1280, 1688, 108000000, 3, 38, 1); // 1066
const OutputMode Mode960p(960, 112, 312, 1280, 1800, 108000000, 3, 36, 1);   // 1000
const OutputMode Mode720p(720, 40, 220, 1280, 1650, 74250000, 5, 20, 5);     //  750
const OutputMode Mode576p(576, 64, 68, 720, 864, 27000000, 5, 39, 5);        //  625
const OutputMode Mode480p(480, 62, 60, 720, 858, 27000000, 6, 30, 9);        //  525

}  // namespace Tv5725
