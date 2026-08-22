#ifndef CLOCK_RATE_AGREEMENT_H
#define CLOCK_RATE_AGREEMENT_H

namespace Clock {

// Whether two independent measurements of the same frame rate are close enough
// that either can be acted on.
//
// One sample off the debug pin is a single pulse timed on a CPU that takes
// interrupts, and it is wrong by percent often enough to matter: the readers
// steer hardware at the answer, so a disputed pair is worth another pass rather
// than a decision. docs/investigations/single-sample-rate-jitter.md
class RateAgreement {
public:
    // Upstream's, from runFrequency(), which is the one reader that already
    // measured twice. Both bind: the relative bound is the tighter one at 50 Hz
    // and the absolute one at the top of the band.
    static constexpr float RelativeTolerance = 0.00833f;
    static constexpr float AbsoluteToleranceHz = 0.5f;

    static bool agree(float oneHz, float otherHz);
};

}  // namespace Clock

#endif  // CLOCK_RATE_AGREEMENT_H
