#ifndef TV5725_BRING_UP_H
#define TV5725_BRING_UP_H

namespace Tv5725 {

// The chip's static bring-up: every subsystem's init(), in the order the
// hardware requires.
//
// This replaces the preset concept rather than being another preset: of the 432
// registers one writeProgramArrayNew() call writes, 306 were identical in all
// twelve scaling tables. docs/chip-initialisation.md.
//
// ORDER IS THE CONTRACT, and it is address order -- what writeProgramArrayNew()
// used, and what the resets require. Chip is first because it holds the six
// SFTRST_*_RSTZ fields that release the deinterlacer, memory, memory FIFO,
// general FIFO, OSD and interrupt blocks, and a reset released after its block
// is configured discards the configuration. test_bringup.cpp asserts it, since
// nothing inside one class can see a constraint between two.
//
// Deliberately not here:
//
// - Anything already written on the SCALING PATH. Not the same as anything the
//   firmware writes somewhere: a field written only by setResetParameters() or
//   one of the two bypass switches has no owner on the path that needs it.
//
// - Anything undocumented, including addresses past the end of the register set
//   and holes inside it. "Every table writes 0x00" is not proof the reset
//   default is 0.
//
// - The output raster, the windows, the scales, the clock. Geometry computes
//   those.
class BringUp {
public:
    // Call where a preset table would have been loaded, BEFORE the engine
    // solves the raster and the windows.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_BRING_UP_H
