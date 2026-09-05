// Host-compiled tests for Tv5725::SyncOnGreen -- `make -C test sync-on-green`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncOnGreen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncType.h"

using namespace Tv5725;

static const uint8_t Poison = 0xE2;

TEST_CASE("the level asked for reaches the slicer")
{
    Wire.reset();
    Wire.poison(Poison);

    SyncOnGreen::apply(12);

    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 12);
}

TEST_CASE("the level is what was held, not what the register reads")
{
    // The register is an output. A caller deriving the next level from a
    // read-back is asking the chip what it was told, and every ratchet in the
    // sketch did exactly that.
    Wire.reset();
    Wire.poison(Poison);
    SyncOnGreen::apply(12);

    SyncOnGreen::ADC_SOGCTRL::write(3);

    CHECK(SyncOnGreen::level() == 12);
}

TEST_CASE("a level past the field is refused rather than truncated")
{
    // Five bits. The old path masked with 0x1f, so 32 arrived as 0 -- the
    // slicer fully open, which is the one value that cannot be recovered from
    // by ratcheting down.
    Wire.reset();
    Wire.poison(Poison);
    SyncOnGreen::apply(12);

    SyncOnGreen::apply(32);

    CHECK(SyncOnGreen::level() == 12);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 12);
}

TEST_CASE("the slicer is in the sync path only on a csync source")
{
    // SP_SOG_MODE follows the sync type, and the slicer only reaches the sync
    // processor with it 1 -- so on a separate-sync source every level is inert
    // and a recovery that walks it is moving a control nothing is reading.
    // Measured on the bench VGA input: SP_SOG_MODE 0, ADC_SOGCTRL walked 12 to
    // 5, and the level was the first thing blamed for a black screen it could
    // not have caused.
    // docs/investigations/the-no-sync-branch-is-the-only-escape.md
    SyncType::set(true);
    CHECK(SyncOnGreen::inSyncPath());

    SyncType::set(false);
    CHECK_FALSE(SyncOnGreen::inSyncPath());
}

TEST_CASE("the answer is held state, not SP_SOG_MODE read back")
{
    // The register echoes the sync type applyForSyncType() wrote, so reading it
    // asks the chip what it was told -- and during a probe the two disagree.
    Wire.reset();
    Wire.poison(Poison);
    SyncType::set(false);

    SyncProcessor::SP_SOG_MODE::write(1);

    CHECK_FALSE(SyncOnGreen::inSyncPath());
}
