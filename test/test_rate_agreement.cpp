// Host-compiled unit tests for src/clock/RateAgreement.h
// -- `make -C test rate-agreement`.
//
// Whether two independent measurements of the same rate are close enough to act
// on. The tolerance is upstream's, from runFrequency(); what is new is that the
// clock generator's seed asks it too.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/clock/RateAgreement.h"

using Clock::RateAgreement;

TEST_CASE("two readings of a settled source agree")
{
    // The bench source measured through the input formatter's test bus, twice.
    CHECK(RateAgreement::agree(50.080818f, 50.080818f));
    CHECK(RateAgreement::agree(50.08f, 50.12f));
    CHECK(RateAgreement::agree(60.0f, 60.4f));
}

TEST_CASE("the single-sample outlier that steered the clock does not agree")
{
    // Measured 2026-08-22 on a settled 320x256@50 source: getSourceFieldRate()
    // returns 51.14 among 50.08s, one sample in ten or so. The seed multiplied
    // by it and the output ran 51 Hz until FrameSync walked it back, which is
    // tens of seconds of dropped frames and an encoder locked to the wrong rate.
    CHECK_FALSE(RateAgreement::agree(50.08f, 51.14f));
    CHECK_FALSE(RateAgreement::agree(51.18f, 50.08f));
}

TEST_CASE("both bounds bind, because neither covers the band alone")
{
    // 0.83% of 50 Hz is 0.42 Hz, so the relative bound is the tighter one at
    // the bench rate; 0.83% of 86 Hz is 0.72 Hz, so the absolute one is tighter
    // at the top of the band getSourceFieldRate() accepts.
    CHECK_FALSE(RateAgreement::agree(50.0f, 50.45f));   // 0.9% apart, under 0.5 Hz
    CHECK_FALSE(RateAgreement::agree(85.0f, 85.6f));    // 0.7% apart, over 0.5 Hz
}

TEST_CASE("a reading that is not a rate agrees with nothing")
{
    CHECK_FALSE(RateAgreement::agree(0.0f, 0.0f));
    CHECK_FALSE(RateAgreement::agree(0.0f, 50.08f));
    CHECK_FALSE(RateAgreement::agree(-50.08f, 50.08f));
}
