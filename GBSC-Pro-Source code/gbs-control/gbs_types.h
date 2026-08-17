#ifndef GBS_TYPES_H_
#define GBS_TYPES_H_

// The legacy flat view of the register map, in ONE place, so any file that
// touches a register can be a .cpp.
//
// GBS is TRANSITIONAL. Registers are migrating out of Tv5725::Tv5725 into the
// subsystem that owns each block, and this is what keeps the call sites that
// have not followed yet compiling. It gains a base per migrated subsystem and
// is deleted when the last caller names its owner directly -- so the base list
// is a progress bar, and inheritance here buys the one thing nothing else can:
// no call site changes on the day a block moves.

#include "src/tv5725/Tv5725.h"
#include "src/tv5725/VideoProcessor.h"

class GBS : public Tv5725::Tv5725, public Tv5725::VideoProcessor {};

#endif  // GBS_TYPES_H_
