// Host-compiled unit tests for the ADC PLL -- `make -C test adc`.
//
// PLLAD_LAT loads MD, ND, KS, CKOS and ICP together on a rising edge, so a
// divider written after that edge leaves the PLL on the old value with every
// register reading back correct. Only the ORDER of the writes can show it,
// which is what the fake bus's trace is for.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"

using namespace Tv5725;

// Where in the trace a field was last written, and where the latch's rising
// edge fell. Asked by NAME through the register typedefs, which carry their own
// segment and byte offset.
template <typename Reg>
static int lastWriteOf()
{
    int at = -1;
    for (size_t i = 0; i < Wire.trace.size(); ++i)
        if (Wire.trace[i].segment == 5
            && Wire.trace[i].reg >= Reg::byteOffset
            && Wire.trace[i].reg < Reg::byteOffset + (Reg::bitWidth + 7) / 8)
            at = static_cast<int>(i);
    return at;
}

static int latchRisingEdge()
{
    const uint8_t mask = static_cast<uint8_t>(1u << Adc::PLLAD_LAT::bitOffset);
    bool low = false;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        if (Wire.trace[i].segment != 5
            || Wire.trace[i].reg != Adc::PLLAD_LAT::byteOffset)
            continue;
        if (!(Wire.trace[i].value & mask))
            low = true;
        else if (low)
            return static_cast<int>(i);
    }
    return -1;
}

TEST_CASE("the divider reaches the PLL before the latch loads it")
{
    Wire.reset();

    Adc::applySampleRate(2250);

    const int divider = lastWriteOf<Adc::PLLAD_MD>();
    const int edge = latchRisingEdge();

    REQUIRE(divider >= 0);
    REQUIRE(edge >= 0);
    CHECK(divider < edge);
    CHECK(Wire.field(5, Adc::PLLAD_MD::byteOffset, Adc::PLLAD_MD::bitOffset,
                     Adc::PLLAD_MD::bitWidth) == 2250);
}

TEST_CASE("the latch is a pulse, not a level")
{
    // A rising edge only exists if the bit was driven low first. Writing 1 over
    // a 1 loads nothing, which is a silent no-op on a chip that reads correct.
    Wire.reset();
    Wire.bank[5][Adc::PLLAD_LAT::byteOffset] = 0xFF;

    Adc::latch();

    CHECK(latchRisingEdge() >= 0);
}
