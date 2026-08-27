#ifndef TV5725_OUTPUT_MODE_H_
#define TV5725_OUTPUT_MODE_H_

// One output mode: its frame height, the CEA-861 timings that place the sync
// pulse inside it, and the clock arithmetic that turns both into a raster.
//
// The output raster, computed rather than loaded from a preset table: both
// totals, both sync pulses, the active window and the display clock seed.
//
//     VDS_HSYNC_RST   horizontalTotal - 1   VDS_HS_ST / VDS_HS_SP   hsync pulse
//     VDS_VSYNC_RST   verticalTotal - 1     VDS_VS_ST / VDS_VS_SP   vsync pulse
//     PLL648_CONTROL_01                     the display clock seed
//
// horizontalTotal = clock / (fieldRate x frameLines), and frameLines belongs to
// the output mode alone, so the field rate moves only the total.
//
// See docs/firmware-geometry-engine.md

#include <stdint.h>

#include "OutputTimings.h"

namespace Tv5725 {

// The modes the user can select
enum PresetPreference : uint8_t {
    Output960P = 0,
    Output480P = 1,
    OutputCustomized = 2,
    Output720P = 3,
    Output1024P = 4,
    Output1080P = 5,
    // 6 was OutputDownscale, which went with the preset tables.
    Output576P = 7,
    OutputBypass = 10,
};

// An output mode: its frame height, and the CEA-861 timings that place the sync
// pulse inside it.
//
// Call forFrameHeight() to find the mode that owns a frame height, and forPreference() to
// find the mode the user asked for.
class OutputMode {
public:
    OutputMode(uint16_t activeLines, float syncNs, float backPorchNs,
               uint16_t vsyncLines, uint16_t vBackPorchLines,
               uint16_t vFrontPorchLines);

    // Swept on the bench 2026-08-11, RiscPC 320x256@50, judged on the TV:
    //
    //      81.35 MHz   1445 x 1126   works, sharp
    //     107.98 MHz   1918 x 1126   works, sharp
    //     129.55 MHz   2301 x 1126   works, sharp
    //     161.98 MHz   2877 x 1126   FLICKERS THEN GOES BLACK
    static const uint32_t WorkingCeilingHz = 129600000;

    // What the ENGINE may ask for. Below WorkingCeilingHz on purpose, and the
    // gap is not caution: 129.6 MHz runs clean and sharp, and is still wrong.
    //
    // A clock buys raster width, and raster width COSTS MAGNIFICATION, because
    // the capture is a property of the source and the solve magnifies whatever
    // it captured to fill whatever raster it was given. Above a magnification
    // this part will not state, the playback pipeline has no valid data for the
    // first stretch of each line, and what shows there is zeros decoding to
    // green and then stale memory as a grey comb.
    //
    // Measured on the bench, RiscPC 320x256@50, capture 973 units:
    //
    //     108   MHz   raster 1920   magnification 1.84   clean
    //     129.6 MHz   raster 2304   magnification 2.22   run-up on picture
    //
    // **It cannot be blanked and it cannot be moved.** Blanking it means opening
    // the display window later, and with the picture panned so content reaches
    // the left edge the run-up lies ON that content, so the blanking clips
    // image. Moving it means putting the output hsync pulse later, and because
    // the encoder holds its sampling window fixed relative to that pulse the
    // whole picture slides by the same amount and the right-hand edge, which
    // only just reaches the panel, retreats by exactly what the left gained.
    //
    // Nor is it escapable by framing: at a 2304 px raster even capturing the
    // entire capturable region magnifies past the onset.
    //
    // **Do not raise this to WorkingCeilingHz.** That was tried, for the black
    // bar in docs/investigations/the-encoder-reframes-the-output.md, and it does
    // not move the bar -- the encoder decides that, not the raster.
    // docs/investigations/display-window-opens-early.md has the measurements.
    static const uint32_t EngineCeilingHz = 108000000;

    // The part wraps the line past about 2240 PRODUCED pixels: measured clean at
    // 2225 and banded at 2250, with the window set to exactly what the scale
    // produces so nothing is unfilled. This is the raster bound that follows,
    // taking the smallest active fraction observed across the presets (0.892).
    // A short frame at the engine ceiling asks for far more than this.
    // docs/investigations/720p-edge-corruption.md
    static const uint16_t MaxHorizontalTotal = 2450;

    // What must stay blank at the far end of the line, in pixels. A property of
    // this part rather than of any standard: the encoder generates its own HDMI
    // blanking and never sees ours. Measured on a 1916 px raster by creeping the
    // display window -- good at 1900, wrong at 1910. Measured in pixels at one
    // clock only, so whether it is really a time is untested.
    // docs/scaler-geometry-model.md "The output front porch"
    static const uint16_t FrontPorchMinPx = 16;

    static const uint16_t HorizontalTotalMax = 4096;  // VDS_HSYNC_RST is 12 bits
    static const uint16_t VerticalTotalMax = 2048;  // VDS_VSYNC_RST is 11 bits

    // The widest line this clock affords, or 0 if it is not a raster.
    static uint16_t horizontalTotalFor(uint32_t hz, uint16_t frameLines, float fieldRateHz);

    // Returns the display clock divider for the given lines and frame rate that is
    // less than or equal to ceilingHz. 0 means no valid divider was found.
    static uint8_t clockDividerFor(uint16_t frameLines, float fieldRateHz,
                                   uint32_t ceilingHz = WorkingCeilingHz);

    uint16_t activeLines() const;

    // The number of lines in a frame including porch and sync.
    uint16_t frameLines() const;

    // Returns the best mode for a frame height, or NULL if none.
    static const OutputMode *forFrameHeight(uint16_t frameLines);

    // The mode a preference names. NULL for bypass and for a custom preset,
    // neither of which is a resolution. A preference is one height whatever the
    // source runs at; matchPresetSource swaps between two of them, and that is
    // OutputChoice's, because it needs a measured rate.
    static const OutputMode *forPreference(PresetPreference presetPreference);

    // The threshold that determines when a mode is considered PAL or NTSC.
    static const uint16_t PalNtscSplitHz = 55;

    // Calculates the output timings for the given frame rate. ceilingHz clamps
    // the display clock to usable maximum.
    OutputTimings solve(float fieldRateHz,
                        uint32_t ceilingHz = WorkingCeilingHz) const;

private:
    uint16_t activeLines_;
    float syncNs_, backPorchNs_;
    uint16_t vsyncLines_, vBackPorchLines_, vFrontPorchLines_;
};

// Defined in OutputRaster.cpp, from the STANDARDS rather than from the tables --
// CEA-861 for 1080p/720p/480p/576p, VESA DMT for 1024p/960p. The tables' own
// heights were one line longer than every one of these; see the constructor.
extern const OutputMode Mode1080p;
extern const OutputMode Mode1024p;
extern const OutputMode Mode960p;
extern const OutputMode Mode720p;
extern const OutputMode Mode576p;
extern const OutputMode Mode480p;

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_MODE_H_
