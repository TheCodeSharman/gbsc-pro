// Host-compiled unit tests for src/tv5725/Interrupts.h -- `make -C test interrupts`.
//
// The chip latches "the source disturbed" in s0_0F. The bits do NOT persist:
// cleared, the source mode changed, nothing polled, they read 0x00 again 25 s
// later in both directions -- so whoever wants them has to read at loop rate,
// and reading them is the same act as claiming them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Interrupts.h"

using namespace Tv5725;

static const uint8_t IntStatus = 0x0F;   // s0_0F, INT_STATUS_
static const uint8_t IntReset = 0x58;    // s0_58, the acknowledge pulses

static void seedStatus(uint8_t bits)
{
    Wire.reset();
    Wire.bank[0][IntStatus] = bits;
}

TEST_CASE("a quiet source has not disturbed")
{
    seedStatus(0x00);

    CHECK_FALSE(Interrupts::takeSourceDisturbed());
}

TEST_CASE("SOG switch means the source disturbed; SOG bad belongs to someone else")
{
    // Measured across a source mode change, sampled from loop() at 30 ms:
    // bits 0 and 1 fire together at ~0.9-1.1 s, in BOTH directions, so bit 1
    // alone is signal enough. Bit 3, the one the datasheet calls "input source
    // switch the mode", fired on one direction only and 3.4 s in.
    //
    // **Bit 0 is NOT read here, and that is the point.** Reading a latched bit
    // claims it, and bit 0 already has an owner on every sync path: the sketch
    // acknowledges it unconditionally every 900 ms and counts consecutive sets
    // to decide a separate-sync source should switch to csync. A second reader
    // wins that race at loop rate and the counter never reaches its threshold,
    // so the recovery disappears with nothing to show for it.
    SUBCASE("bit 1 alone") {
        seedStatus(0x02);
        CHECK(Interrupts::takeSourceDisturbed());
    }

    SUBCASE("both, as the bench sees them") {
        seedStatus(0x03);
        CHECK(Interrupts::takeSourceDisturbed());
    }

    SUBCASE("bit 0 alone belongs to the sync-type heuristic") {
        seedStatus(0x01);
        CHECK_FALSE(Interrupts::takeSourceDisturbed());
        CHECK_FALSE(Wire.touched[0][IntReset]);
    }

    SUBCASE("a bit that means something else does not") {
        seedStatus(0x80);   // STATUS_INT_INP_CSYNC
        CHECK_FALSE(Interrupts::takeSourceDisturbed());
    }
}

TEST_CASE("acknowledging SOG switch leaves SOG bad's acknowledge alone")
{
    // Both resets share s0_58, so a byte-wide write would carry bit 0 with it
    // and acknowledge a condition this has no business claiming.
    seedStatus(0x02);
    Wire.bank[0][IntReset] = 0x01;

    REQUIRE(Interrupts::takeSourceDisturbed());

    CHECK((Wire.bank[0][IntReset] & 0x01) == 0x01);
}

TEST_CASE("taking the disturbance acknowledges it")
{
    // Latched, so an unacknowledged bit reads set forever and every later poll
    // reports a disturbance that already happened.
    seedStatus(0x03);

    REQUIRE(Interrupts::takeSourceDisturbed());

    // The acknowledge is a PULSE -- 1 then 0 -- so what is left behind is 0 and
    // the evidence it ran is that the register was written at all.
    CHECK(Wire.touched[0][IntReset]);
    CHECK(Wire.bank[0][IntReset] == 0x00);
}

TEST_CASE("nothing is acknowledged when nothing fired")
{
    // The acknowledge lands mid-pulse on a condition that arrives between the
    // read and the write, so it is not free to issue speculatively.
    seedStatus(0x00);

    REQUIRE_FALSE(Interrupts::takeSourceDisturbed());

    CHECK_FALSE(Wire.touched[0][IntReset]);
}
