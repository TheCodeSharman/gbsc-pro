#include "PresetLoad.h"

namespace Tv5725 {

namespace {

bool scalingRgbhvInForce_ = false;
uint16_t scalingRgbhvLines_ = 0;

}  // namespace

bool PresetLoad::scalingRgbhvInForce()
{
    return scalingRgbhvInForce_;
}

void PresetLoad::rememberScalingRgbhv(uint16_t sourceLines)
{
    scalingRgbhvInForce_ = true;
    scalingRgbhvLines_ = sourceLines;
}

void PresetLoad::forgetScalingRgbhv()
{
    scalingRgbhvInForce_ = false;
    scalingRgbhvLines_ = 0;
}

PresetLoad::PresetLoad(uint8_t adcInputSel,
                       bool preferScalingRgbhv, bool validForScalingRgbhv)
    : inputIsYpBpR_(adcInputSel == 0),
      enableScalingRgbhv_(preferScalingRgbhv && validForScalingRgbhv)
{
}

bool PresetLoad::inputIsYpBpR() const
{
    return inputIsYpBpR_;
}

bool PresetLoad::enableScalingRgbhv() const
{
    return enableScalingRgbhv_;
}

uint8_t PresetLoad::rgbhvPresetStandard(uint16_t sourceLines)
{
    const uint16_t loadedLines = scalingRgbhvLines_;
    if (sourceLines < 280 && loadedLines > 280)
        return 1;
    if (sourceLines < 380 && loadedLines > 380)
        return 2;
    if (sourceLines > 380 && loadedLines < 380)
        return 3;
    return 0;
}

uint8_t PresetLoad::rgbhvStandardFor(uint16_t sourceLines, float fieldRateHz)
{
    if (sourceLines < ShortSourceLines)
        return 1;
    if (sourceLines < TallSourceLines)
        return 2;
    if (fieldRateHz > 44.0f && fieldRateHz < 53.8f)
        return 4;
    return 3;
}

} // namespace Tv5725
