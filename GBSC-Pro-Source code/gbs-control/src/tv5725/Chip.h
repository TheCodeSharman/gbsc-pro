#ifndef TV5725_CHIP_H
#define TV5725_CHIP_H

namespace Tv5725 {

// The chip itself: block resets, analog pads, DAC routing and the output clock
// enables. Segment 0's housekeeping -- the parts that belong to no one video
// block, and so to none of the other subsystem classes.
//
// The six SFTRST_*_RSTZ fields are why this runs first. Written 1 they RELEASE
// the deinterlacer, memory, memory FIFO, general FIFO, OSD and interrupt blocks
// -- the opposite of what the names suggest -- and a block released after it is
// configured discards that configuration. test_bringup.cpp asserts the ordering,
// since nothing inside one class can see a constraint between two.
class Chip {
public:
    // Every static segment-0 register, in address order. Called from the
    // bring-up before any other subsystem, and before the engine solves the
    // raster and the windows.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_CHIP_H
