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

uint8_t PresetLoad::rgbhvPresetStandard(uint16_t sourceLines, uint16_t loadedLines)
{
    if (sourceLines < 280 && loadedLines > 280)
        return 1;
    if (sourceLines < 380 && loadedLines > 380)
        return 2;
    if (sourceLines > 380 && loadedLines < 380)
        return 3;
    return 0;
}

} // namespace Tv5725
