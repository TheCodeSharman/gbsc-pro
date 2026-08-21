#ifndef TV5725_DISPLAY_WINDOW_H_
#define TV5725_DISPLAY_WINDOW_H_

// VDS_DIS_?B_ST / VDS_DIS_?B_SP on both axes -- the aperture the encoder sees.
// RD-5725-1.1 calls it the "final display" blanking, "used to clean the output
// data in blanking", which is what separates it from MemoryWindow.
#include "BlankingTiming.h"

namespace Tv5725 {

class DisplayWindow {
public:
    DisplayWindow();
    DisplayWindow(const BlankingTiming &horizontal, const BlankingTiming &vertical);

    const BlankingTiming &horizontal() const;
    const BlankingTiming &vertical() const;

private:
    BlankingTiming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_DISPLAY_WINDOW_H_
