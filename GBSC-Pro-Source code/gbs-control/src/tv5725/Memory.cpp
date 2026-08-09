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

// What gbs-control.ino:4136 asks for where no pair was measured.
static const uint16_t OffsetOverFetch = 4;

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

uint16_t Memory::offsetFor(uint16_t outputLinePx)
{
    // Deliberately NOT tracking the fetch. Measured 2026-08-09, the offset is
    // not critical -- clean anywhere across 190..256 against a fetch of 204 --
    // so it stays the value that was verified rather than gaining a second
    // register that moves on every pad press for no measured reason.
    uint32_t offset = (outputLinePx == Line1080p)
        ? Offset1080p
        : (uint32_t)DefaultFetch + OffsetOverFetch;

    return offset > OffsetMax ? OffsetMax : (uint16_t)offset;
}

}  // namespace Tv5725
