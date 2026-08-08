#ifndef GEOMETRY_CONTROL_STEPS_H_
#define GEOMETRY_CONTROL_STEPS_H_

// What one press asks for, in OUTPUT PIXELS. Controls translates it into the
// input units the engine takes.

#include <stdint.h>

namespace Tv5725 {

class ControlSteps {
public:
    static const int16_t Pan = 8;    // web pads and the OSD bar
    static const int16_t Zoom = 8;
    static const int16_t Fine = 1;   // one OSD tap, before the hold ramp
};

}  // namespace Tv5725

#endif  // GEOMETRY_CONTROL_STEPS_H_
