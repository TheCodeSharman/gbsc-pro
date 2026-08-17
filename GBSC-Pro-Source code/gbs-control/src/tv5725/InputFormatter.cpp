#include "InputFormatter.h"

namespace Tv5725 {

void InputFormatter::init()
{
    IF_IN_DREG_BYPS::write(0x0);                 // s1_00[0:0]
    IF_UV_REVERT::write(0x0);                    // s1_00[2:2]
    IF_SEL_656::write(0x0);                      // s1_00[3:3]
    IF_SEL16BIT::write(0x0);                     // s1_00[4:4]
    IF_HS_FLIP::write(0x0);                      // s1_00[7:7]
    IF_UV_FLIP::write(0x0);                      // s1_01[1:1]
    IF_U_DELAY::write(0x0);                      // s1_01[2:2]
    IF_V_DELAY::write(0x0);                      // s1_01[3:3]
    IF_TAP6_BYPS::write(0x0);                    // s1_01[4:4]
    IF_Y_DELAY::write(0x3);                      // s1_01[6:5]
    IF_SEL24BIT::write(0x1);                     // s1_01[7:7]

    // doPostPresetLoadSteps() leaves these to `if (rto->inputIsYpBpR)` and to
    // the standard 3/4/8/9 branch, and 15 kHz RGB into a non-custom preset takes
    // neither, so the table was the only writer. 0 and 3 are what all twelve
    // ship, bar IF_HS_Y_PDELAY 2 in ntsc_1920x1080, which the YPbPr branch still
    // asks for afterwards.
    IF_HS_TAP11_BYPS::write(0x0);                // s1_02[4:4]
    IF_HS_Y_PDELAY::write(0x3);                  // s1_02[6:5]
    IF_HS_UV_SIGN2UNSIGN::write(0x0);            // s1_02[7:7]
    IF_HS_RATE_SEG0::write(0x0);                 // s1_03[7:0]
    IF_HS_RATE_SEG1::write(0x0);                 // s1_04[7:0]
    IF_HS_RATE_SEG2::write(0x0);                 // s1_05[7:0]
    IF_HS_RATE_SEG3::write(0x0);                 // s1_06[7:0]
    IF_HS_RATE_SEG4::write(0x0);                 // s1_07[7:0]
    IF_HS_RATE_SEG5::write(0x0);                 // s1_08[7:0]
    IF_HS_RATE_SEG6::write(0x0);                 // s1_09[7:0]
    IF_HS_RATE_SEG7::write(0x0);                 // s1_0a[7:0]
    IF_HS_RATE_LOW::write(0x0);                  // s1_0b[3:0]

    // The non-linear scaling-down factor select: 00 is a ratio over 1/2, 01
    // under 1/2, 10 under 1/4 (RD-5725-1.1, s1_0b[5:4]). Nothing consults it
    // here -- every IF_HS_RATE_SEG above is 0, so the scaling-down DDA is not
    // running -- and it is written at what the ten scaling tables ship rather
    // than at what a zero rate implies, because that is the picture that works.
    IF_HS_DEC_FACTOR::write(0x1);                // s1_0b[5:4]
    IF_SEL_HSCALE::write(0x1);                   // s1_0b[6:6]

    // The line double's write reset start position (RD-5725-1.1, s1_0c[4:1]).
    // The datasheet says what the counter does and nothing about choosing the
    // value, and the tables offer 5 and 3 with no rule behind either -- not the
    // PAL/NTSC split it looks like, since the PAL six split 5/3/3/5/5/5. 5 is
    // the bench-proven one.
    IF_LD_ST::write(5);              // s1_0c[4:1]

    // Horizontal blanking set 0. The IF module has three sets -- 0 at
    // s1_10/s1_12, 1 at s1_14/s1_16, 2 at s1_18/s1_1a -- with no selector
    // documented between them; Geometry::write() owns set 2, the capture window
    // and the one that demonstrably moves the picture. Set 1 is deliberately
    // absent, measured inert and asserted absent by test_bringup.cpp
    // (docs/investigations/preset-abandonment-audit.md). Set 0 has not had that
    // experiment, so it is written at what the ten scaling tables ship.
    IF_HB_ST::write(2);                          // s1_10[10:0]
    IF_HB_SP::write(72);                         // s1_12[10:0]

    // The scale-down path's blanking stop, the one field of the six with no
    // constant across the ten scaling tables (136..272) and no derivation, so
    // this is inherited rather than computed.
    IF_HBIN_SP::write(272);                      // s1_26[11:0]

    IF_SEL_ADC_SYNC::write(0x1);                 // s1_28[2:2]
}

}  // namespace Tv5725
