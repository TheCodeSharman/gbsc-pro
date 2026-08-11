#ifndef TV5725_DISPLAY_CLOCK_H_
#define TV5725_DISPLAY_CLOCK_H_

// The display clock, and the output raster it has to pay for.
//
//     required clock = rasterHorizontalTotal x frameLines x sourceFieldRate
//
// so CeilingHz is a CONSTRAINT ON WHICH RASTERS ARE LEGAL, not an input. Neither
// FrameSync::findBestHTotal() nor externalClockGenSyncInOutRate() owns the
// raster: the first returns the total unchanged once the periods agree, and the
// second steers the Si5351 by an unclamped ratio, so the divider byte is only a
// seed that puts it in range.
//
// Every mapped clock is 648 MHz over an integer -- /16, /12, /10, /8, /6, /5, /4
// -- which is where the register's name comes from. It stays a table because the
// byte is not the divider: the high nibble runs 2, 4, 5, 6, 8, 9, A against
// divisors 16, 12, 10, 8, 6, 5, 4.

#include <stdint.h>

namespace Tv5725 {

class DisplayClock {
public:
    // The highest display clock Tvia documents as tested: its own current
    // measurement reads "162MHz 32bit memory, 108MHz Display clock", and
    // DS-5725-3.2 Table 15 rates CLKOUT at 108 MHz / 20pF.
    //
    // **A WORKING CEILING, NOT A PROVEN HARD LIMIT.** CLKOUT is disabled on this
    // board -- PAD_CKOUT_ENZ is 1 in every scaling preset, because the MS9288A
    // takes the analog output -- so Table 15 rates a pad nobody loads, and what
    // bounds the internal VCLK is stated nowhere. The register offers 129.6 and
    // 162 MHz above this.
    //
    // Table 14's 80 MHz CLKIN is the digital video INPUT port, unused here, and
    // the 162 MHz FBCLK rating is the memory interface. Neither is this.
    //
    // CEA-861 1080p wants 148.5 MHz, which nothing in the mapped range hits
    // except 162, so the MS9288A is always resampling. That is the architecture.
    static const uint32_t CeilingHz = 108000000;

    // PLL648_CONTROL_01 is parked here while the Si5351 drives the display, and
    // the real divider is stashed in rto->presetDisplayClock. See options.h.
    static const uint8_t ExternalSentinel = 0x75;

    // What the firmware falls back to when no byte names a clock. A GUESS, kept
    // because it is what shipped: a lost divider costs 25% of the line here
    // instead of announcing itself.
    static const uint32_t FallbackHz = 81000000;

    // This divider byte's pixel clock, or 0 when the byte does not name one.
    // 0 rather than a default: the sentinel is a byte the firmware writes itself,
    // and answering FallbackHz for it is how externalClockGenResetClock() lost
    // the divider and ran the display 25% slow.
    static uint32_t hzFor(uint8_t divider);

    // The widest horizontal total this clock affords, given the frame height it
    // must fill and the field rate it must match. 0 if any input is 0.
    //
    // FLOORED: a raster needs horizontalTotal x frameLines x fieldRate hertz, so
    // rounding up asks for a clock the part is not rated for.
    //
    // `frameLines` is the raster TOTAL, not the register: VDS_VSYNC_RST holds one
    // less.
    static uint16_t horizontalTotalFor(uint32_t hz, uint16_t frameLines,
                              float fieldRateHz);
};

}  // namespace Tv5725

#endif  // TV5725_DISPLAY_CLOCK_H_
