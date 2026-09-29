#ifndef TV5725_CONTROL_STEPS_H_
#define TV5725_CONTROL_STEPS_H_

// What one press asks for, in OUTPUT PIXELS. Controls translates it into the
// input units the engine takes.
//
// A REMOTE TAP IS NOT HERE. It asks for the smallest move the axis has, which
// is a capture granule and so depends on the magnification the solve landed on
// -- Controls::horizontalPanFine() and its three peers take it in granules.

#include <stdint.h>

namespace Tv5725 {

class ControlSteps {
public:
    static const int16_t Pan = 8;    // web pads and the OSD bar
    static const int16_t Zoom = 8;
};

}  // namespace Tv5725

#endif  // TV5725_CONTROL_STEPS_H_
