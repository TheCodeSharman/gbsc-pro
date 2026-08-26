#include "Adc.h"

#include <Arduino.h>

namespace Tv5725 {

void Adc::selectInput(uint8_t inputSel)
{
    ADC_INPUT_SEL::write(inputSel);
}

void Adc::enableSyncOnGreen(uint8_t enable)
{
    ADC_SOGEN::write(enable);
}

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
    ADC_AUTO_OFST_PRD::write(0x1);               // s5_0e[1:1]
    ADC_AUTO_OFST_DELAY::write(0x0);             // s5_0e[3:2]
    ADC_AUTO_OFST_STEP::write(0x0);              // s5_0e[5:4]
    ADC_AUTO_OFST_TEST::write(0x1);              // s5_0e[7:7]
    ADC_AUTO_OFST_RANGE_REG::write(0x0);         // s5_0f[7:0]
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

uint8_t Adc::postDividerFor(uint32_t ckoHz)
{
    if (ckoHz >= 80000000u)
        return 0;
    if (ckoHz >= 40000000u)
        return 1;
    if (ckoHz >= 20000000u)
        return 2;
    return 3;
}

uint8_t Adc::oversampleFor(uint8_t postDivider, uint8_t wanted)
{
    uint8_t ratio = wanted < 1 ? 1 : wanted;
    while (ratio > 1 && stepsFor(ratio) > postDivider)
        ratio /= 2;
    return ratio;
}

uint8_t Adc::stepsFor(uint8_t oversample)
{
    uint8_t steps = 0;
    for (uint8_t ratio = oversample; ratio > 1; ratio /= 2)
        ++steps;
    return steps;
}

uint8_t Adc::applyOversample(uint8_t postDivider, uint8_t oversample)
{
    const uint8_t ratio = oversampleFor(postDivider, oversample);

    PLLAD_CKOS::write((uint8_t)(postDivider - stepsFor(ratio)));

    // The decimators undo in the digital domain what the faster tap added, so
    // they follow the ratio rather than the tap.
    ADC_CLK_ICLK1X::write(ratio >= 2 ? 1 : 0);
    ADC_CLK_ICLK2X::write(ratio >= 4 ? 1 : 0);
    DEC1_BYPS::write(ratio >= 4 ? 0 : 1);
    DEC2_BYPS::write(ratio >= 2 ? 0 : 1);

    return ratio;
}

uint8_t Adc::applySampleRate(uint16_t divider, uint32_t lineRateHz,
                             uint8_t oversample)
{
    if (lineRateHz == 0) {
        // No CKO, so no row to read the crossover table against. The divider is
        // the caller's own and still goes in; picking a row by arithmetic on a
        // zero would be a guess wearing a calculation's clothes.
        PLLAD_MD::write(divider);
        latch();
        return oversample < 1 ? 1 : oversample;
    }

    uint8_t postDivider = postDividerFor((uint32_t)divider * lineRateHz);

    PLLAD_MD::write(divider);
    PLLAD_KS::write(postDivider);
    uint8_t ratio = applyOversample(postDivider, oversample);

    latch();
    return ratio;
}

}  // namespace Tv5725
