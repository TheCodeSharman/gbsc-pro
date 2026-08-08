#ifndef GEOMETRY_AXIS_SOLUTION_H_
#define GEOMETRY_AXIS_SOLUTION_H_

// One axis's four output registers.
#include <stdint.h>

namespace Tv5725 {
// One axis's four output registers, plus where the picture actually landed.
class AxisSolution {
public:
    AxisSolution();

    float produced() const;
    int32_t origin() const;
    int32_t windowStop() const;
    int32_t windowStart() const;
    int32_t displayStop() const;
    int32_t displayStart() const;
    bool usable() const;

private:
    friend class Axis;   // the only thing that may fill one in
    float produced_;
    int32_t origin_, windowStop_, windowStart_, displayStop_, displayStart_;
};

}  // namespace Tv5725

#endif  // GEOMETRY_AXIS_SOLUTION_H_
