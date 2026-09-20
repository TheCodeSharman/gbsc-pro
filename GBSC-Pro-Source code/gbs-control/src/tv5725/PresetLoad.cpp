#include "PresetLoad.h"

namespace Tv5725 {

namespace {

bool scalingRgbhvInForce_ = false;

}  // namespace

bool PresetLoad::scalingRgbhvInForce()
{
    return scalingRgbhvInForce_;
}

void PresetLoad::rememberScalingRgbhv()
{
    scalingRgbhvInForce_ = true;
}

void PresetLoad::forgetScalingRgbhv()
{
    scalingRgbhvInForce_ = false;
}

} // namespace Tv5725
