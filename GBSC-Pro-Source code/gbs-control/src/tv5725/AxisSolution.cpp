#include "AxisSolution.h"

namespace Tv5725 {

AxisSolution::AxisSolution()
    : produced_(0.0f), origin_(0), windowStop_(0), windowStart_(0),
      displayStop_(0), displayStart_(0) {}

float AxisSolution::produced() const { return produced_; }

int32_t AxisSolution::origin() const { return origin_; }

int32_t AxisSolution::windowStop() const { return windowStop_; }

int32_t AxisSolution::windowStart() const { return windowStart_; }

int32_t AxisSolution::displayStop() const { return displayStop_; }

int32_t AxisSolution::displayStart() const { return displayStart_; }

bool AxisSolution::usable() const { return produced_ > 0.0f; }

}  // namespace Tv5725
