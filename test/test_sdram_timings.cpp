// Host-compiled unit tests for src/tv5725/SdramTimings.h -- `make -C test sdram-timing`.
//
// How fast the SDRAM bus may run, decided against the memory part's datasheet
// rather than against whichever preset table loaded last -- the twelve split
// PLL_MS six/six between 129.6 MHz and the FBCLK pin, following nothing.
//
// The part is an EM638325TS-6: 6 ns clock cycle at CAS latency 3 (166 MHz), tRCD
// and tRP 18 ns each. docs/EM638325-Industrial_Rev-3.2.pdf,
// docs/tv5725-chip.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SdramTimings.h"

using Tv5725::SdramTimings;

// What all twelve preset tables program, and so what the chip is running when
// apply() asks: MEM_ACT_CYCLE = MEM_PCHG_CYCLE = 1, meaning three clocks each.
static const uint8_t ThreeClocks = 1;

TEST_CASE("a cycle-count register value is two clocks more than it reads")
{
    // RD-5725-1.1: 00 -> 2, 01 -> 3, 10 -> 4, 11 -> 5. Reading this as the
    // clock count directly would put tRCD a whole clock short.
    CHECK(SdramTimings::cyclesFor(0) == 2);
    CHECK(SdramTimings::cyclesFor(ThreeClocks) == 3);
    CHECK(SdramTimings::cyclesFor(2) == 4);
    CHECK(SdramTimings::cyclesFor(3) == 5);
}

TEST_CASE("the divider's codes are the frequencies the datasheet lists")
{
    CHECK(SdramTimings::clockKHz(0) == 108000u);
    CHECK(SdramTimings::clockKHz(1) == 81000u);
    CHECK(SdramTimings::clockKHz(3) == 162000u);
    CHECK(SdramTimings::clockKHz(4) == 144000u);
    CHECK(SdramTimings::clockKHz(5) == 185143u);
    CHECK(SdramTimings::clockKHz(6) == 216000u);
    CHECK(SdramTimings::clockKHz(7) == 129600u);
}

TEST_CASE("FBCLK names a pin, not a frequency, so it can never be derived")
{
    // PLL_MS = 010 takes the memory clock from pin 110. It measured clean on
    // the bench, so it works -- but its frequency is a board fact this repo
    // cannot read, and a value that cannot be checked against the part is not
    // one to choose automatically.
    CHECK(SdramTimings::clockKHz(SdramTimings::FbclkCode) == 0u);
    CHECK_FALSE(SdramTimings::inSpec(SdramTimings::FbclkCode, ThreeClocks, ThreeClocks));
    CHECK(SdramTimings::fastestInSpec() != SdramTimings::FbclkCode);
    // And there is nothing to program alongside it either.
    CHECK(SdramTimings::actCycleRegister(SdramTimings::FbclkCode)
          == SdramTimings::NotEncodable);
}

TEST_CASE("162MHz is in spec and 185MHz is not, which is the whole decision")
{
    // 162 MHz: 6.17 ns against tCK3's 6 ns, and 3 x 6.17 = 18.5 ns against
    // tRCD/tRP's 18 ns. In spec on all three, with 2.9% to spare.
    CHECK(SdramTimings::inSpec(3, ThreeClocks, ThreeClocks));

    // 185 MHz: 5.40 ns, under tCK3 -- and 3 x 5.40 = 16.2 ns, under tRCD/tRP
    // as well. It fails twice over, which is why the speed bin alone was not
    // the argument.
    CHECK_FALSE(SdramTimings::inSpec(5, ThreeClocks, ThreeClocks));
    CHECK_FALSE(SdramTimings::inSpec(6, ThreeClocks, ThreeClocks));
}

TEST_CASE("everything at or below 162MHz is in spec, including what shipped")
{
    CHECK(SdramTimings::inSpec(1, ThreeClocks, ThreeClocks));  // 81
    CHECK(SdramTimings::inSpec(0, ThreeClocks, ThreeClocks));  // 108
    CHECK(SdramTimings::inSpec(7, ThreeClocks, ThreeClocks));  // 129.6, was shipping
    CHECK(SdramTimings::inSpec(4, ThreeClocks, ThreeClocks));  // 144
}

TEST_CASE("the fastest clock the part allows is 162MHz")
{
    CHECK(SdramTimings::fastestInSpec() == 3);
}

TEST_CASE("the cycle counts are computed from the clock, not inherited")
{
    // This is what MemoryBus programs alongside the clock. Three each at
    // 162 MHz: 18000 ps over a 6172 ps period is 2.92, and a partial clock
    // does not cover a minimum, so it rounds up.
    CHECK(SdramTimings::cyclesNeeded(3, SdramTimings::TrcdMinPs) == 3);
    CHECK(SdramTimings::cyclesNeeded(3, SdramTimings::TrpMinPs) == 3);
    CHECK(SdramTimings::actCycleRegister(3) == ThreeClocks);
    CHECK(SdramTimings::pchgCycleRegister(3) == ThreeClocks);

    // The same 18 ns costs fewer clocks as the bus slows, which is the whole
    // reason these two registers cannot be a constant: at 81 MHz one period is
    // 12.35 ns and two clocks already cover tRCD.
    CHECK(SdramTimings::cyclesNeeded(1, SdramTimings::TrcdMinPs) == 2);
    CHECK(SdramTimings::cyclesNeeded(7, SdramTimings::TrcdMinPs) == 3);  // 129.6
}

TEST_CASE("a count below the register's floor is reported as two clocks")
{
    // MEM_ACT_CYCLE cannot ask for one clock -- 00 means two -- so a bus slow
    // enough to need only one must still be told two. Rounding down to a value
    // the register cannot express would program 00 and mean something else.
    CHECK(SdramTimings::cyclesNeeded(1, 1000) == 2);
    CHECK(SdramTimings::registerForCycles(2) == 0);
    CHECK(SdramTimings::registerForCycles(5) == 3);
}

TEST_CASE("cycle counts the two bits cannot hold are refused, not truncated")
{
    // Five clocks is the ceiling. Truncating a sixth to fit would silently
    // program a bus that violates tRCD, which is the failure mode with no
    // symptom until the picture is wrong.
    CHECK(SdramTimings::registerForCycles(6) == SdramTimings::NotEncodable);
    CHECK(SdramTimings::registerForCycles(1) == SdramTimings::NotEncodable);
}

TEST_CASE("the round trip through the register encoding is lossless")
{
    for (uint8_t reg = 0; reg < 4; ++reg) {
        CHECK(SdramTimings::registerForCycles(SdramTimings::cyclesFor(reg)) == reg);
    }
}

TEST_CASE("the precharge count is checked, not just the activate count")
{
    // tRP and tRCD are separate parameters against separate registers. Checking
    // one and assuming the other is how a bus ends up violating half its
    // timing while every test passes.
    CHECK_FALSE(SdramTimings::inSpec(3, ThreeClocks, 0));
    CHECK_FALSE(SdramTimings::inSpec(3, 0, ThreeClocks));
}
