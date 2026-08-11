#include "DisplayClock.h"

namespace Tv5725 {

const uint32_t DisplayClock::CeilingHz;
const uint8_t DisplayClock::ExternalSentinel;
const uint32_t DisplayClock::FallbackHz;

uint32_t DisplayClock::hzFor(uint8_t divider)
{
    // Lifted verbatim from the if-chain in externalClockGenResetClock(), which
    // is the only place it existed. 0x35 and 0x00 are bytes the firmware writes
    // itself -- setOutModeHdBypass() and the reset path -- and upstream maps
    // both to 81 MHz deliberately, so they are entries rather than fallbacks.
    switch (divider) {
    case 0x25: return 40500000;
    case 0x45: return 54000000;
    case 0x55: return 64800000;
    case 0x65: return 81000000;
    case 0x85: return 108000000;
    case 0x95: return 129600000;
    case 0xA5: return 162000000;
    case 0x35: return 81000000;
    case 0x00: return 81000000;
    default:   return 0;
    }
}

uint16_t DisplayClock::horizontalTotalFor(uint32_t hz, uint16_t frameLines,
                                 float fieldRateHz)
{
    if (hz == 0 || frameLines == 0 || fieldRateHz <= 0.0f)
        return 0;

    // Clocks available per frame, then per line. Done in float because the
    // field rate is measured rather than nominal -- 50.02 Hz is a normal
    // reading and truncating it to 50 would move the answer by a pixel.
    float perFrame = (float)hz / fieldRateHz;
    float perLine = perFrame / (float)frameLines;

    if (perLine < 1.0f)
        return 0;

    // Truncation IS the floor here, and it is what the budget asks for.
    uint32_t horizontalTotal = (uint32_t)perLine;

    // VDS_HSYNC_RST is twelve bits, so the raster total cannot exceed 4096.
    return horizontalTotal > 4096 ? 4096 : (uint16_t)horizontalTotal;
}

}  // namespace Tv5725
