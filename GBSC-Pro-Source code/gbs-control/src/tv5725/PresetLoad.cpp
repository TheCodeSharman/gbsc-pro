#include "PresetLoad.h"

namespace Tv5725 {

PresetLoad::PresetLoad(uint8_t videoStandardInput, uint8_t adcInputSel,
                       bool preferScalingRgbhv, bool validForScalingRgbhv)
    : videoStandardInput_(videoStandardInput == NoValidMode ? 0 : videoStandardInput),
      inputIsYpBpR_(adcInputSel == 0),
      enableScalingRgbhv_(preferScalingRgbhv && validForScalingRgbhv)
{
}

uint8_t PresetLoad::videoStandardInput() const
{
    return videoStandardInput_;
}

uint8_t PresetLoad::videoStandardInputAfterLoad() const
{
    return enableScalingRgbhv_ ? ScalingRgbhvStandard : videoStandardInput_;
}

bool PresetLoad::inputIsYpBpR() const
{
    return inputIsYpBpR_;
}

bool PresetLoad::enableScalingRgbhv() const
{
    return enableScalingRgbhv_;
}

} // namespace Tv5725
