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

// --- what the latch loads, calculated rather than inherited -------------------

TEST_CASE("the post divider comes from the datasheet's crossover table")
{
    // RD-5725-1.1, PLLAD_KS: 00 divides by 1 over 162..80 MHz, 01 by 2 over
    // 80..40, 10 by 4 over 40..20, 11 by 8 below that. The table is read
    // against CKO -- the clock the divider alone produces -- not against the
    // oversampled rate the ADC then runs at.
    CHECK(Adc::postDividerFor(120000000u) == 0);
    CHECK(Adc::postDividerFor(80000000u) == 0);
    CHECK(Adc::postDividerFor(79999999u) == 1);
    CHECK(Adc::postDividerFor(40000000u) == 1);
    CHECK(Adc::postDividerFor(39999999u) == 2);
    CHECK(Adc::postDividerFor(20000000u) == 2);
    CHECK(Adc::postDividerFor(19999999u) == 3);

    // The bench RiscPC: 2250 samples on a 15574 Hz line is 35.0 MHz, which is
    // the 40..20 MHz row. Measured on the unit as PLLAD_KS 2.
    CHECK(Adc::postDividerFor(2250u * 15574u) == 2);
}

TEST_CASE("an oversampling the post divider cannot carry is reduced, not written anyway")
{
    // Each doubling takes an output tap one step faster than the post divider,
    // and there is no tap above the top. Asking for more than the clock can
    // give must come back as what it can, or the decimators are wired for a
    // rate the PLL is not producing.
    CHECK(Adc::oversampleFor(2, 4) == 4);
    CHECK(Adc::oversampleFor(1, 4) == 2);
    CHECK(Adc::oversampleFor(0, 4) == 1);
    CHECK(Adc::oversampleFor(0, 2) == 1);
    CHECK(Adc::oversampleFor(3, 4) == 4);
}

TEST_CASE("the bench source lands on the group measured on the hardware")
{
    // Every value here was read off the unit in the state that produces a
    // correct picture, and each one is a bit the latch loads or a decimator
    // that follows it.
    Wire.reset();

    CHECK(Adc::applySampleRate(2250, 15574, 4) == 4);

    CHECK(Wire.field(5, Adc::PLLAD_MD::byteOffset, Adc::PLLAD_MD::bitOffset,
                     Adc::PLLAD_MD::bitWidth) == 2250);
    CHECK(Wire.field(5, Adc::PLLAD_KS::byteOffset, Adc::PLLAD_KS::bitOffset,
                     Adc::PLLAD_KS::bitWidth) == 2);
    CHECK(Wire.field(5, Adc::PLLAD_CKOS::byteOffset, Adc::PLLAD_CKOS::bitOffset,
                     Adc::PLLAD_CKOS::bitWidth) == 0);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK1X::byteOffset,
                     Adc::ADC_CLK_ICLK1X::bitOffset, 1) == 1);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK2X::byteOffset,
                     Adc::ADC_CLK_ICLK2X::bitOffset, 1) == 1);
    CHECK(Wire.field(5, Adc::DEC1_BYPS::byteOffset, Adc::DEC1_BYPS::bitOffset, 1) == 0);
    CHECK(Wire.field(5, Adc::DEC2_BYPS::byteOffset, Adc::DEC2_BYPS::bitOffset, 1) == 0);
}

TEST_CASE("with no line rate measured there is no crossover row to pick")
{
    // The cold boot: a divider inherited off the chip and nothing measured yet.
    // CKO is unknown, so the table cannot be read -- and picking the bottom row
    // by arithmetic on a zero would be a guess wearing a calculation's clothes.
    // The divider still goes in; the rest waits for a pass that can measure.
    Wire.reset();
    Wire.bank[5][Adc::PLLAD_KS::byteOffset] = 0xFF;

    Adc::applySampleRate(1856, 0, 4);

    CHECK(Wire.field(5, Adc::PLLAD_MD::byteOffset, Adc::PLLAD_MD::bitOffset,
                     Adc::PLLAD_MD::bitWidth) == 1856);
    CHECK_FALSE(Wire.touched[5][Adc::PLLAD_KS::byteOffset]);
    CHECK(latchRisingEdge() >= 0);
}

