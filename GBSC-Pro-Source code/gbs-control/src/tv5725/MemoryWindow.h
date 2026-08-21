#ifndef TV5725_MEMORY_WINDOW_H_
#define TV5725_MEMORY_WINDOW_H_

// VDS_?B_ST / VDS_?B_SP on both axes. RD-5725-1.1 describes this blanking as
// "used to get data from memory", which is what separates it from DisplayWindow.
#include "BlankingTiming.h"

namespace Tv5725 {

class MemoryWindow {
public:
    MemoryWindow();
    MemoryWindow(const BlankingTiming &horizontal, const BlankingTiming &vertical);

    const BlankingTiming &horizontal() const;
    const BlankingTiming &vertical() const;

private:
    BlankingTiming horizontal_, vertical_;
};

}  // namespace Tv5725

#endif  // TV5725_MEMORY_WINDOW_H_
