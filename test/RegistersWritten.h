#ifndef TEST_REGISTERS_WRITTEN_H_
#define TEST_REGISTERS_WRITTEN_H_

// How many registers the code under test has touched. A stray write lands
// somewhere nothing below reads, so it moves this and nothing else -- reading
// every field but counting none would miss it.
//
// Include after the FakeTwoWire Wire the binary defines.

#include "fake/Wire.h"

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
