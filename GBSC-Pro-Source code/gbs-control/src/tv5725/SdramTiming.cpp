#include "SdramTiming.h"

namespace Tv5725 {

const uint32_t SdramTiming::TckMinPs;
const uint32_t SdramTiming::TrcdMinPs;
const uint32_t SdramTiming::TrpMinPs;
const uint8_t SdramTiming::FbclkCode;
const uint8_t SdramTiming::MaxCycles;
const uint8_t SdramTiming::NotEncodable;

namespace {

// RD-5725-1.1, PLL648 CONTROL 00, MS[2:0]. Index is the register value.
// FbclkCode is 0 because it names pin 110, not a frequency.
const uint32_t ClockKHz[8] = {
    108000,  // 000
    81000,   // 001
    0,       // 010  FBCLK (pin 110)
    162000,  // 011
    144000,  // 100
    185143,  // 101  648/3.5
    216000,  // 110
    129600,  // 111
};

// One clock period in picoseconds. 1e12 ps/s over (kHz * 1e3) Hz.
uint32_t periodPs(uint32_t kHz)
{
    return kHz ? (uint32_t)(1000000000UL / kHz) : 0;
}

}  // namespace

uint32_t SdramTiming::clockKHz(uint8_t pllMs)
{
    return pllMs < 8 ? ClockKHz[pllMs] : 0;
}

uint8_t SdramTiming::cyclesFor(uint8_t registerValue)
{
    return (uint8_t)((registerValue & 0x03) + 2);
}

uint8_t SdramTiming::registerForCycles(uint8_t cycles)
{
    if (cycles < 2 || cycles > MaxCycles) {
        return NotEncodable;
    }
    return (uint8_t)(cycles - 2);
}

uint8_t SdramTiming::cyclesNeeded(uint8_t pllMs, uint32_t minPs)
{
    const uint32_t period = periodPs(clockKHz(pllMs));
    if (period == 0) {
        return 0;
    }
    // Whole clocks, rounded up: a partial clock does not cover a minimum.
    const uint32_t whole = (minPs + period - 1) / period;
    // Two clocks is the narrowest the register encodes, and asking for fewer
    // would report an impossible configuration as a comfortable one.
    return (uint8_t)(whole < 2 ? 2 : whole);
}

uint8_t SdramTiming::actCycleRegister(uint8_t pllMs)
{
    return registerForCycles(cyclesNeeded(pllMs, TrcdMinPs));
}

uint8_t SdramTiming::pchgCycleRegister(uint8_t pllMs)
{
    return registerForCycles(cyclesNeeded(pllMs, TrpMinPs));
}

bool SdramTiming::inSpec(uint8_t pllMs, uint8_t actCycleReg, uint8_t pchgCycleReg)
{
    const uint32_t period = periodPs(clockKHz(pllMs));
    if (period == 0) {
        return false;  // FBCLK, or a code outside the register -- uncheckable.
    }
    if (period < TckMinPs) {
        return false;
    }
    if ((uint32_t)cyclesFor(actCycleReg) * period < TrcdMinPs) {
        return false;
    }
    return (uint32_t)cyclesFor(pchgCycleReg) * period >= TrpMinPs;
}

uint8_t SdramTiming::fastestInSpec()
{
    uint8_t best = 1;  // 81 MHz, the slowest the divider offers
    uint32_t bestKHz = 0;
    for (uint8_t code = 0; code < 8; ++code) {
        const uint8_t act = actCycleRegister(code);
        const uint8_t pchg = pchgCycleRegister(code);
        if (act == NotEncodable || pchg == NotEncodable) {
            continue;  // FBCLK lands here too: no frequency, nothing to derive.
        }
        if (!inSpec(code, act, pchg)) {
            continue;
        }
        const uint32_t kHz = clockKHz(code);
        if (kHz > bestKHz) {
            bestKHz = kHz;
            best = code;
        }
    }
    return best;
}

}  // namespace Tv5725
