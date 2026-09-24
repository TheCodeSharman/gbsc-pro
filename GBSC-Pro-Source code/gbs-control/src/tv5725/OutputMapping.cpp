#include "OutputMapping.h"

namespace Tv5725 {

OutputMapping::OutputMapping() : produced_(0.0f) {}

Scale OutputMapping::scale() const { return scale_; }

float OutputMapping::produced() const { return produced_; }

const BlankingTiming &OutputMapping::memory() const { return memory_; }

const BlankingTiming &OutputMapping::display() const { return display_; }

bool OutputMapping::usable() const { return produced_ > 0.0f; }

}  // namespace Tv5725
