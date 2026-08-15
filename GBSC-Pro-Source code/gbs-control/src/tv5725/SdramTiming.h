#ifndef TV5725_SDRAM_TIMING_H
#define TV5725_SDRAM_TIMING_H

#include <stdint.h>

namespace Tv5725 {

// What the memory part will stand, and therefore how fast the bus may run.
//
// The board fits one EM638325TS-6 (docs/tv5725-chip.md, schematic SDRAM sheet),
// and the "-6" is a speed bin, not a part revision: 6 ns clock cycle at CAS
// latency 3, which is 166 MHz. docs/EM638325-Industrial_Rev-3.2.pdf, Table 1
// and Table 2. Etron publish the industrial revision; the board's commercial
// -6/-6G shares the bin and differs only in temperature range.
//
// The clock is derived rather than chosen: the fastest code that stays in spec,
// which is 162 MHz.
//
//     code  clock       tCK       tRCD/tRP at 3 clocks   verdict
//     001    81   MHz   12.35 ns  37.0 ns                in spec
//     000   108   MHz    9.26 ns  27.8 ns                in spec
//     111   129.6 MHz    7.72 ns  23.1 ns                in spec (was shipping)
//     100   144   MHz    6.94 ns  20.8 ns                in spec
//     011   162   MHz    6.17 ns  18.5 ns                in spec, 2.9% margin
//     101   185.1 MHz    5.40 ns  16.2 ns                BREAKS tCK and tRCD/tRP
//     110   216   MHz    4.63 ns  13.9 ns                BREAKS tCK and tRCD/tRP
//
// tRCD and tRP are why 185 and 216 are not merely over the speed bin:
// MEM_ACT_CYCLE and MEM_PCHG_CYCLE give three memory clocks each, and three
// clocks stops covering the part's 18 ns between 162 and 185 MHz. A faster clock
// would need those widened first.
//
// PLL_MS = 010 is never chosen: it takes the clock from the FBCLK pin, whose
// frequency is a board fact this repo cannot read. It measures clean, but a
// value that cannot be checked against a datasheet is not one to derive.
//
// 129.6 MHz, FBCLK and 162 MHz all give a clean picture under the heaviest
// playback load the unit has.
class SdramTiming {
public:
    // EM638325TS-6 AC parameters, in picoseconds. Table 1 and the AC
    // characteristics table, "-6I" column.
    static const uint32_t TckMinPs = 6000;    // clock cycle time, CAS latency 3
    static const uint32_t TrcdMinPs = 18000;  // RAS# to CAS# delay, same bank
    static const uint32_t TrpMinPs = 18000;   // precharge to activate, same bank

    // PLL648 divides a 648 MHz VCO. Kilohertz keeps 129.6 and 185.1 exact.
    // RD-5725-1.1, PLL648 CONTROL 00, MS[2:0].
    static const uint8_t FbclkCode = 2;  // clock from pin 110 -- frequency unknown

    // MEM_ACT_CYCLE / MEM_PCHG_CYCLE hold two bits and buy two to five memory
    // clocks. RD-5725-1.1: 00 -> 2, 01 -> 3, 10 -> 4, 11 -> 5. Five is a
    // ceiling, not a formality -- it is what stops a fast enough clock from
    // being made to work by widening alone.
    static const uint8_t MaxCycles = 5;
    static const uint8_t NotEncodable = 0xFF;

    // The clock a PLL_MS code selects, in kHz. Zero for FbclkCode, which names
    // a pin rather than a frequency.
    static uint32_t clockKHz(uint8_t pllMs);

    // Memory clocks a MEM_ACT_CYCLE / MEM_PCHG_CYCLE register value buys.
    static uint8_t cyclesFor(uint8_t registerValue);

    // And back: the register value buying this many clocks, or NotEncodable.
    static uint8_t registerForCycles(uint8_t cycles);

    // Whole clocks needed to cover a datasheet minimum at this PLL_MS code.
    // Zero when the clock is unknown, which only FbclkCode is.
    static uint8_t cyclesNeeded(uint8_t pllMs, uint32_t minPs);

    // The MEM_ACT_CYCLE / MEM_PCHG_CYCLE values this clock requires, or
    // NotEncodable if two bits cannot hold them. **Derived, not chosen**: the
    // registers exist to cover tRCD and tRP, so the clock decides them, and
    // owning the clock without owning these would be owning half a decision.
    static uint8_t actCycleRegister(uint8_t pllMs);
    static uint8_t pchgCycleRegister(uint8_t pllMs);

    // Whether a configuration holds -- tCK for the part, and enough clocks for
    // tRCD and tRP given the cycle counts actually programmed.
    static bool inSpec(uint8_t pllMs, uint8_t actCycleReg, uint8_t pchgCycleReg);

    // The fastest PLL_MS code that can be made to hold: tCK covered, and both
    // cycle counts encodable. Paired with actCycleRegister/pchgCycleRegister,
    // which say what to program alongside it.
    static uint8_t fastestInSpec();
};

}  // namespace Tv5725

#endif
