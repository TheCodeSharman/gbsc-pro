// Host-compiled unit tests for src/tv5725/MemoryBus.cpp -- `make -C test memory-bus`.
//
// No seam had to be invented: Tv5725.h is header-only C++ whose only Arduino
// dependency is Wire, and test/fake/Wire.h already models the segmented slave
// for test_segment_select.cpp, so compiling MemoryBus.cpp against it is the whole
// trick and the firmware needs no #ifdef.
//
// This has to exist before the preset tables can go. While a table still loads,
// a field the bring-up FORGETS is silently supplied by the blob and the picture
// looks fine -- invisible on hardware until the tables are deleted, at which
// point it surfaces across nine subsystems at once. Here it is a failing
// assertion in a millisecond: poison every bank, run init(), ask what was
// touched.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/MemoryBus.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SdramTimings.h"

using Tv5725::MemoryBus;
using Tv5725::SdramTimings;

// Neither a preset table's value nor the firmware's, for any field here.
static const uint8_t Poison = 0xA5;

// Poisoned, then brought up. Every test starts from the same place.
struct FreshChip {
    FreshChip()
    {
        Wire.reset();
        Wire.poison(Poison);
        MemoryBus::init();
    }
};

TEST_CASE("the bus comes up at the fastest clock the part is rated for")
{
    FreshChip chip;

    // 162 MHz -- but not a number written down twice. SdramTimings derives it
    // from the EM638325TS-6's tCK, tRCD and tRP and test_sdram_timing.cpp pins
    // that; this only checks init() programmed what it chose.
    CHECK(Wire.field(0, 0x40, 4, 3) == SdramTimings::fastestInSpec());
    CHECK(Wire.field(0, 0x40, 4, 3) == 3);
}

TEST_CASE("the cycle counts programmed are the ones that clock requires")
{
    FreshChip chip;

    const uint8_t clock = SdramTimings::fastestInSpec();
    CHECK(Wire.field(4, 0x05, 0, 2) == SdramTimings::actCycleRegister(clock));
    CHECK(Wire.field(4, 0x05, 4, 2) == SdramTimings::pchgCycleRegister(clock));
}

TEST_CASE("CAS latency 3 is programmed, which is what makes 162MHz legal")
{
    FreshChip chip;

    // MEM_MODE_REG [6:4] is the latency driven during the Load Mode Register
    // cycle. The part's 6 ns tCK is its CL3 figure, so this byte and the clock
    // are one decision: a test checking the clock alone would pass while the
    // bus ran out of spec.
    CHECK(Wire.field(4, 0x01, 4, 3) == 3);
    CHECK(Wire.bank[4][0x01] == 0x30);
}

TEST_CASE("the board's nanosecond trim is written, not left to a preset")
{
    FreshChip chip;

    CHECK(Wire.field(4, 0x04, 0, 3) == 2);  // MEM_FK_RD_DLY
    CHECK(Wire.field(4, 0x18, 0, 3) == 0);  // MEM_DATA_DLY_REG, 0.00 ns
    CHECK(Wire.field(4, 0x1B, 4, 3) == 4);  // MEM_CLK_DLY_REG,  1.00 ns
}

TEST_CASE("a neighbour sharing a byte survives the field beside it")
{
    FreshChip chip;

    // MEM_FK_RD_DLY is [2:0] of s4_04 and MEM_RD_LAT_PIP is [6:4] of it, and
    // MEM_CLK_DLY_REG and MEM_ADR_DLY_REG share s4_1b the same way, so a
    // whole-byte write carrying one field's value clears its neighbour.
    CHECK(Wire.field(4, 0x04, 4, 3) == 3);  // MEM_RD_LAT_PIP
    CHECK(Wire.field(4, 0x1B, 0, 3) == 1);  // MEM_ADR_DLY_REG, 0.25 ns
}

TEST_CASE("every register the subsystem owns is actually written")
{
    FreshChip chip;

    // Asked of the fake, not inferred from the value: a field whose owned value
    // happened to equal the poison would read correct having never been touched.
    // Reading the right value and concluding the code ran is the mistake.
    for (uint8_t reg = 0x01; reg <= 0x1D; ++reg) {
        if (reg == 0x02) continue;  // high byte of MEM_MODE_REG, written as a pair
        CAPTURE(reg);
        CHECK(Wire.touched[4][reg]);
    }
    CHECK(Wire.touched[0][0x40]);  // PLL_MS, which lives in segment 0
}

TEST_CASE("the bus is aimed at the right bank for every write")
{
    FreshChip chip;

    // PLL_MS is segment 0 and the SDRAM controller segment 4, so this subsystem
    // crosses a bank boundary -- which is why the fake models the pointer rather
    // than the fields. A write landing with the pointer misaimed shows up as a
    // touched byte in a bank this subsystem has no business in.
    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s) {
        if (s == 0 || s == 4) continue;
        for (int r = 0; r < 256; ++r) {
            CAPTURE(s);
            CAPTURE(r);
            REQUIRE_FALSE(Wire.touched[s][r]);
        }
    }
}

TEST_CASE("segment 0 gets the clock and nothing else")
{
    FreshChip chip;

    // The one byte outside segment 4. Anything else touched there would be a
    // field this class writes without saying so in its header.
    for (int r = 0; r < 256; ++r) {
        if (r == 0x40) continue;
        CAPTURE(r);
        REQUIRE_FALSE(Wire.touched[0][r]);
    }
}
