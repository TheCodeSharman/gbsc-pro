#ifndef TV5725_ADC_H
#define TV5725_ADC_H

#include "Tv5725.h"

namespace Tv5725 {

// The ADC and its PLL: power, trim, test paths and the auto-offset that is
// switched off.
//
// The PLL's RATE is deliberately absent: PLLAD_MD is Tv5725::Sampling's, held as
// state and written with IF_HSYNC_RST and SP_RT_HS_SP immediately before
// latchPLLAD(). A divider written after the latch leaves the PLL running the old
// value with every register reading back correct -- a solid green screen with
// nothing to diagnose from, and sync survives it. PLLAD_ND is here at 0 and is a
// different thing: latched by the same edge, but not rate.
//
// ADC_AUTO_OFST_EN = 0 leaves the offsets to setAdcParametersGainAndOffset() and
// the auto-gain loop, which are per source and belong in the sketch.
// ADC_TR_RSEL, ADC_TR_ISEL, ADC_TA_CTRL and ADC_TEST are analog trim and test
// selects with no derivation available -- what all twelve tables shipped.
class Adc {
public:
    typedef UReg<0x05, 0x00, 0, 8> ADC_5_00;

    typedef UReg<0x05, 0x00, 0, 2> ADC_CLK_PA;                        // Clock selection for PA_ADC When = 00, PA_ADC input clock
                                                                      // is from PLLAD’s CLKO2 When = 01, PA_ADC input clock is
                                                                      // from PCLKIN When = 10, PA_ADC input clock is from V4CLK

    typedef UReg<0x05, 0x00, 2, 1> ADC_CLK_PLLAD;                     // When = 11, reserved Clock selection for PLLAD When = 0,
                                                                      // PLLAD input clock is from sync processor When = 1, PLLAD
                                                                      // input clock is from OSC

    typedef UReg<0x05, 0x00, 3, 1> ADC_CLK_ICLK2X;                    // ICLK2X control When = 0, ICLK2X = ADC output clock

    typedef UReg<0x05, 0x00, 4, 1> ADC_CLK_ICLK1X;                    // When = 1, ICLK2X = ADC output clock / 2 ICLK1X control
                                                                      // When = 0, ICLK1X = ICLK2X When = 1, ICLK1X = ICLK2X /2

    typedef UReg<0x05, 0x02, 0, 1> ADC_SOGEN;                         // ADC SOG enable When = 0, ADC disable SOG mode

    typedef UReg<0x05, 0x02, 1, 5> ADC_SOGCTRL;                       // When = 1, ADC enable SOG mode SOG control signal ADC
                                                                      // input selection When = 00, R0/G0/B0/SOG0 as input

    typedef UReg<0x05, 0x02, 6, 2> ADC_INPUT_SEL;                     // When = 01, R1/G1/B1/SOG1 as input When = 10, R2/G2/B2 as
                                                                      // input When = 11, reserved

    typedef UReg<0x05, 0x03, 0, 8> ADC_5_03;

    typedef UReg<0x05, 0x03, 0, 1> ADC_POWDZ;                         // ADC power down control When = 0, ADC in power down mode

    typedef UReg<0x05, 0x03, 1, 1> ADC_RYSEL_R;                       // When = 1, ADC work normally Clamp to ground or midscale
                                                                      // for R ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 2, 1> ADC_RYSEL_G;                       // Clamp to ground or midscale for G ADC When = 0, clamp to
                                                                      // GND

    typedef UReg<0x05, 0x03, 3, 1> ADC_RYSEL_B;                       // When = 1, clamp to midscale Clamp to ground or midscale
                                                                      // for B ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 4, 2> ADC_FLTR;                          // ADC internal filter control When = 00, 150MHz When = 01,
                                                                      // 110MHz When = 10, 70MHz

    typedef UReg<0x05, 0x04, 0, 8> ADC_TEST_04;

    typedef UReg<0x05, 0x04, 0, 2> ADC_TR_RSEL;                       // REF test resistor selection

