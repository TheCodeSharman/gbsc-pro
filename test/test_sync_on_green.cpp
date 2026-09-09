// Host-compiled tests for Tv5725::SyncOnGreen -- `make -C test sync-on-green`.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncOnGreen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"

void tv5725Log(const char *) {}

using namespace Tv5725;

static const uint8_t Poison = 0xE2;

TEST_CASE("the level asked for reaches the sync separator")
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
    // sync separator fully open, which is the one value that cannot be recovered from
    // by ratcheting down.
    Wire.reset();
    Wire.poison(Poison);
    SyncOnGreen::apply(12);

    SyncOnGreen::apply(32);

    CHECK(SyncOnGreen::level() == 12);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 12);
}

TEST_CASE("the sync separator is in the sync path only on a csync source")
{
    // SP_SOG_MODE follows the sync type, and the sync separator only reaches the sync
    // processor with it 1 -- so on a separate-sync source every level is inert
    // and a recovery that walks it is moving a control nothing is reading.
    // Measured on the bench VGA input: SP_SOG_MODE 0, ADC_SOGCTRL walked 12 to
    // 5, and the level was the first thing blamed for a black screen it could
    // not have caused.
    // docs/investigations/the-no-sync-branch-is-the-only-escape.md
    SyncMeasurement::set(true);
    CHECK(SyncOnGreen::inSyncPath());

    SyncMeasurement::set(false);
    CHECK_FALSE(SyncOnGreen::inSyncPath());
}

TEST_CASE("the answer is held state, not SP_SOG_MODE read back")
{
    // The register echoes the sync type applyForSyncType() wrote, so reading it
    // asks the chip what it was told -- and during a probe the two disagree.
    Wire.reset();
    Wire.poison(Poison);
    SyncMeasurement::set(false);

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

TEST_CASE("a sync separator that is not in the sync path is left alone")
{
    seedSlicer(1, 0xFF);
    SyncMeasurement::set(false);
    SyncOnGreen::choose(7);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(g_inForce == 0);
    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
}

TEST_CASE("a sync separator already producing clean edges keeps the level chosen")
{
    // The whole cost of the walk is paid per step, so a source that is already
    // good must not be walked off a level that works.
    seedSlicer(1, 0x05);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(SyncOnGreen::level() == 11);
}

TEST_CASE("a sync separator that never comes good walks to the floor and resets")
{
    // HSACT holds but the sync separator's own output stays dead, which is the state
    // the ratchet exists for. Reaching the floor without finding a level puts
    // the default back rather than leaving the sync separator wide open.
    seedSlicer(1, 0x00);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(13);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
    CHECK(g_inForce > 1);
}

TEST_CASE("the level reaches the sync separator through the injected action")
{
    // Writing ADC_SOGCTRL here instead would skip the phase and ADC PLL
    // latches that putting a level in force carries, and a divider written
    // without its latch leaves the PLL on the old value.
    seedSlicer(1, 0x05);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(9);

    SyncOnGreen::acquire(testClock, putInForce);

    CHECK(g_inForce >= 1);
    CHECK(g_lastInForce == 9);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 9);
}

// The coarse pass: two at a time, no settling runs. The sketch reaches for it
// while sync has only just gone, where acquire()'s windows would cost more than
// the attempt is worth.
TEST_CASE("the coarse pass leaves a sync separator that is already reporting clean edges")
{
    seedSlicer(1, 0x05);            // both bits the coarse test asks for
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(SyncOnGreen::level() == 11);
    CHECK(g_inForce == 0);
}

TEST_CASE("the coarse pass steps down by two and resets at the floor")
{
    // 13 -> 11 -> 9 -> 7 -> 5 -> 3, then below 4 it puts the default back
    // rather than leaving the sync separator near wide open.
    seedSlicer(1, 0x00);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(13);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(SyncOnGreen::level() == SyncOnGreen::DefaultLevel);
    CHECK(g_inForce > 1);
}

TEST_CASE("the coarse pass leaves a sync separator out of the sync path alone")
{
    seedSlicer(1, 0x00);
    SyncMeasurement::set(false);
    SyncOnGreen::choose(7);

    SyncOnGreen::acquireCoarse(putInForce);

    CHECK(g_inForce == 0);
    CHECK(SyncOnGreen::level() == 7);   // left where it was, not reset
}

