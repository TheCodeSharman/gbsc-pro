#ifndef TV5725_CONTROLS_H_
#define TV5725_CONTROLS_H_

// A user press, routed to the engine and logged. The engine takes output
// pixels and sizes them from the scale it solved.

#include <stdint.h>

#include "Axis.h"
#include "VideoPath.h"

class Print;

namespace Tv5725 {

class Controls {
public:
    Controls(VideoPath &engine, Print &console);

    // True where the press moved the picture, so a caller drawing a bar can say
    // the control has reached its limit without reading a register back.
    bool horizontalPan(int16_t pixels);
    bool verticalPan(int16_t pixels);
    bool horizontalZoom(int16_t pixels);
    bool verticalZoom(int16_t pixels);

    // `steps` of the SMALLEST move the axis has, rather than of output pixels:
    // what a remote tap asks for, and what its hold ramp multiplies. A number
    // of pixels cannot express it -- one capture granule is
    // granularity x magnification pixels, which the solve decides -- and asking
    // for one pixel rounds to nothing above x2, where the press reports a limit
    // that is not there.
    bool horizontalPanFine(int16_t steps);
    bool verticalPanFine(int16_t steps);
    bool horizontalZoomFine(int16_t steps);
    bool verticalZoomFine(int16_t steps);

    VideoPath &engine() const;

private:
    // One granule of `axis`, in the output pixels a press is stated in, times
    // `steps`.
    int16_t finePixels(const Axis &axis, int16_t steps) const;

    // The ADJ line: what the press asked for and the registers it landed in.
    // Under GBS_DEBUG, like every other console line.
    void report(const char *control, int16_t pixels) const;

    VideoPath &engine_;
    Print &console_;
};

}  // namespace Tv5725

#endif  // TV5725_CONTROLS_H_
