// Host-compiled unit tests for src/clock/RateAgreement.h
// -- `make -C test rate-agreement`.
//
// Whether two measurements of the same rate are close enough to act on. The
// display clock is set to the RATIO of two of these, so a pair that agrees and
// is wrong beats against the source for as long as the boot runs.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/clock/RateAgreement.h"

using Clock::RateAgreement;

// What the bench measures, to the milli-hertz the firmware prints: 48 samples
// across four mode changes, 16 of each rate, and every one of them identical.
static const float Bench60 = 60.317f;
static const float Bench50 = 50.080f;

TEST_CASE("two readings of a settled source agree")
{
    CHECK(RateAgreement::agree(Bench60, Bench60));
    CHECK(RateAgreement::agree(Bench50, Bench50));

    // The whole spread 97 consecutive measurements of one source produced.
    CHECK(RateAgreement::agree(60.317314f, 60.317406f));
}

// **THE PART QUANTISES, AND ONE STEP IS ONE SOURCE LINE.** Measured at
// 800x600@60: the field period is 2652636 CPU ticks and the wrong readings sit
// at multiples of 4224 away, which is that field divided by its 627 lines. A
// pair one step apart is two different readings of one rate, not a rate either
// of them states -- and a tolerance that admits one puts the display clock
// there. docs/known-issues.md
TEST_CASE("a pair one source line apart does not agree")
{
    SUBCASE("at the 627 lines of the bench 60 Hz mode") {
        CHECK_FALSE(RateAgreement::agree(Bench60, Bench60 * (1.0f + 1.0f / 627.0f)));
        CHECK_FALSE(RateAgreement::agree(Bench60, Bench60 * (1.0f - 1.0f / 627.0f)));
    }

    SUBCASE("at the 311 lines of the bench 50 Hz mode") {
        CHECK_FALSE(RateAgreement::agree(Bench50, Bench50 * (1.0f + 1.0f / 311.0f)));
    }

    // The tolerance has to stay under one line for a source of many lines too,
    // where a line is the smallest fraction of the field.
    SUBCASE("at the 1125 lines of a 1080-line source") {
        CHECK_FALSE(RateAgreement::agree(Bench60, Bench60 * (1.0f + 1.0f / 1125.0f)));
    }
}

TEST_CASE("the outlier that steered the clock does not agree")
{
    // One boot of six logged `rate match: source 60801 mHz` against a source
    // running 60317, put the output 0.8% off it, and shook for the life of the
    // boot. Nothing re-measures until the next solve.
    CHECK_FALSE(RateAgreement::agree(60.317f, 60.801f));

    // And the 320x256@50 case that came before it: 51.14 among 50.08s.
    CHECK_FALSE(RateAgreement::agree(50.08f, 51.14f));
    CHECK_FALSE(RateAgreement::agree(51.18f, 50.08f));
}

TEST_CASE("a reading that is not a rate agrees with nothing")
{
    CHECK_FALSE(RateAgreement::agree(0.0f, 0.0f));
    CHECK_FALSE(RateAgreement::agree(0.0f, 50.08f));
    CHECK_FALSE(RateAgreement::agree(-50.08f, 50.08f));
}
