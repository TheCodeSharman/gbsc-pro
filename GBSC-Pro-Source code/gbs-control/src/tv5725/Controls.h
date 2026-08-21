#ifndef TV5725_CONTROLS_H_
#define TV5725_CONTROLS_H_

// What a user press means, and the only place output pixels become input units.
//
// Separate from Geometry because it is a property of the CONTROLS: the engine
// takes input units and neither knows nor cares what a press is.

#include <stdint.h>

#include "Axis.h"
#include "Geometry.h"

class Print;

namespace Tv5725 {

class Controls {
public:
    Controls(Geometry &engine, Print &console);

    bool horizontalPan(int16_t pixels);
    bool verticalPan(int16_t pixels);
    bool horizontalZoom(int16_t pixels);
    bool verticalZoom(int16_t pixels);

    Geometry &engine() const;

private:
    // Output pixels -> input units, at the magnification actually in force: a
    // fixed unit step gets coarser the further you zoom in, and the two axes
    // move differently for the same press whenever their magnifications differ.
    // The axis decides the floor, because it owns what its hardware can act on.
    static int16_t unitsFor(int16_t pixels, uint16_t scaleReg, const Axis &axis);

    // The ADJ line: what the press asked for, what it became, and the registers
    // it landed in. Under GBS_DEBUG, like every other console line.
    void report(const char *control, int16_t pixels, int16_t units) const;

    Geometry &engine_;
    Print &console_;
};

}  // namespace Tv5725

#endif  // TV5725_CONTROLS_H_
