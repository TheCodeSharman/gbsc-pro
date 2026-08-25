#include "OutputRaster.h"

namespace Tv5725 {

OutputRaster::OutputRaster(uint16_t total, uint16_t activeStop)
    : total_(total), activeStop_(activeStop) {}

uint16_t OutputRaster::total() const { return total_; }

uint16_t OutputRaster::activeStop() const { return activeStop_; }

bool OutputRaster::solved() const { return total_ > 0; }

}  // namespace Tv5725
