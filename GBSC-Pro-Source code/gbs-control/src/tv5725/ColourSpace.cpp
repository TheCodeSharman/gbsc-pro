#include "ColourSpace.h"

namespace Tv5725 {

const uint8_t ColourSpace::ComponentGain;
const uint8_t ColourSpace::RgbGain;
const uint8_t ColourSpace::UCosGain;
const uint8_t ColourSpace::VCosGain;
const uint8_t ColourSpace::ComponentOutputCosGain;

void ColourSpace::applyYuv(ColourBalance &balance)
{
    Adc::ADC_RYSEL_R::write(1);
    Adc::ADC_RYSEL_G::write(0);
    Adc::ADC_RYSEL_B::write(1);
    DEC_MATRIX_BYPS::write(1);
    InputFormatter::IF_MATRIX_BYPS::write(1);

    VideoProcessor::VDS_UCOS_GAIN::write(UCosGain);
    VideoProcessor::VDS_VCOS_GAIN::write(VCosGain);

    Adc::ADC_RGCTRL::write(ComponentGain);
    Adc::ADC_GGCTRL::write(ComponentGain);
    Adc::ADC_BGCTRL::write(ComponentGain);

    balance.restFor(ColourBalance::Component);
    balance.apply();
}

void ColourSpace::applyRgb(ColourBalance &balance)
{
    Adc::ADC_RYSEL_R::write(0);
    Adc::ADC_RYSEL_G::write(0);
    Adc::ADC_RYSEL_B::write(0);
    DEC_MATRIX_BYPS::write(0);
    InputFormatter::IF_MATRIX_BYPS::write(1);

    VideoProcessor::VDS_UCOS_GAIN::write(UCosGain);
    VideoProcessor::VDS_VCOS_GAIN::write(VCosGain);

    Adc::ADC_RGCTRL::write(RgbGain);
    Adc::ADC_GGCTRL::write(RgbGain);
    Adc::ADC_BGCTRL::write(RgbGain);

    balance.restFor(ColourBalance::Rgb);
    balance.apply();
}

void ColourSpace::applyComponentOutput(ColourBalance &balance)
{
    VideoProcessor::VDS_UCOS_GAIN::write(ComponentOutputCosGain);
    VideoProcessor::VDS_VCOS_GAIN::write(ComponentOutputCosGain);

    balance.restFor(ColourBalance::ComponentOutput);
    balance.apply();
}

}  // namespace Tv5725
