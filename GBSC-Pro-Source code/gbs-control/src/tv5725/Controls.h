#ifndef TV5725_CONTROLS_H_
#define TV5725_CONTROLS_H_

// A user press, routed to the engine and logged. The engine takes output
// pixels and sizes them from the scale it solved.

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
    // The ADJ line: what the press asked for and the registers it landed in.
    // Under GBS_DEBUG, like every other console line.
    void report(const char *control, int16_t pixels) const;

    Geometry &engine_;
    Print &console_;
};

}  // namespace Tv5725

#endif  // TV5725_CONTROLS_H_
