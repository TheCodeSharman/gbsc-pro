#include "ColourSpace.h"

namespace Tv5725 {

void ColourSpace::applyYuv()
{
    Adc::ADC_RYSEL_R::write(1);
    Adc::ADC_RYSEL_G::write(0);
    Adc::ADC_RYSEL_B::write(1);
    DEC_MATRIX_BYPS::write(1);
    InputFormatter::IF_MATRIX_BYPS::write(1);

    VideoProcessor::VDS_Y_GAIN::write(0x80);
    VideoProcessor::VDS_UCOS_GAIN::write(0x1C);
    VideoProcessor::VDS_VCOS_GAIN::write(0x29);

    // Overwritten later in the same load by the gain the ADC settles on --
    // 0x7B on the bench, against the 0x33 written here. Kept because removing a
    // write whose effect is invisible is a change this move cannot justify.
    Adc::ADC_RGCTRL::write(0x33);
    Adc::ADC_GGCTRL::write(0x33);
    Adc::ADC_BGCTRL::write(0x33);

    VideoProcessor::VDS_Y_OFST::write(0x0E);
    VideoProcessor::VDS_U_OFST::write(0x03);
    VideoProcessor::VDS_V_OFST::write(0x04);
}

void ColourSpace::applyRgb()
{
    Adc::ADC_RYSEL_R::write(0);
    Adc::ADC_RYSEL_G::write(0);
    Adc::ADC_RYSEL_B::write(0);
    DEC_MATRIX_BYPS::write(0);
    InputFormatter::IF_MATRIX_BYPS::write(1);

    VideoProcessor::VDS_Y_GAIN::write(0x80);
    VideoProcessor::VDS_UCOS_GAIN::write(0x1C);
    VideoProcessor::VDS_VCOS_GAIN::write(0x29);
    VideoProcessor::VDS_Y_OFST::write(0x00);
    VideoProcessor::VDS_U_OFST::write(0x00);
    VideoProcessor::VDS_V_OFST::write(0x00);
}

}  // namespace Tv5725
