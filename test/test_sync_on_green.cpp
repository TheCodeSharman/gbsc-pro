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

// The walk counts a run of samples inside a window in milliseconds, so the two
// are coupled: the run only completes where a register read is much faster
// than a millisecond. A bus read is about 100 us on the unit, so the clock here
// moves one millisecond every ten reads.
static uint32_t g_reads = 0;
static uint32_t testClock() { return g_reads++ / 10; }

static unsigned g_inForce = 0;
static uint8_t g_lastInForce = 0;

// Putting a level in force also latches the sampling phases and the ADC PLL.
static void putInForce()
{
    ++g_inForce;
    g_lastInForce = SyncOnGreen::level();
    SyncOnGreen::apply();
}

static void seedSlicer(uint8_t hsActive, uint8_t sliceBus)
{
    Wire.reset();
    Wire.poison(Poison);
    Wire.bank[0][0x16] = hsActive ? 0x02 : 0x00;   // STATUS_SYNC_PROC_HSACT, bit 1
    Wire.bank[0][0x2F] = sliceBus;                 // TEST_BUS_2F
    g_reads = 0;
    g_inForce = 0;
}

TEST_CASE("a slicer that is not in the sync path is left alone")
{
    seedSlicer(1, 0xFF);
    SyncType::set(false);
    SyncOnGreen::choose(7);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(g_inForce == 0);
    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
}

TEST_CASE("a slicer already producing clean edges keeps the level chosen")
{
    // The whole cost of the walk is paid per step, so a source that is already
    // good must not be walked off a level that works.
    seedSlicer(1, 0x05);
    SyncType::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(SyncOnGreen::level() == 11);
}

TEST_CASE("a slicer that never comes good walks to the floor and resets")
{
    // HSACT holds but the slicer's own output stays dead, which is the state
    // the ratchet exists for. Reaching the floor without finding a level puts
    // the default back rather than leaving the slicer wide open.
    seedSlicer(1, 0x00);
    SyncType::set(true);
    SyncOnGreen::choose(13);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
    CHECK(g_inForce > 1);
}

TEST_CASE("the level reaches the slicer through the injected action")
{
    // Writing ADC_SOGCTRL here instead would skip the phase and ADC PLL
    // latches that putting a level in force carries, and a divider written
    // without its latch leaves the PLL on the old value.
    seedSlicer(1, 0x05);
    SyncType::set(true);
    SyncOnGreen::choose(9);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(g_inForce >= 1);
    CHECK(g_lastInForce == 9);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 9);
}

// The coarse pass: two at a time, no settling runs. The sketch reaches for it
// while sync has only just gone, where acquire()'s windows would cost more than
// the attempt is worth.
TEST_CASE("the coarse pass leaves a slicer that is already reporting clean edges")
{
    seedSlicer(1, 0x05);            // both bits the coarse test asks for
    SyncType::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(SyncOnGreen::level() == 11);
    CHECK(g_inForce == 0);
}

TEST_CASE("the coarse pass steps down by two and resets at the floor")
{
    // 13 -> 11 -> 9 -> 7 -> 5 -> 3, then below 4 it puts the default back
    // rather than leaving the slicer near wide open.
    seedSlicer(1, 0x00);
    SyncType::set(true);
    SyncOnGreen::choose(13);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
    CHECK(g_inForce > 1);
}

TEST_CASE("the coarse pass leaves a slicer out of the sync path alone")
{
    seedSlicer(1, 0x00);
    SyncType::set(false);
    SyncOnGreen::choose(7);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(g_inForce == 0);
    CHECK(SyncOnGreen::level() == 7);   // left where it was, not reset
}