    typedef UReg<0x05, 0x04, 2, 3> ADC_TR_ISEL;                       // REF test currents selection

    typedef UReg<0x05, 0x05, 0, 8> ADC_TA_05_CTRL;

    typedef UReg<0x05, 0x05, 0, 1> ADC_TA_EN;                         // ADC test enable When = 0, ADC work normally

    typedef UReg<0x05, 0x05, 1, 4> ADC_TA_CTRL;                       // When = 1, ADC is in test mode ADC test bus control bit

    typedef UReg<0x05, 0x06, 0, 7> ADC_ROFCTRL;                       // Offset control for R channel of ADC

    typedef UReg<0x05, 0x07, 0, 7> ADC_GOFCTRL;                       // Offset control for G channel of ADC

    typedef UReg<0x05, 0x08, 0, 7> ADC_BOFCTRL;                       // Offset control for B channel of ADC

    typedef UReg<0x05, 0x09, 0, 8> ADC_RGCTRL;                        // Gain control for R channel of ADC

    typedef UReg<0x05, 0x0A, 0, 8> ADC_GGCTRL;                        // Gain control for G channel of ADC

    typedef UReg<0x05, 0x0B, 0, 8> ADC_BGCTRL;                        // Gain control for B channel of ADC

    typedef UReg<0x05, 0x0C, 0, 8> ADC_TEST_0C;

    typedef UReg<0x05, 0x0C, 0, 1> ADC_CKBS;                          // ADC output clock invert control When = 0, default

    typedef UReg<0x05, 0x0C, 1, 4> ADC_TEST;                          // When = 1, ADC output clock will be invert For ADC test
                                                                      // reserved

    typedef UReg<0x05, 0x0E, 0, 1> ADC_AUTO_OFST_EN;                  // Auto offset adjustment enable When = 0, auto offset
                                                                      // adjustment disable

    typedef UReg<0x05, 0x0E, 1, 1> ADC_AUTO_OFST_PRD;                 // When = 1, auto offset adjustment enable Offset adjustment
                                                                      // by frame When = 0, offset adjustment by frame When = 1,
                                                                      // offset adjustment by line

    typedef UReg<0x05, 0x0E, 2, 2> ADC_AUTO_OFST_DELAY;               // Horizontal sample delay control When = 00, offset
                                                                      // adjustment horizontal sample delay 1 pipe When = 01,
                                                                      // offset adjustment horizontal sample delay 2 pipe When =
                                                                      // 10, offset adjustment horizontal sample delay 3 pipe

    typedef UReg<0x05, 0x0E, 4, 2> ADC_AUTO_OFST_STEP;                // When = 11, offset adjustment horizontal sample delay 4
                                                                      // pipe Offset adjustment step control When = 00, offset
                                                                      // adjustment by absolute difference. When = 01, offset
                                                                      // adjustment by 1 When = 10, offset adjustment by 2 When =
                                                                      // 11, offset adjustment by 3

    typedef UReg<0x05, 0x0E, 7, 1> ADC_AUTO_OFST_TEST;                // Auto offset adjustment test control

    typedef UReg<0x05, 0x0F, 0, 8> ADC_AUTO_OFST_RANGE_REG;

    typedef UReg<0x05, 0x0F, 0, 4> ADC_AUTO_OFST_U_RANGE;             // U channel offset detection range Define U channel offset
                                                                      // detection range 0~15

    typedef UReg<0x05, 0x0F, 4, 4> ADC_AUTO_OFST_V_RANGE;             // V channel offset detection range Define V channel offset
                                                                      // detection range 0~15

    typedef UReg<0x05, 0x11, 0, 8> PLLAD_CONTROL_00_5x11;

    typedef UReg<0x05, 0x11, 0, 1> PLLAD_VCORST;                      // VCORST Initial VCO control voltage

    typedef UReg<0x05, 0x11, 1, 1> PLLAD_LEN;                         // LEN Enable signal for clock

    typedef UReg<0x05, 0x11, 2, 1> PLLAD_TEST;                        // TEST Test clock selection

