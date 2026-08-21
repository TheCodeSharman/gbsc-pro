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

uint16_t Memory::fetchFor(uint16_t captureWidth)
{
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
    return fetchFor(lineUnits);
}

}  // namespace Tv5725
