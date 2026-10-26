#ifndef TEST_REGISTERS_WRITTEN_H_
#define TEST_REGISTERS_WRITTEN_H_

// How many registers the code under test has touched. A stray write lands
// somewhere nothing below reads, so it moves this and nothing else -- reading
// every field but counting none would miss it.
//
// Include after the FakeTwoWire Wire the binary defines.

#include "fake/Wire.h"

// Where a one-bit field last went from 0 to 1, asked by NAME. A field sharing
// its byte with its neighbours cannot be found by looking for writes to the
// byte, which every neighbour also makes.
template <typename Reg>
static int lastRisingEdgeOf()
{
    const uint8_t mask = static_cast<uint8_t>(1u << Reg::bitOffset);
    bool low = false;
    int at = -1;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        if (Wire.trace[i].segment != Reg::segment
            || Wire.trace[i].reg != Reg::byteOffset)
            continue;
        if (!(Wire.trace[i].value & mask))
            low = true;
        else if (low) {
            at = static_cast<int>(i);
            low = false;
        }
    }
    return at;
}

static unsigned registersWritten()
{
    unsigned written = 0;
    for (uint8_t seg = 0; seg < FakeTwoWire::Segments; ++seg)
        for (int reg = 0; reg < 256; ++reg)
            if (Wire.touched[seg][reg])
                ++written;
    return written;
}

#endif  // TEST_REGISTERS_WRITTEN_H_
