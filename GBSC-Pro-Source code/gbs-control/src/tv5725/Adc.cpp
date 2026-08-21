#include "Adc.h"

#include <Arduino.h>

namespace Tv5725 {

void Adc::init()
{
    ADC_CLK_PA::write(0x0);                      // s5_00[1:0]
    ADC_CLK_PLLAD::write(0x0);                   // s5_00[2:2]
    ADC_POWDZ::write(0x1);                       // s5_03[0:0]
    ADC_TR_RSEL::write(0x2);                     // s5_04[1:0]
    ADC_TR_ISEL::write(0x0);                     // s5_04[4:2]
    ADC_TA_EN::write(0x0);                       // s5_05[0:0]
    ADC_TA_CTRL::write(0x1);                     // s5_05[4:1]
    ADC_CKBS::write(0x0);                        // s5_0c[0:0]
    ADC_TEST::write(0x9);                        // s5_0c[4:1]
    ADC_AUTO_OFST_EN::write(0x0);                // s5_0e[0:0]
    ADC_AUTO_OFST_U_RANGE::write(0x0);           // s5_0f[3:0]
    ADC_AUTO_OFST_V_RANGE::write(0x0);           // s5_0f[7:4]
    PLLAD_TEST::write(0x0);                      // s5_11[2:2]
    PLLAD_TS::write(0x0);                        // s5_11[3:3]

    // The ADC PLL's VCO gain and charge pump current, which only bypass ever
    // wrote: without these the PLL ran on whatever loop current the last bypass
    // excursion chose. 6/1 is what a preset load installed, and it stays
    // steppable -- updateCoastPosition() reads ICP >= 5 && FS == 1 before
    // dropping to 5/0. Neither takes effect until PLLAD_LAT sees a rising edge,
    // which resetPLLAD() supplies well after BringUp::init().
    PLLAD_FS::write(0x1);                        // s5_11[5:5]
    PLLAD_BPS::write(0x0);                       // s5_11[6:6]
    PLLAD_ND::write(0x0);                        // s5_14[11:0]
    PLLAD_ICP::write(0x6);                       // s5_17[2:0]
    PA_ADC_LOCKOFF::write(0x0);                  // s5_18[6:6]
    PA_SP_LOCKOFF::write(0x0);                   // s5_19[6:6]
}

void Adc::latch()
{
    PLLAD_LAT::write(0);
    delayMicroseconds(128);
    PLLAD_LAT::write(1);
}

void Adc::applySampleRate(uint16_t divider)
{
    PLLAD_MD::write(divider);
    latch();
}

}  // namespace Tv5725
