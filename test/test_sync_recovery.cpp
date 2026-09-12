// Host-compiled unit tests for src/videosource/SyncRecovery.cpp
// -- `make -C test sync-recovery`.
//
// Pure arithmetic, so no fake Wire and no sketch symbols.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/SyncRecovery.h"



TEST_CASE("the first failed pass escalates nothing")
{
    CHECK(SyncRecovery::stepAt(0) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(1) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(2) == SyncRecovery::LiftSogFloor);
}

TEST_CASE("each step fires at the count the moduli first fired it at")
{
    CHECK(SyncRecovery::stepAt(2) == SyncRecovery::LiftSogFloor);
    CHECK(SyncRecovery::stepAt(8) == SyncRecovery::CoastWindow);
    CHECK(SyncRecovery::stepAt(27) == SyncRecovery::SyncProcessorDynamic);
    CHECK(SyncRecovery::stepAt(32) == SyncRecovery::ReleaseCapture);
    CHECK(SyncRecovery::stepAt(34) == SyncRecovery::HoldClamp);
    CHECK(SyncRecovery::stepAt(38) == SyncRecovery::NudgeModeDetect);
    CHECK(SyncRecovery::stepAt(48) == SyncRecovery::HsyncOverflowProtect);
    CHECK(SyncRecovery::stepAt(150) == SyncRecovery::FullReset);
    CHECK(SyncRecovery::stepAt(413) == SyncRecovery::ToggleInput);
    CHECK(SyncRecovery::stepAt(450) == SyncRecovery::ReopenSogSeparator);
}

TEST_CASE("the sync-type re-probe follows the reset instead of sharing its count")
{
    // It sat inside the % 150 block, so both ran on one pass. One step per
    // position is the whole point of the list, and a pass is about 20 ms.
    CHECK(SyncRecovery::stepAt(150) == SyncRecovery::FullReset);
    CHECK(SyncRecovery::stepAt(151) == SyncRecovery::ReprobeSyncType);
}

TEST_CASE("the overflow-protect step avoids the count the power check writes")
{
    // loop() rewrites the counter to 63 when the board-power check at 61 passes,
    // and the next pass increments past it, so a step at 63 can never be seen.
    CHECK(SyncRecovery::stepAt(63) == SyncRecovery::None);
}

TEST_CASE("nothing fires between the steps")
{
    // The moduli fired again and again -- 27, 54, 81 -- which is the behaviour
    // the list removes. 54 and 81 are the check that it did.
    CHECK(SyncRecovery::stepAt(3) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(26) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(54) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(64) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(81) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(300) == SyncRecovery::None);
    CHECK(SyncRecovery::stepAt(412) == SyncRecovery::None);
}

TEST_CASE("a repeat of an earlier modulus no longer reaches its step")
{
    // 64 is 4 x 16 and past 47, which used to toggle the overflow protect; 300
    // is 2 x 150, which used to run the whole reset block a second time.
    CHECK(SyncRecovery::stepAt(64) != SyncRecovery::HsyncOverflowProtect);
    CHECK(SyncRecovery::stepAt(300) != SyncRecovery::FullReset);
}

TEST_CASE("the list cycles, because an unplugged source needs it to")
{
    // Stopping would leave a unit switched off and on again with no way back,
    // which is what the moduli never doing so was buying.
    CHECK(SyncRecovery::stepAt(451 + 2) == SyncRecovery::LiftSogFloor);
    CHECK(SyncRecovery::stepAt(451 + 150) == SyncRecovery::FullReset);
    CHECK(SyncRecovery::stepAt(451 + 413) == SyncRecovery::ToggleInput);
    CHECK(SyncRecovery::stepAt(3 * 451 + 8) == SyncRecovery::CoastWindow);
}

TEST_CASE("the input toggle stays the rarest step, once per cycle")
{
    // It moves the ADC mux, so it is the guess of last resort. Being last in
    // the list is what its % 413 was approximating.
    uint16_t toggles = 0;
    for (uint16_t p = 0; p < 3 * SyncRecovery::CycleLength; ++p) {
        if (SyncRecovery::stepAt(p) == SyncRecovery::ToggleInput)
            ++toggles;
    }
    CHECK(toggles == 3);
}

TEST_CASE("every step is reachable exactly once per cycle")
{
    for (uint8_t s = SyncRecovery::LiftSogFloor; s <= SyncRecovery::ReopenSogSeparator; ++s) {
        CAPTURE(s);
        uint16_t seen = 0;
        for (uint16_t p = 0; p < SyncRecovery::CycleLength; ++p) {
            if (SyncRecovery::stepAt(p) == (SyncRecovery::Step)s)
                ++seen;
        }
        CHECK(seen == 1);
    }
}

TEST_CASE("positionOf names where a step sits, and None sits nowhere")
{
    CHECK(SyncRecovery::positionOf(SyncRecovery::LiftSogFloor) == 2);
    CHECK(SyncRecovery::positionOf(SyncRecovery::ToggleInput) == 413);
    CHECK(SyncRecovery::positionOf(SyncRecovery::ReopenSogSeparator) == 450);
    CHECK(SyncRecovery::positionOf(SyncRecovery::None) == 0);
}
