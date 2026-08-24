// The encoder re-acquires when sync goes away, and the blank is held as state
// rather than toggled, so it cannot outlive the change that caused it.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SketchSeam.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncOutput.h"

using namespace Tv5725;

static bool padBlanked() { return Chip::PAD_SYNC_OUT_ENZ::read() == 1; }

TEST_CASE("the output blanks while a mode change is outstanding")
{
    Wire.reset();
    SyncOutput sync;

    CHECK(sync.poll(true, 1000));
    CHECK(padBlanked());

    SUBCASE("and re-asserts it for as long as the change runs") {
        // Every pass, not once: the preset and reset paths write this pad too,
        // and a blank they cleared mid-change defeats the whole ordering.
        CHECK(sync.poll(true, 1000 + 10 * SyncOutput::MinimumBlankMs));
        CHECK(padBlanked());
    }

    SUBCASE("and comes back once the change is done and the encoder has had its time") {
        CHECK(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs));
        CHECK_FALSE(padBlanked());
    }

    SUBCASE("a change that finishes faster than the encoder still holds the blank") {
        // The whole point of ordering it this way: the measurement absorbs the
        // relock, and only a change quicker than that pays anything.
        CHECK_FALSE(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs - 1));
        CHECK(padBlanked());

        CHECK(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs));
        CHECK_FALSE(padBlanked());
    }
}

TEST_CASE("a settled engine leaves the output alone")
{
    Wire.reset();
    SyncOutput sync;

    // Nothing outstanding and nothing blanked: the pass must not write, or the
    // bus carries a register write on every loop.
    CHECK_FALSE(sync.poll(false, 500));
    CHECK_FALSE(sync.poll(false, 5000));
}

TEST_CASE("a second change while blanked restarts the encoder's time")
{
    Wire.reset();
    SyncOutput sync;

    REQUIRE(sync.poll(true, 1000));
    REQUIRE(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs));
    REQUIRE_FALSE(padBlanked());

    REQUIRE(sync.poll(true, 2000));
    CHECK(padBlanked());
    CHECK_FALSE(sync.poll(false, 2000 + SyncOutput::MinimumBlankMs - 1));
    CHECK(padBlanked());
}

TEST_CASE("a press can blank before the engine knows anything has changed")
{
    // A preset command writes output registers on its way to telling the
    // engine, and the picture glitches if the blank waits for that.
    Wire.reset();
    SyncOutput sync;

    sync.blankNow(1000);
    CHECK(padBlanked());

    SUBCASE("and it is held for the encoder's time even if no change follows") {
        CHECK_FALSE(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs - 1));
        CHECK(padBlanked());
        CHECK(sync.poll(false, 1000 + SyncOutput::MinimumBlankMs));
        CHECK_FALSE(padBlanked());
    }

    SUBCASE("and the change that follows extends it rather than restarting it") {
        REQUIRE(sync.poll(true, 1100));
        CHECK(padBlanked());
        CHECK_FALSE(sync.poll(false, 1100 + SyncOutput::MinimumBlankMs - 1));
        CHECK(padBlanked());
    }
}