    typedef UReg<0x05, 0x11, 3, 1> PLLAD_TS;                          // TS Test clock selection and HSL clock selection

    typedef UReg<0x05, 0x11, 4, 1> PLLAD_PDZ;                         // PDZ When = 0, PLLAD is power down mode

    typedef UReg<0x05, 0x11, 5, 1> PLLAD_FS;                          // When = 1, PLLAD work normally FS, VCO gain selection When
                                                                      // = 0, default When = 1, high gain selected

    typedef UReg<0x05, 0x11, 6, 1> PLLAD_BPS;                         // BPS When = 0, default

    typedef UReg<0x05, 0x11, 7, 1> PLLAD_LAT;                         // When = 1, bypass input clock to CKO1 and CKO2 Latch
                                                                      // control for PLLAD control This bit’s rising edge is used
                                                                      // to trigger PLLAD control bit: ND, MD, KS, CKOS, ICP

    typedef UReg<0x05, 0x12, 0, 12> PLLAD_MD;                         // MD[11:8] PLLAD feedback divider control

    typedef UReg<0x05, 0x14, 0, 12> PLLAD_ND;                         // ND[11:8] PLLAD input divider control

    typedef UReg<0x05, 0x16, 0, 8> PLLAD_5_16;

    typedef UReg<0x05, 0x16, 0, 2> PLLAD_R;                           // R Skew control for testing

    typedef UReg<0x05, 0x16, 2, 2> PLLAD_S;                           // S Skew control for testing

    typedef UReg<0x05, 0x16, 4, 2> PLLAD_KS;                          // KS VCO post divider control, it is determined by CKO
                                                                      // frequency When = 00, divide by 1 (162MHz~80MHz) When =
                                                                      // 01, divide by 2 (80MHz~40MHz) When = 10, divide by 4
                                                                      // (40MHz~20MHz) When = 11, divide by 8 (20MHz~min MHz)

    typedef UReg<0x05, 0x16, 6, 2> PLLAD_CKOS;                        // Part of PLLAD_CKOS, which RD-5725-1.1 documents as one
                                                                      // 2-bit block at s5_16 rather than field by field.

    typedef UReg<0x05, 0x17, 0, 3> PLLAD_ICP;                         // ICP Charge pump current selection When = 000, Icp = 50uA
                                                                      // When = 001, Icp = 100uA When = 010, Icp = 150uA When =
                                                                      // 011, Icp = 250uA When = 100, Icp = 350uA When = 101, Icp
                                                                      // = 500uA When = 110, Icp = 750uA When = 111, Icp = 1mA

    typedef UReg<0x05, 0x18, 0, 1> PA_ADC_BYPSZ;                      // BYPSZ, PA for ADC bypass control When = 0, PA_ADC is
                                                                      // bypass

    typedef UReg<0x05, 0x18, 1, 5> PA_ADC_S;                          // When = 1, PA_ADC work normally PA_ADC phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x18, 6, 1> PA_ADC_LOCKOFF;                    // When = 0, default

    typedef UReg<0x05, 0x18, 7, 1> PA_ADC_LAT;                        // When = 1, PA_ADC lock circuit disable PA_ADC latch signal
                                                                      // This bit’s rising edge is used to trigger
                                                                      // PA_ADC_CNTRL_[5:1]

    typedef UReg<0x05, 0x19, 0, 1> PA_SP_BYPSZ;                       // BYPSZ, PA for PLLAD bypass control When = 0, PA_PLLAD is
                                                                      // bypass

    typedef UReg<0x05, 0x19, 1, 5> PA_SP_S;                           // When = 1, PA_PLLAD work normally PA_PLLAD phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x19, 6, 1> PA_SP_LOCKOFF;                     // When = 0, default

    typedef UReg<0x05, 0x19, 7, 1> PA_SP_LAT;                         // When = 1, PA_PLLAD lock circuit disable PA_PLLAD latch
                                                                      // signal This bit’s rising edge is used to trigger
                                                                      // PA_PLLAD_CNTRL_[5:1]

    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_ADC_H
