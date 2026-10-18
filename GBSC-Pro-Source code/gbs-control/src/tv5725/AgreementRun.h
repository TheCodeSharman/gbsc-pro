#ifndef TV5725_AGREEMENT_RUN_H_
#define TV5725_AGREEMENT_RUN_H_

// Two readings agreeing is the measurement, and a bounded escape when none
// ever do.
//
// **WHAT AGREEING MEANS IS THE CALLER'S**, because the quantities are not
// comparable: a field rate agrees inside a relative band, a sync duty inside
// the key's own tolerance, and a line count has an alternating pair to admit.
// Each of those tolerances is measured and belongs with its quantity. What is
// shared is the RUN -- one reading is not a measurement, and a reading nothing
// ever agrees with still has to let the mode change finish.
//
// **IT REJECTS NOISE AND CANNOT REJECT BIAS.** Two readings taken through the
// same wrong term agree with each other perfectly, so a run says a measurement
// REPEATS and nothing about whether it is right. What answers that is a check
// against something independently known -- the count behind a rate, the
// published raster behind a duty -- and no run substitutes for one.
// ../../../docs/investigations/the-key-carried-the-origins-calibration.md

#include <stdint.h>

namespace Tv5725 {

class AgreementRun {
public:
    // How many disagreements may be held before the latest reading is taken as
    // it stands.
    explicit AgreementRun(uint8_t budget);

    // Offer a reading, the caller saying whether it agrees with the one before
    // it. True once a pair has agreed, or once the budget is spent.
    bool offer(bool agreed);

    // Whether a reading is owed: one has been offered and nothing has agreed
    // with it yet.
    bool awaiting() const;

    void reset();

private:
    uint8_t budget_;
    uint8_t spent_;
};

}  // namespace Tv5725

#endif  // TV5725_AGREEMENT_RUN_H_