// Lifting the level off the floor, the first thing tried when sync has only
// just gone. The walk leaves the level at the floor when nothing it tried
// worked, and a separator that far open slices noise as sync.

TEST_CASE("a level at the floor is lifted one step")
{
    seedSlicer(1, 0x05);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(1);

    SyncOnGreen::liftOffFloor(putInForce);

    CHECK(SyncOnGreen::level() == 2);
    CHECK(g_inForce == 1);
}

TEST_CASE("a level with room to step is left where it is")
{
    seedSlicer(1, 0x05);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(2);

    SyncOnGreen::liftOffFloor(putInForce);

    CHECK(SyncOnGreen::level() == 2);
    CHECK(g_inForce == 0);
}

TEST_CASE("a sync separator out of the sync path is not lifted")
{
    seedSlicer(1, 0x05);
    SyncMeasurement::set(false);
    SyncOnGreen::choose(1);

    SyncOnGreen::liftOffFloor(putInForce);

    CHECK(SyncOnGreen::level() == 1);
    CHECK(g_inForce == 0);
}

// Re-acquiring the level, the escalation ladder's rung. Judged on whether the
// sync separator's own output moves at all: a measured line length that never
// changes across a run of reads is a separator slicing nothing, and no walk can
// find a threshold from evidence that is not there.

static unsigned g_walks = 0;
static void countWalk() { ++g_walks; }

static void seedLineLength(uint8_t hsActive, bool moving, bool pllInReset)
{
    seedSlicer(hsActive, 0x05);
    Wire.bank[0][0x19] = 0x40;                      // STATUS_SYNC_PROC_HLOW_LEN
    Wire.bank[0][0x1A] = 0x00;
    if (moving)
        Wire.drift(0, 0x19);
    Wire.bank[5][0x11] = pllInReset ? 0x01 : 0x00;  // PLLAD_VCORST
    g_walks = 0;
}

TEST_CASE("a sync separator out of the sync path is not re-acquired")
{
    seedLineLength(1, true, false);
    SyncMeasurement::set(false);
    SyncOnGreen::choose(7);

    SyncOnGreen::reacquire(countWalk, putInForce, false);

    CHECK(g_walks == 0);
    CHECK(SyncOnGreen::level() == 7);
}

TEST_CASE("a sync separator whose output moves is handed to the walk")
{
    seedLineLength(1, true, false);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::reacquire(countWalk, putInForce, false);

    CHECK(g_walks == 1);
    CHECK(SyncOnGreen::level() == 11);
}

TEST_CASE("a sync separator whose output is frozen is parked rather than walked")
{
    seedLineLength(1, false, false);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::reacquire(countWalk, putInForce, false);

    CHECK(g_walks == 0);
    CHECK(SyncOnGreen::level() == SyncOnGreen::FrozenLevel);
}

TEST_CASE("an ADC PLL held in reset is not evidence the output is frozen")
{
    // Nothing the separator reports means anything while the PLL that clocks
    // the measurement is in reset, so a reading that does not move there says
    // nothing about the level.
    seedLineLength(1, false, true);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::reacquire(countWalk, putInForce, false);

    CHECK(g_walks == 1);
    CHECK(SyncOnGreen::level() == 11);
}

TEST_CASE("re-opening the sync separator takes the walk's place, not its result")
{
    seedLineLength(1, true, false);
    SyncMeasurement::set(true);
    SyncOnGreen::choose(11);

    SyncOnGreen::reacquire(countWalk, putInForce, true);

    CHECK(g_walks == 0);
    CHECK(SyncOnGreen::level() == 0);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 0);
}

// The tuning pass: run while a source is acquired, it steps the level down
// ahead of a sync loss rather than waiting for one. Its window and its
// bad-sample count are held across passes.
static uint32_t g_now = 0;
static uint32_t fixedClock() { return g_now; }

// The walk from a chosen starting level, which the caller supplies: on a path
// where the level may not be walked at all it parks the default instead.
static unsigned g_escalations = 0;
static void escalate()
{
    ++g_escalations;
    SyncOnGreen::choose(SyncOnGreen::DefaultLevel);
    SyncOnGreen::acquire(testClock, putInForce);
}

