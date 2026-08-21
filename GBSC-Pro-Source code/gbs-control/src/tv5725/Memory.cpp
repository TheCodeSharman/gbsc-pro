#include "Memory.h"

namespace Tv5725 {

const uint16_t Memory::FetchMin;
const uint16_t Memory::FetchMax;
const uint16_t Memory::OffsetMax;
const uint16_t Memory::DefaultFetch;
const uint16_t Memory::Line1080p;
const uint16_t Memory::Fetch1080p;
const uint16_t Memory::Offset1080p;
const uint16_t Memory::RequestsPerLine;
const uint16_t Memory::FetchFloor;

uint16_t Memory::fetchFor(uint16_t outputLinePx, uint16_t captureWidth)
{
    // NO RASTER GATE. Gating the rule on the swept 1445 px line meant a preset
    // load onto a 1435 px raster switched it off, leaving PB_FETCH_NUM at 256
    // against a capture of 1185 that needed 297 -- and the tearing came back.
    // DefaultFetch is not neutral, it is tuned for a raster it is not, and it
    // can be below the floor.
    (void)outputLinePx;

    // ROUNDED UP. A line one pixel short of its source still fails to finish,
    // and it repeats -- the start of the picture reappearing at the right.
    uint32_t needed = ((uint32_t)captureWidth + RequestsPerLine - 1)
                      / RequestsPerLine;

    if (needed < FetchFloor)
        needed = FetchFloor;
    if (needed > FetchMax)
        needed = FetchMax;

    return (uint16_t)needed;
}

uint16_t Memory::offsetFor(uint16_t lineUnits)
{
    return fetchFor(0, lineUnits);
}

}  // namespace Tv5725
