#ifndef TV5725_CAPTURE_WINDOW_H_
#define TV5725_CAPTURE_WINDOW_H_

// A capture window: IF units horizontally, HALF-LINES vertically.
#include <stdint.h>

namespace Tv5725 {
// A capture window, in IF units horizontally and HALF-LINES vertically.
class CaptureWindow {
public:
    CaptureWindow();
    CaptureWindow(uint16_t sp, uint16_t st);

    uint16_t sp() const;
    uint16_t st() const;
    uint16_t width() const;
    bool usable() const;

private:
    uint16_t sp_, st_;
};

}  // namespace Tv5725

#endif  // TV5725_CAPTURE_WINDOW_H_
