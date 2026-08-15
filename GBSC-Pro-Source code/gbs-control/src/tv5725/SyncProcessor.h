#ifndef TV5725_SYNC_PROCESSOR_H
#define TV5725_SYNC_PROCESSOR_H

namespace Tv5725 {

// The sync processor's static half: polarity inversions, the thresholds it uses
// to decide a sync edge is real, and its stability counters.
//
// IT COUNTS IN ADC SAMPLES, NOT IF UNITS: three of this block's neighbours hold
// values well above the 1277-unit IF line, and HLOW_LEN only matches the
// source's mode file read in ADC -- 181/2553 = 7.09% against AKF50's 36/512 =
// 7.03%, where the IF reading gives twice the mode's sync width.
//
// What is not here is everything that moves per source: updateSpDynamic() owns
// the coast and delta quadruple, updateCoastPosition() SP_H_CST_ST/SP,
// updateClampPosition() the clamp, and Tv5725::Sampling SP_RT_HS_SP. A static
// write of any of those would fight a per-source decision.
//
// DO NOT POISON SP_RT_HS_SP TO TEST ANYTHING. Set 1110 against a 2553-sample
// line, and again at only 100 low, SP_VTOTAL fell to a steady 97/98 through the
// load and for minutes afterwards, needing /sc?~ to recover.
class SyncProcessor {
public:
    // Every static register of this subsystem, in address order.
    static void init();
};

}  // namespace Tv5725

#endif  // TV5725_SYNC_PROCESSOR_H
