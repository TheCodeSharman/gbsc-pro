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

#include "RasterSolution.h"

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
    //      81.35 MHz   1445 x 1126   shipped -- works
    //     107.98 MHz   1918 x 1126   works, sharp
    //     129.55 MHz   2301 x 1126   works, sharp
    //     161.98 MHz   2877 x 1126   FLICKERS THEN GOES BLACK
    static const uint32_t WorkingCeilingHz = 129600000;

    // The ceiling 
    static const uint32_t EngineCeilingHz = 108000000;

    static const uint16_t HorizontalTotalMax = 4096;  // VDS_HSYNC_RST is 12 bits
    static const uint16_t VerticalTotalMax = 2048;  // VDS_VSYNC_RST is 11 bits

    // The widest line this clock affords, or 0 if it is not a raster. FLOORED:
    // rounding up would ask for a clock above the divider's rating.
    static uint16_t horizontalTotalFor(uint32_t hz, uint16_t frameLines, float fieldRateHz);

    // The largest seed at or under the ceiling that still yields a valid raster --
    // largest because the seed only has to put the Si5351 in range. Skips seeds
    // whose horizontalTotal overflows the 12-bit register. 0 means "do not write".
    static uint8_t dividerFor(uint16_t frameLines, float fieldRateHz,
                              uint32_t ceilingHz = WorkingCeilingHz);

    uint16_t activeLines() const;

    // active + front + sync + back. What VDS_VSYNC_RST is written from, less one.
    uint16_t frameLines() const;

    // The mode that owns this frame height, or NULL if none does. The height is
    // the one part of a raster that is a CHOICE rather than a calculation.
    //
    // NULL is not a failure and must NOT fall back to 1080p: a height nobody has
    // swept keeps the raster it already had.
    static const OutputMode *forFrameHeight(uint16_t frameLines);

    // The mode the USER asked for, so the engine does not read its own input off
    // the chip.
    //
    // NULL for two reasons the caller must not conflate:
    //   - OutputCustomized: the saved preset's bytes ARE the mode, so reading
    //     the raster back is correct there.
    //   - OutputBypass: no scaled raster to solve.
    //
    // fieldRateHz disambiguates one preference and picks a RESOLUTION, not a
    // timing: Output480P is 480 active lines at 60 Hz and 576 at 50, per CEA-861.
    static const OutputMode *forPreference(PresetPreference presetPreference,
                                           float fieldRateHz);

    // Below this a source is 50 Hz. The same split docs/vesa-gtf.md settled --
    // PAL or NTSC on field rate, no curve -- and far from both 50 and 59.94, so
    // a source measured a little off still lands on the right side.
    static const uint16_t PalNtscSplitHz = 55;

    // Everything, for a measured field rate. Pass the ceiling only to sweep it.
    RasterSolution solve(float fieldRateHz,
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
