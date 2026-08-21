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
               float frontPorchNs, uint16_t vsyncLines, uint16_t vBackPorchLines,
               uint16_t vFrontPorchLines);

    // Swept on the bench 2026-08-11, RiscPC 320x256@50, judged on the TV:
    //
    //      81.35 MHz   1445 x 1126   works, sharp
    //     107.98 MHz   1918 x 1126   works, sharp
    //     129.55 MHz   2301 x 1126   works, sharp
    //     161.98 MHz   2877 x 1126   FLICKERS THEN GOES BLACK
    static const uint32_t WorkingCeilingHz = 129600000;

    // A usability limit, not an electrical one: a wider raster costs zoom
    // travel. A different question from WorkingCeilingHz, not a safer copy.
    static const uint32_t EngineCeilingHz = 108000000;

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

    // Returns the output mode set in the users preferences, takes the frame rate so that 
    // it can correctly choose between 480p and 576p. Returns NULL for bypass, or 
    // a custom preset.
    static const OutputMode *forPreference(PresetPreference presetPreference,
                                           float fieldRateHz);

    // The threshold that determines when a mode is considered PAL or NTSC.
    static const uint16_t PalNtscSplitHz = 55;

    // Calculates the output timings for the given frame rate. ceilingHz clamps
    // the display clock to usable maximum.
    OutputTimings solve(float fieldRateHz,
                        uint32_t ceilingHz = WorkingCeilingHz) const;

private:
    uint16_t activeLines_;
    float syncNs_, backPorchNs_, frontPorchNs_;
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
