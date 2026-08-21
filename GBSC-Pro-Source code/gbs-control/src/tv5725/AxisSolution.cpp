#include "AxisSolution.h"

namespace Tv5725 {

AxisSolution::AxisSolution() : produced_(0.0f) {}

float AxisSolution::produced() const { return produced_; }

const BlankingTiming &AxisSolution::memory() const { return memory_; }

const BlankingTiming &AxisSolution::display() const { return display_; }

bool AxisSolution::usable() const { return produced_ > 0.0f; }

}  // namespace Tv5725
