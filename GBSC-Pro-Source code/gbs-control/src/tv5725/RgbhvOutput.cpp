#include "RgbhvOutput.h"

namespace Tv5725 {

bool RgbhvOutput::scaling_ = false;

bool RgbhvOutput::isScaling() { return scaling_; }

void RgbhvOutput::chooseScaling() { scaling_ = true; }

void RgbhvOutput::chooseBypass() { scaling_ = false; }

}  // namespace Tv5725