static void seedTuning(uint8_t level, bool sogBad)
{
    Wire.reset();
    Wire.poison(Poison);
    Wire.bank[0][0x0F] = sogBad ? 0x01 : 0x00;   // STATUS_INT_SOG_BAD, bit 0
    Wire.bank[0][0x16] = 0x02;                   // HSACT, bit 1, held
    Wire.bank[0][0x19] = 0x10;                   // HLOW_LEN, 12 bits over 0x19..0x1A
    Wire.bank[0][0x1A] = 0x00;
    g_now = 0;
    g_inForce = 0;
    g_escalations = 0;
    g_reads = 0;
    SyncMeasurement::set(true);
    SyncOnGreen::choose(level);
    SyncOnGreen::forgetWindow(g_now);
}

TEST_CASE("one window of bad samples is not enough to move the level")
{
    // A pass counts at most sixteen, and the level only moves once a window has
    // seen more than that -- so a single burst does not walk a working source
    // off a level that holds.
    seedTuning(11, true);

    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 11);
}

TEST_CASE("bad samples past the threshold step the level down by one")
{
    seedTuning(11, true);

    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 10);
}

TEST_CASE("a step reports that the level moved")
{
    // The sync processor's dynamic registers are refreshed after a step, and
    // that refresh is the sketch's until step 5 of the retirement.
    seedTuning(11, true);

    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);
    SyncOnGreen::Tuning outcome =
        SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(outcome.levelMoved);
}

TEST_CASE("a step puts the new level in force through the injected action")
{
    seedTuning(11, true);

    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(g_lastInForce == 10);
    CHECK(SyncOnGreen::ADC_SOGCTRL::read() == 10);
}

TEST_CASE("a clean source is left where it is")
{
    // No bad-hsync interrupt and HSACT held: there is no evidence to act on,
    // and the level a source works at must survive a pass that finds nothing.
    seedTuning(11, false);

    SyncOnGreen::tune(false, true, fixedClock, putInForce, escalate);
    SyncOnGreen::tune(false, true, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 11);
    CHECK(g_inForce == 0);
}

TEST_CASE("a sync separator out of the sync path is left alone")
{
    // SP_SOG_MODE follows the sync type. On a separate-sync source the level
    // is inert, and walking it moves a control nothing is reading.
    seedTuning(11, true);
    SyncMeasurement::set(false);

    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 11);
    CHECK(g_inForce == 0);
}

TEST_CASE("a source reporting bad hsync is reported unsettled")
{
    // The frame time lock walks away from a rate measured across a sync
    // disturbance, so it is told to hold off. That stamp is the sketch's until
    // step 11.
    seedTuning(11, true);

    SyncOnGreen::Tuning outcome =
        SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(outcome.sourceUnsettled);
}

TEST_CASE("a level too low to step hands the walk to the caller")
{
    // Below two there is nowhere left to step. The walk is not run from here:
    // whether the level may be walked at all depends on the path the source is
    // on, and walking it during a detection sweep pins it at the floor.
    seedTuning(1, true);

    for (int pass = 0; pass < 4; ++pass)
        SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(g_escalations == 1);
}

TEST_CASE("a window that closes after a step takes one more off the level")
{
    // The step inside the window was evidence the level was too high; closing
    // the window without further trouble takes the margin the source turned
    // out to need. Only from eight up -- below that the walk owns it.
    seedTuning(11, true);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);   // steps to 10

    Wire.bank[0][0x0F] = 0x00;                                 // trouble stops
    g_now += 4000;                                             // the window closes
    SyncOnGreen::Tuning outcome =
        SyncOnGreen::tune(false, true, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 9);
    CHECK(outcome.phaseStale);
}

TEST_CASE("a window that closes with no step leaves the level alone")
{
    seedTuning(11, false);

    g_now += 4000;
    SyncOnGreen::tune(false, true, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 11);
    CHECK(g_inForce == 0);
}

TEST_CASE("forgetting the window discards the samples counted in it")
{
    // A mode change makes the evidence stale: it was gathered against the
    // timing the source has just left.
    seedTuning(11, true);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    SyncOnGreen::forgetWindow(g_now);
    SyncOnGreen::tune(false, false, fixedClock, putInForce, escalate);

    CHECK(SyncOnGreen::level() == 11);
}
