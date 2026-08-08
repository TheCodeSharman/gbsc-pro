// Host-compiled unit tests for src/input/HoldRamp.h -- `make -C test hold-ramp`.
//
// Pure logic with the clock injected, so the whole ramp is testable without a
// board, a remote, or a single sleep. That is the point of the class existing
// separately from the IR handler at all.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/input/HoldRamp.h"

// The bench remote's key codes; any two distinct values would do.
static const uint32_t KeyLeft = 0xEA52807F;
static const uint32_t KeyRight = 0xEA5240BF;

// The remote sends a repeat frame about every 110 ms while a key is held.
// Confirmed on the wire 2026-08-09: 8 of 31 frames in a bench capture were the
// repeat code.
static const unsigned long RepeatMs = 110;

TEST_CASE("a tap is always exactly one")
{
    HoldRamp ramp;

    SUBCASE("the first press of a run is one unit") {
        // The whole reason to hold the ramp off at the start: a tap has to be
        // able to mean one pixel, or precise alignment is impossible.
        CHECK(ramp.multiplierFor(KeyLeft, 1000) == 1);
    }

    SUBCASE("separate taps are all one unit") {
        // Deliberate presses, far enough apart to be separate intentions.
        unsigned long now = 1000;
        for (int i = 0; i < 5; ++i) {
            now += 700;
            CHECK(ramp.multiplierFor(KeyLeft, now) == 1);
        }
    }
}

TEST_CASE("a press held through the dead time is still one unit")
{
    // Without a dead time you cannot stop on a value: an ordinary press that
    // happens to linger takes off, and you overshoot every time.
    HoldRamp ramp;
    unsigned long now = 1000;
    CHECK(ramp.multiplierFor(KeyLeft, now) == 1);
    for (uint8_t i = 0; i < HoldRamp::DeadRepeats; ++i) {
        now += RepeatMs;
        CHECK(ramp.multiplierFor(KeyLeft, now) == 1);
    }
}

// One scenario rather than subcases: each step depends on the ramp state the
// previous one left, so splitting it would re-run the hold from cold and assert
// against a ramp that had never accelerated.
TEST_CASE("holding accelerates, never goes backwards, and then caps")
{
    HoldRamp ramp;
    unsigned long now = 1000;
    ramp.multiplierFor(KeyLeft, now);
    for (uint8_t i = 0; i < HoldRamp::DeadRepeats; ++i) {
        now += RepeatMs;
        ramp.multiplierFor(KeyLeft, now);
    }
    now += RepeatMs;
    int16_t first = ramp.multiplierFor(KeyLeft, now);
    CHECK(first > 1);

    int16_t previous = first;
    for (int i = 0; i < 40; ++i) {
        now += RepeatMs;
        int16_t step = ramp.multiplierFor(KeyLeft, now);
        CHECK(step >= previous);
        previous = step;
    }

    // A ramp with no ceiling crosses the whole line in one held press and there
    // is no way back to where you were.
    CHECK(previous == HoldRamp::MaxMultiplier);
}

TEST_CASE("a run ends when the key does, or when it changes")
{
    SUBCASE("a gap resets the ramp to one unit") {
        // Releasing the key stops the repeat frames; the next press is a new
        // intention and must be precise again.
        HoldRamp ramp;
        unsigned long now = 1000;
        for (int i = 0; i < 30; ++i) {
            ramp.multiplierFor(KeyLeft, now);
            now += RepeatMs;
        }
        CHECK(ramp.multiplierFor(KeyLeft, now) > 1);
        now += HoldRamp::RunGapMs + 1;
        CHECK(ramp.multiplierFor(KeyLeft, now) == 1);
    }

    SUBCASE("a different key resets the ramp to one unit") {
        // Ramping left at speed and then tapping right must not fling the
        // picture back the other way.
        HoldRamp faster;
        unsigned long now = 1000;
        for (int i = 0; i < 30; ++i) {
            faster.multiplierFor(KeyLeft, now);
            now += RepeatMs;
        }
        CHECK(faster.multiplierFor(KeyLeft, now) > 1);
        now += RepeatMs;
        CHECK(faster.multiplierFor(KeyRight, now) == 1);
    }
}

TEST_CASE("a repeat frame counts as the key it repeats")
{
    SUBCASE("a repeat frame continues the run rather than breaking it") {
        // The remote does not resend the key code while a key is held, it sends
        // a repeat marker. Handled here so the IR handler does not have to, and
        // so a build whose library reports repeats differently still ramps.
        HoldRamp ramp;
        unsigned long now = 1000;
        ramp.multiplierFor(KeyLeft, now);
        for (int i = 0; i < 30; ++i) {
            now += RepeatMs;
            ramp.multiplierFor(HoldRamp::RepeatCode, now);
        }
        CHECK(ramp.multiplierFor(HoldRamp::RepeatCode, now + RepeatMs) > 1);
    }

    SUBCASE("a repeat frame with no key before it is ignored") {
        HoldRamp cold;
        CHECK(cold.multiplierFor(HoldRamp::RepeatCode, 1000) == 1);
    }
}

TEST_CASE("the ramp survives millis() wrapping")
{
    // millis() rolls over every 49.7 days and this unit is left running for
    // weeks. Unsigned subtraction handles it; comparing timestamps does not.
    HoldRamp ramp;
    unsigned long justBefore = (unsigned long)-(long)(RepeatMs / 2);
    CHECK(ramp.multiplierFor(KeyLeft, justBefore) == 1);
    CHECK(ramp.multiplierFor(KeyLeft, justBefore + RepeatMs) == 1);
    for (int i = 0; i < 30; ++i)
        ramp.multiplierFor(KeyLeft, justBefore + RepeatMs * (i + 2));
    CHECK(ramp.multiplierFor(KeyLeft, justBefore + RepeatMs * 32) > 1);
}

TEST_CASE("a repeat frame resolves to the key it repeats")
{
    HoldRamp ramp;
    ramp.multiplierFor(KeyRight, 1000);

    SUBCASE("a repeat frame resolves to the held key") {
        // The IR handler dispatches on the key code, and a repeat frame matches
        // no case, so a held key would be dropped before it ever reached the
        // ramp. Resolving it here keeps that knowledge in one place.
        CHECK(ramp.resolve(HoldRamp::RepeatCode) == KeyRight);
    }

    SUBCASE("an ordinary key resolves to itself") {
        CHECK(ramp.resolve(KeyLeft) == KeyLeft);
    }

    SUBCASE("a repeat frame with nothing held resolves to itself") {
        // No key to attribute it to, so it must not be turned into one.
        HoldRamp cold;
        CHECK(cold.resolve(HoldRamp::RepeatCode) == HoldRamp::RepeatCode);
    }
}
