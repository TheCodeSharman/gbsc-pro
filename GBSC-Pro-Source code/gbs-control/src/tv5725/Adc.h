#ifndef TV5725_ADC_H
#define TV5725_ADC_H

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
    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_ADC_H