TEST_CASE("every bit the latch loads is written before the edge")
{
    // The whole reason this is one function: PLLAD_LAT loads MD, KS and CKOS
    // together, so a group assembled across two calls latches whatever the
    // chip happened to be holding for the rest. A preset load leaving KS at
    // the wrong crossover row is what makes the sync processor -- which counts
    // in ADC clocks -- report a line that is not there.
    Wire.reset();

    Adc::applySampleRate(2250, 15574, 4);

    const int edge = latchRisingEdge();
    REQUIRE(edge >= 0);
    CHECK(lastWriteOf<Adc::PLLAD_MD>() < edge);
    CHECK(lastWriteOf<Adc::PLLAD_KS>() < edge);
    CHECK(lastWriteOf<Adc::PLLAD_CKOS>() < edge);
}

TEST_CASE("the divider reaches the PLL before the latch loads it")
{
    Wire.reset();

    Adc::applySampleRate(2250, 15574, 4);

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

// --- the tap and the decimators, without the divider --------------------------

TEST_CASE("the oversampling tap is one step per doubling below the post divider")
{
    // PLLAD_CKOS picks which tap of the ADC clock feeds the pipeline and the
    // decimators undo in the digital domain what that tap added, so the two
    // describe one ratio between them. Set the tap against decimators for
    // another ratio and the screen is solid green with every register healthy.
    Wire.reset();

    CHECK(Adc::applyOversample(2, 4) == 4);

    CHECK(Wire.field(5, Adc::PLLAD_CKOS::byteOffset, Adc::PLLAD_CKOS::bitOffset,
                     Adc::PLLAD_CKOS::bitWidth) == 0);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK1X::byteOffset,
                     Adc::ADC_CLK_ICLK1X::bitOffset, 1) == 1);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK2X::byteOffset,
                     Adc::ADC_CLK_ICLK2X::bitOffset, 1) == 1);
    CHECK(Wire.field(5, Adc::DEC1_BYPS::byteOffset, Adc::DEC1_BYPS::bitOffset, 1) == 0);
    CHECK(Wire.field(5, Adc::DEC2_BYPS::byteOffset, Adc::DEC2_BYPS::bitOffset, 1) == 0);
}

TEST_CASE("a ratio the post divider cannot carry is installed reduced")
{
    Wire.reset();

    CHECK(Adc::applyOversample(0, 4) == 1);

    CHECK(Wire.field(5, Adc::PLLAD_CKOS::byteOffset, Adc::PLLAD_CKOS::bitOffset,
                     Adc::PLLAD_CKOS::bitWidth) == 0);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK1X::byteOffset,
                     Adc::ADC_CLK_ICLK1X::bitOffset, 1) == 0);
    CHECK(Wire.field(5, Adc::ADC_CLK_ICLK2X::byteOffset,
                     Adc::ADC_CLK_ICLK2X::bitOffset, 1) == 0);
    CHECK(Wire.field(5, Adc::DEC1_BYPS::byteOffset, Adc::DEC1_BYPS::bitOffset, 1) == 1);
    CHECK(Wire.field(5, Adc::DEC2_BYPS::byteOffset, Adc::DEC2_BYPS::bitOffset, 1) == 1);
}

TEST_CASE("the post divider is the caller's, and is not written or read back")
{
    // The chip is not the place to keep it. Reading PLLAD_KS back to derive the
    // tap asks the register what the firmware itself chose, and between a write
    // and the latch that loads it the answer is the value the PLL is not on.
    Wire.reset();
    Wire.bank[5][Adc::PLLAD_KS::byteOffset] = 0xFF;

    Adc::applyOversample(1, 2);

    CHECK_FALSE(Wire.touched[5][Adc::PLLAD_MD::byteOffset]);
    CHECK(Wire.field(5, Adc::PLLAD_KS::byteOffset, Adc::PLLAD_KS::bitOffset,
                     Adc::PLLAD_KS::bitWidth) == 3);
}

TEST_CASE("the tap is written but not loaded, because the caller owns the edge")
{
    // PLLAD_LAT loads MD, ND, KS, CKOS and ICP together, so a caller still
    // assembling that group must not have an edge fired underneath it.
    Wire.reset();

    Adc::applyOversample(2, 2);

    CHECK(latchRisingEdge() == -1);
}
