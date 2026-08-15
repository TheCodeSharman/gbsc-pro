#ifndef TV5725_FRAME_BUFFER_H
#define TV5725_FRAME_BUFFER_H

#include <stdint.h>

namespace Tv5725 {

// The SDRAM frame buffer subsystem: where capture, playback and the
// deinterlacer's field store live in memory, and the guards that bound them.
//
// The numbers, and every reason for them, are in MemoryMap: this class is only
// the register traffic, so that MemoryMap stays pure arithmetic.
//
// ORDER WITHIN init() IS LOAD-BEARING: the constants transcribed from the preset
// tables run FIRST, so the derived map wins where the two overlap.
//
// The registers this moves are ones the picture does not depend on, checked one
// at a time against a frozen source.
class FrameBuffer {
public:
    // Write the memory map and arm its guards. Called from the bring-up where
    // a preset table would have been loaded, and BEFORE the engine solves the
    // raster and the windows.
    static void init();
};

}  // namespace Tv5725

#endif
