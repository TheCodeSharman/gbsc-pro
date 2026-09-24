#ifndef TV5725_AXIS_SOLUTION_H_
#define TV5725_AXIS_SOLUTION_H_

// RESPONSIBILITY: carry one axis's answer out of OutputWindow -- where the
// picture landed, and the two blanking windows bounding it. A value, filled by
// the one class entitled to compute it.
#include <stdint.h>

#include "BlankingTiming.h"

namespace Tv5725 {

class AxisSolution {
public:
    AxisSolution();

    float produced() const;

    // VDS_?B_ST / VDS_?B_SP -- the window the playback stage fetches through.
    const BlankingTiming &memory() const;

    // VDS_DIS_?B_ST / VDS_DIS_?B_SP -- the aperture that reaches the encoder.
    const BlankingTiming &display() const;

    bool usable() const;

private:
    friend class OutputWindow;   // the only thing that may fill one in
    float produced_;
    BlankingTiming memory_, display_;
};

}  // namespace Tv5725

#endif  // TV5725_AXIS_SOLUTION_H_
