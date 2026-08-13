#ifndef TV5725_OUTPUT_RASTER_H_
#define TV5725_OUTPUT_RASTER_H_

// The output raster, computed rather than loaded from a preset table: both
// totals, both sync pulses, the active window and the display clock seed.
//
//     VDS_HSYNC_RST   horizontalTotal - 1   VDS_HS_ST / VDS_HS_SP   hsync pulse
//     VDS_VSYNC_RST   verticalTotal - 1     VDS_VS_ST / VDS_VS_SP   vsync pulse
//     PLL648_CONTROL_01                     the display clock seed
//
// horizontalTotal = clock / (fieldRate x frameLines), and frameLines belongs to
// the output mode alone, so the field rate moves only the total. The divider is
// a SEED: externalClockGenSyncInOutRate() steers the Si5351 unclamped, so the
// real pixel clock ends up where the raster demands.
// docs/firmware-geometry-engine.md

#include <stdint.h>

namespace Tv5725 {

class OutputMode;

// A solved raster: every output timing register, from a mode and a field rate.
class RasterSolution {
public:
    RasterSolution();

    uint16_t horizontalTotal, verticalTotal;
    uint8_t divider;
    uint16_t hsyncStart, hsyncStop;
    uint16_t vsyncStart, vsyncStop;

    // First active pixel and line: the end of sync plus the back porch. This is
    // what the geometry engine should bound the picture by, not the raster total.
    uint16_t activeStart, activeLinesStart;

    float fieldRate;

    // False when anything upstream was unmeasurable. **CHECK THIS BEFORE
    // WRITING.** A raster written from a measurement that did not happen is how
    // the screen goes dark with every register still reading correct.
    bool usable() const;

    // The clock this raster asks for. The Si5351 is steered here, so it is the
    // real pixel clock rather than the seed's nominal frequency.
    uint32_t demandedHz() const;

    uint16_t activeWidth() const;
};

class OutputRaster {
public:
    // Swept on the bench 2026-08-11, RiscPC 320x256@50, judged on the TV:
    //
    //      81.35 MHz   1445 x 1126   shipped -- works
    //     107.98 MHz   1918 x 1126   works, sharp
    //     129.55 MHz   2301 x 1126   works, sharp
    //     161.98 MHz   2877 x 1126   FLICKERS THEN GOES BLACK
    //
    // A FLOOR on the true limit, not the limit: nothing between 129.6 and 162 has
    // been tried. NOT DisplayClock::CeilingHz, which rates a pad PAD_CKOUT_ENZ
    // disables.
    static const uint32_t WorkingCeilingHz = 129600000;

    // What the engine asks for, which is a different question from what the part
    // does: a wider raster costs zoom travel, because the zoom floor is
    // raster / maxMagnification while the default capture depends on the INPUT
    // line alone, so the two do not track.
    //
    // A usability choice, not a picture-quality one -- the sweep above clears
    // both 1918 and 2301. Equal to DisplayClock::CeilingHz by COINCIDENCE; that
    // is a pad rating. Do not merge them.
    static const uint32_t EngineCeilingHz = 108000000;

    // Below this a source is 50 Hz. The same split docs/vesa-gtf.md settled --
    // PAL or NTSC on field rate, no curve -- and far from both 50 and 59.94, so
    // a source measured a little off still lands on the right side.
    static const uint16_t PalNtscSplitHz = 55;

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

    // The mode that owns this frame height, or NULL if none does. The height is
    // the one part of a raster that is a CHOICE rather than a calculation.
    //
    // NULL is not a failure and must NOT fall back to 1080p: a height nobody has
    // swept keeps the raster it already had.
    static const OutputMode *modeFor(uint16_t frameLines);
};

// One output mode: its frame height, and the CEA-861 timings that place the sync
// pulse inside it.
//
// The sync comes from the standard, and converts unambiguously because the
// standard is a TIME: 1080p sync and back porch are identical at 50 and 60 Hz --
// only the front porch absorbs the rate -- so 1.293 us of every line is fixed and
// what must be reproduced is the time, at our own clock.
//
// EDID is unreachable, so conformant timing is the safest blind guess. The shipped
// tables are no guide: their sync widths run 52 to 152 with no relation to
// horizontalTotal, and two stop BEFORE they start.
class OutputMode {
public:
    OutputMode(uint16_t frameLines, float syncNs, float backPorchNs,
               uint16_t vsyncLines, uint16_t vBackPorchLines);

    uint16_t frameLines() const;

    // Everything, for a measured field rate. Pass the ceiling only to sweep it.
    RasterSolution solve(float fieldRateHz,
                         uint32_t ceilingHz = OutputRaster::WorkingCeilingHz) const;

private:
    uint16_t frameLines_;
    float syncNs_, backPorchNs_;
    uint16_t vsyncLines_, vBackPorchLines_;
};

// Defined in OutputRaster.cpp. Frame heights are the shipped tables' own, where
// each mode's PAL and NTSC pair agree exactly -- 1126 rather than CEA's 1125, one
// line long, kept because changing it moves the vertical geometry and the
// horizontal is the axis under test.
extern const OutputMode Mode1080p;
extern const OutputMode Mode720p;

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_RASTER_H_
