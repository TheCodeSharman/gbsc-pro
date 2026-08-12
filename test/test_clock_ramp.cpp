// Host-compiled unit tests for src/clock/ClockRamp.h -- `make -C test clock-ramp`.
//
// How the Si5351's output frequency is allowed to MOVE, as opposed to what it is
// set to. Two policies that were buried in the sketch and framesync.h with no
// test and no stated reason, extracted with what evidence there is.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/clock/ClockRamp.h"

using namespace Clock;

TEST_CASE("a small move is ramped and a large one is not")
{
    // Both thresholds are upstream's, carried over unexplained: 1 kHz steps, and
    // only for moves under 750 kHz. The original's two near-identical branches
    // for rising and falling use the same threshold and the same step, so there
    // is no asymmetry to preserve.
    CHECK(ClockRamp::ramps(81000000u, 81000500u));
    CHECK(ClockRamp::ramps(81000000u, 80999500u));
    CHECK(ClockRamp::ramps(81000000u, 81749999u));
    CHECK(ClockRamp::ramps(81000000u, 80250001u));

    SUBCASE("and 750 kHz is the edge") {
        CHECK_FALSE(ClockRamp::ramps(81000000u, 81750000u));
        CHECK_FALSE(ClockRamp::ramps(81000000u, 80250000u));
        // The jump this exists to allow: 81 -> 108 MHz is a preset change.
        CHECK_FALSE(ClockRamp::ramps(81000000u, 108000000u));
    }

    SUBCASE("no move is not a ramp") {
        CHECK_FALSE(ClockRamp::ramps(81000000u, 81000000u));
    }
}

TEST_CASE("advance walks in 1 kHz steps and lands exactly on the target")
{
    // The caller loops until advance() returns the target, writing each value. So
    // the sequence has to terminate ON the target rather than near it -- a clock
    // left 500 Hz out is a slow roll, which is what a beat looks like.
    uint32_t current = 1000000u;
    const uint32_t target = 996500u;

    uint32_t written[8];
    unsigned count = 0;
    while (current != target && count < 8) {
        current = ClockRamp::advance(current, target);
        written[count++] = current;
    }

    REQUIRE(count == 4);
    CHECK(written[0] == 999000u);
    CHECK(written[1] == 998000u);
    CHECK(written[2] == 997000u);
    CHECK(written[3] == target);

    SUBCASE("upwards is the mirror image") {
        uint32_t up = 996500u;
        const uint32_t high = 1000000u;
        unsigned steps = 0;
        while (up != high && steps < 8) {
            up = ClockRamp::advance(up, high);
            ++steps;
        }
        CHECK(steps == 4);
        CHECK(up == high);
    }

    SUBCASE("a move too big to ramp arrives in one write") {
        CHECK(ClockRamp::advance(81000000u, 108000000u) == 108000000u);
    }

    SUBCASE("already there is one write, not zero") {
        // The original always finishes with a direct set, even when the loop did
        // nothing, so a caller that trusted a zero-step answer would skip it.
        CHECK(ClockRamp::advance(81000000u, 81000000u) == 81000000u);
    }
}

TEST_CASE("two target frequencies are preloaded through an intermediate")
{
    // Upstream's, unexplained and kept verbatim: 87 MHz before 108, 48.5 before
    // 40.5, each with a 1 ms settle. It is in every working state measured on
    // this board and costs 1 ms. **DO NOT REMOVE IT WITHOUT A MEASUREMENT.**
    CHECK(ClockRamp::preloadFor(108000000u) == 87000000u);
    CHECK(ClockRamp::preloadFor(40500000u) == 48500000u);

    SUBCASE("everything else goes straight there") {
        CHECK(ClockRamp::preloadFor(81000000u) == 0u);
        CHECK(ClockRamp::preloadFor(129600000u) == 0u);
        CHECK(ClockRamp::preloadFor(162000000u) == 0u);
        CHECK(ClockRamp::preloadFor(0u) == 0u);
    }

    SUBCASE("129.6 MHz has no preload and the bench ran it anyway") {
        // Worth pinning: the 2026-08-11 sweep reached 129.6 MHz with no preload
        // and the picture was sharp, so the preload is not a general requirement
        // for high frequencies.
        CHECK(ClockRamp::preloadFor(129600000u) == 0u);
    }
}
