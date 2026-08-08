#ifndef GEOMETRY_CONTROLS_H_
#define GEOMETRY_CONTROLS_H_

// What a user press means, and the only place output pixels become input units.
//
// Separate from Geometry because it is a property of the CONTROLS: the engine
// takes input units and neither knows nor cares what a press is.

#include <stdint.h>

#include "Geometry.h"

class Print;

namespace Tv5725 {

class Controls {
public:
    Controls(Geometry &engine, Print &console);

    bool panH(int16_t pixels);
    bool panV(int16_t pixels);
    bool zoomH(int16_t pixels);
    bool zoomV(int16_t pixels);

    Geometry &engine() const;

private:
    // Output pixels -> input units, at the magnification actually in force: a
    // fixed unit step gets coarser the further you zoom in, and the two axes
    // move differently for the same press whenever their magnifications differ.
    // One input unit is the floor -- finer has to come from the output side, and
    // would be a different control.
    static int16_t unitsFor(int16_t pixels, uint16_t scaleReg);

    // The ADJ line: what the press asked for, what it became, and the registers
    // it landed in. Under GBS_DEBUG, like every other console line.
    void report(const char *control, int16_t pixels, int16_t units) const;

    Geometry &engine_;
    Print &console_;
};

}  // namespace Tv5725

#endif  // GEOMETRY_CONTROLS_H_
