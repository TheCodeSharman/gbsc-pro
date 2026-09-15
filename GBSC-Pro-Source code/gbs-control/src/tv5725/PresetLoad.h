#ifndef TV5725_PRESET_LOAD_H
#define TV5725_PRESET_LOAD_H

#include <stdint.h>

namespace Tv5725 {

// One flag: whether the output in force is scaling RGBHV.
//
// **NOTHING HERE READS AN OUTPUT RESOLUTION OR A FRAMING, WHICH THE NAME
// SUGGESTS.** OutputChoice answers the resolution, and the standard byte's
// vocabulary that was the rest of this class is gone with the byte. The flag
// is engine mode state and belongs to Tv5725::VideoPath.
// docs/video-source-acquisition.md
//
// Plain integers, no registers and no rto->, so it host-compiles.
class PresetLoad {
public:
    // Whether the output in force is scaling RGBHV. State rather than a chip
    // register: it lived in s1_2c, an address RD-5725-1.1 does not document,
    // and every reader had to be ordered against the load that cleared it.
    static bool scalingRgbhvInForce();

    // A scaling RGBHV preset is loaded.
    static void rememberScalingRgbhv();

    // A load is starting, and what the last one enabled says nothing about it.
    static void forgetScalingRgbhv();
};

} // namespace Tv5725

#endif
