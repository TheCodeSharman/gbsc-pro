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

static int lastLatchRisingEdge()
{
    const uint8_t mask = static_cast<uint8_t>(1u << Adc::PLLAD_LAT::bitOffset);
    bool low = false;
    int at = -1;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        if (Wire.trace[i].segment != 5
            || Wire.trace[i].reg != Adc::PLLAD_LAT::byteOffset)
            continue;
        if (!(Wire.trace[i].value & mask))
            low = true;
        else if (low) {
            at = static_cast<int>(i);
            low = false;
        }
    }
    return at;
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

// The analog input mux and the sync-on-green enable. Separate verbs because an
// input choice writes the mux LAST, after the sync path is configured.

static const uint8_t InputPoisons[2] = {0xA5, 0x5A};

template <typename Field>
static uint32_t afterCall(void (*control)(uint8_t), uint8_t value)
{
    Wire.reset();
    Wire.poison(InputPoisons[0]);
    control(value);
    return Field::read();
}

template <typename Field>
static bool callWrote(void (*control)(uint8_t), uint8_t value)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(InputPoisons[i]);
        control(value);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("selecting an input writes the mux")
{
    CHECK(afterCall<Adc::ADC_INPUT_SEL>(Adc::selectInput, 1) == 1);
    CHECK(afterCall<Adc::ADC_INPUT_SEL>(Adc::selectInput, 0) == 0);
}

TEST_CASE("the sync-on-green enable is its own choice")
{
    CHECK(afterCall<Adc::ADC_SOGEN>(Adc::enableSyncOnGreen, 1) == 1);
    CHECK(afterCall<Adc::ADC_SOGEN>(Adc::enableSyncOnGreen, 0) == 0);
}

TEST_CASE("neither touches the other, nor the ADC PLL")
{
    CHECK(callWrote<Adc::ADC_INPUT_SEL>(Adc::selectInput, 1));
    CHECK_FALSE(callWrote<Adc::ADC_SOGEN>(Adc::selectInput, 1));

    CHECK(callWrote<Adc::ADC_SOGEN>(Adc::enableSyncOnGreen, 1));
    CHECK_FALSE(callWrote<Adc::ADC_INPUT_SEL>(Adc::enableSyncOnGreen, 1));

    CHECK_FALSE(callWrote<Adc::PLLAD_MD>(Adc::selectInput, 1));
    CHECK_FALSE(callWrote<Adc::PLLAD_KS>(Adc::selectInput, 1));
    CHECK_FALSE(callWrote<Adc::PLLAD_CKOS>(Adc::selectInput, 1));
}


// --- the ADC as RGBHV bypass sets it up --------------------------------------

TEST_CASE("the divider goes in before the edge that loads it")
{
    // The whole reason this lives in a class with a trace behind it. PLLAD_LAT
    // loads MD on a rising edge, so a divider written after that edge leaves
    // the PLL clocking the old one while the register reads the new -- a solid
    // green screen with nothing self-inconsistent to diagnose from.
    Wire.reset();
    Adc::applySampleRate(2039, 37879, 1);

    const int md = lastWriteOf<Adc::PLLAD_MD>();
    const int edge = latchRisingEdge();
    CHECK(md >= 0);
    CHECK(edge >= 0);
    CHECK(md < edge);
}

TEST_CASE("bypass leaves the divider to whoever measured the source")
{
    // It used to write a literal 1856, which every caller then overwrote. The
    // sync processor counts back a LATCHED divider, so two writers of it is two
    // answers to the one witness that the latch happened.
    Wire.reset();
    Adc::applyForBypassRgbhv();

    CHECK(lastWriteOf<Adc::PLLAD_MD>() < 0);
}

TEST_CASE("bypass takes the ADC's internal filter out of the path")
{
    // Bypass hands the ADC straight to the DACs, so anything shaping the
    // samples on the way is shaping the picture.
    Wire.reset();
    Adc::applyForBypassRgbhv();

    CHECK(Wire.field(5, 0x03, 4, 2) == 0);
}

// The one thing measured to clear a railed HPERIOD_IF from this end: take the
// ADC's input away and give it back, so the input formatter re-acquires the
// line. docs/investigations/hperiod-if-railing.md
TEST_CASE("the input bounce takes the input away and puts the same one back")
{
    Wire.reset();
    Adc::ADC_INPUT_SEL::write(1);          // VGA, the bench input
    Wire.trace.clear();

    Adc::bounceInput();

    // Away and back, in that order, and back to the input it found rather than
    // a hardcoded one -- the bounce must not become an input change.
    REQUIRE(Wire.trace.size() >= 2);
    CHECK(Adc::ADC_INPUT_SEL::read() == 1);

    int away = -1, back = -1;
    for (size_t i = 0; i < Wire.trace.size(); ++i) {
        if (Wire.trace[i].segment != 5 || Wire.trace[i].reg != 0x02)
            continue;
        uint8_t sel = (uint8_t)((Wire.trace[i].value >> 6) & 0x3);
        if (sel == 0 && away < 0)
            away = (int)i;
        else if (sel == 1 && away >= 0)
            back = (int)i;
    }
    CHECK(away >= 0);
    CHECK(back > away);
}

TEST_CASE("the bounce leaves the rest of the byte alone")
{
    // ADC_INPUT_SEL shares s5_02 with the sync-on-green sync separator level, so a byte
    // write here would take the sync separator with it.
    Wire.reset();
    Wire.bank[5][0x02] = 0x58;             // level 12 under input 1
    Adc::bounceInput();

    CHECK(Wire.bank[5][0x02] == 0x58);
}

// Trying the other input, the escalation the ladder reaches last. The ADC has
// two RGB inputs and a source that will not lock on one is worth trying on the
// other; the caller that does not lock puts back what this reports.

TEST_CASE("trying the other input moves off the one in force and says which it was")
{
    Wire.reset();
    Adc::selectInput(1);

    const uint8_t previous = Adc::selectOtherInput();

    CHECK(previous == 1);
    CHECK(Adc::ADC_INPUT_SEL::read() == 0);
}

TEST_CASE("trying the other input from anywhere but one lands on one")
{
    Wire.reset();
    Adc::selectInput(2);

    const uint8_t previous = Adc::selectOtherInput();

    CHECK(previous == 2);
    CHECK(Adc::ADC_INPUT_SEL::read() == 1);
}

TEST_CASE("trying the other input leaves the sync separator level alone")
{
    // ADC_INPUT_SEL shares s5_02 with the level, so a byte write here would
    // take the sync separator with it.
    Wire.reset();
    Wire.bank[5][0x02] = 0x58;             // level 12 under input 1

    Adc::selectOtherInput();

    CHECK(((Wire.bank[5][0x02] >> 1) & 0x1f) == 12);
}

// The sampling phase: where in the ADC clock the sample is taken. Two
// adjusters, the sync processor's and the ADC's, each latched by its own bit.

TEST_CASE("a sampling phase is latched, not merely written")
{
    // The value only reaches the adjuster on a rising edge of its latch bit, so
    // a write without one leaves the phase where it was with the register
    // reading the new value -- the same trap PLLAD_MD has.
    Wire.reset();

    Adc::applyPhaseAdc(9);

    CHECK(Adc::PA_ADC_S::read() == 9);
    CHECK(Adc::PA_ADC_LAT::read() == 1);
}

TEST_CASE("the sync processor's phase is a different adjuster from the ADC's")
{
    Wire.reset();

    Adc::applyPhaseSyncProcessor(5);

    CHECK(Adc::PA_SP_S::read() == 5);
    CHECK(Adc::PA_SP_LAT::read() == 1);
    CHECK(Adc::PA_ADC_S::read() == 0);
}

TEST_CASE("a phase past the field is refused rather than truncated")
{
    // Five bits. Masking 32 in puts 0 there, which is a phase nobody chose.
    Wire.reset();
    Adc::applyPhaseAdc(9);

    Adc::applyPhaseAdc(Adc::PhaseMax + 1);

    CHECK(Adc::PA_ADC_S::read() == 9);
}

TEST_CASE("restarting the adjusters leaves both out of bypass")
{
    Wire.reset();

    Adc::restartPhaseAdjusters();

    CHECK(Adc::PA_SP_BYPSZ::read() == 1);
    CHECK(Adc::PA_ADC_BYPSZ::read() == 1);
}

// The VCO gain. RD-5725-1.1 names the bit and gives it no band, so the
// thresholds here are the bench sweep at 800x600@60: gain 0 locks up to 136 MHz
// of VCO and fails by 144, gain 1 locks down to 121 MHz and fails by 106.
// docs/investigations/the-vco-gain-follows-the-vco.md

TEST_CASE("the VCO gain is the low one only where the high one will not lock")
{
    // The sweep leaves an overlap between 121 and 136 MHz where both gains
    // lock, so the threshold inside it is a choice rather than a boundary. What
    // the rule has to get right is either side of it.
    CHECK(Adc::vcoGainFor(90900000u) == 0);
    CHECK(Adc::vcoGainFor(106100000u) == 0);

    CHECK(Adc::vcoGainFor(143900000u) == 1);
    CHECK(Adc::vcoGainFor(151500000u) == 1);
    CHECK(Adc::vcoGainFor(159100000u) == 1);
}

TEST_CASE("the sample rate writes the gain for the VCO, not for CKO")
{
    // 143.9 MHz was reached at two post dividers on the bench -- CKO 72.0 MHz
    // over two and 36.0 MHz over four -- and both need the high gain. Read
    // against CKO the two would disagree.
    Wire.reset();
    Adc::applySampleRate(1900, 37879, 1);   // CKO 72.0 MHz, KS 1
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_FS::read() == 1);

    Wire.reset();
    Adc::applySampleRate(950, 37879, 1);    // CKO 36.0 MHz, KS 2
    CHECK(Adc::PLLAD_KS::read() == 2);
    CHECK(Adc::PLLAD_FS::read() == 1);

    Wire.reset();
    Adc::applySampleRate(1200, 37879, 1);   // CKO 45.5 MHz, VCO 90.9 MHz
    CHECK(Adc::PLLAD_KS::read() == 1);
    CHECK(Adc::PLLAD_FS::read() == 0);
}

TEST_CASE("a rate of nothing writes no gain it cannot derive")
{
    // No CKO, so no VCO to read a gain against. The divider still goes in.
    Wire.reset();
    Wire.poison(0xA5);
    Adc::applySampleRate(1856, 0, 1);

    CHECK(Wire.field(5, 0x11, 5, 1) == ((0xA5 >> 5) & 1));
}

TEST_CASE("the scaling path brings the charge pump down off the bypass entry's")
{
    Wire.reset();
    Adc::PLLAD_ICP::write(6);

    Adc::applyScalingChargePump();

    CHECK(Adc::PLLAD_ICP::read() == 5);
    CHECK(lastWriteOf<Adc::PLLAD_ICP>() < lastLatchRisingEdge());
}

TEST_CASE("a charge pump already below the band's is not relatched")
{
    Wire.reset();
    Adc::PLLAD_ICP::write(4);
    const size_t before = Wire.trace.size();

    Adc::applyScalingChargePump();

    CHECK(Adc::PLLAD_ICP::read() == 4);
    CHECK(Wire.trace.size() == before);
}

TEST_CASE("the sampling phase is held here, and nothing reads it off the chip")
{
    // Both phases were the sketch's, and one site took PA_ADC_S back into the
    // held value -- a register standing in for state the engine chose. One
    // owner holds both, chooses without writing, and puts them in force
    // together. docs/video-source-acquisition.md
    Wire.reset();

    Adc::choosePhaseSyncProcessor(9);
    Adc::choosePhaseAdc(24);

    CHECK(Adc::phaseSyncProcessor() == 9);
    CHECK(Adc::phaseAdc() == 24);

    SUBCASE("choosing reaches no register") {
        CHECK(Adc::PA_SP_S::read() == 0);
        CHECK(Adc::PA_ADC_S::read() == 0);
    }

    SUBCASE("putting them in force latches both adjusters") {
        Adc::applyPhases();

        CHECK(Adc::PA_SP_S::read() == 9);
        CHECK(Adc::PA_SP_LAT::read() == 1);
        CHECK(Adc::PA_ADC_S::read() == 24);
        CHECK(Adc::PA_ADC_LAT::read() == 1);
    }

    SUBCASE("a phase past the field is refused rather than truncated") {
        Adc::choosePhaseAdc(Adc::PhaseMax + 1);
        CHECK(Adc::phaseAdc() == 24);
    }

    SUBCASE("the ADC's phase steps round its field") {
        Adc::choosePhaseAdc(Adc::PhaseMax);
        Adc::nudgePhaseAdc();
        CHECK(Adc::phaseAdc() == 0);
    }
}

// A sync processor whose line count is unstable at a chosen band of phases,
// which is the only thing the sweep judges by. Reads back the phase actually
// latched, so it answers the search rather than a script of it.
static uint16_t g_sweepDivider = 0;
static uint8_t g_badFrom = 0, g_badTo = 0;
static unsigned g_feeds = 0;

static uint16_t lineSamplesAtPhase()
{
    const uint8_t at = Adc::PA_SP_S::read();
    const bool bad = at >= g_badFrom && at <= g_badTo;
    return bad ? (uint16_t)(g_sweepDivider + 1) : g_sweepDivider;
}

static void countFeed() { ++g_feeds; }

TEST_CASE("the sampling phase is swept for the window furthest from the worst")
{
    // The search walks the field, scores each phase by how often the sync
    // processor miscounts the line, and takes the point OPPOSITE the worst run
    // of three -- so the phase chosen is half a field away from where sampling
    // was least stable. docs/video-source-acquisition.md
    Wire.reset();
    Adc::PLLAD_MD::write(2230);
    g_sweepDivider = 2230;
    g_badFrom = 4; g_badTo = 6;
    g_feeds = 0;
    Adc::choosePhaseSyncProcessor(16);
    Adc::choosePhaseAdc(16);

    CHECK(Adc::acquirePhase(2, true, false, lineSamplesAtPhase, countFeed));

    SUBCASE("the phase lands opposite the worst window") {
        CHECK(Adc::phaseSyncProcessor() == 21);
    }

    SUBCASE("and both adjusters are latched with it") {
        CHECK(Adc::PA_SP_S::read() == 21);
        CHECK(Adc::PA_SP_LAT::read() == 1);
        CHECK(Adc::PA_ADC_LAT::read() == 1);
    }

    SUBCASE("the watchdog is fed inside the loop, not once around it") {
        CHECK(g_feeds > 34);
    }
}

TEST_CASE("a sweep that finds no steady phase chooses none")
{
    // Every phase miscounting means the fault is not the phase, and picking one
    // out of noise is worse than leaving it.
    Wire.reset();
    Adc::PLLAD_MD::write(2230);
    g_sweepDivider = 2230;
    g_badFrom = 0; g_badTo = Adc::PhaseMax;
    Adc::choosePhaseSyncProcessor(9);
    Adc::choosePhaseAdc(24);

    CHECK_FALSE(Adc::acquirePhase(2, true, false, lineSamplesAtPhase, countFeed));

    // The ADC's phase is untouched, because only the search writes it.
    CHECK(Adc::phaseAdc() == 24);

    // **The sync processor's is left where the WALK stopped**, not where it
    // started: the search latches each phase to score it, so a refusal still
    // leaves the last one it tried. Two steps on from the entry phase, because
    // the walk is two longer than the field. That is what the sketch did and it
    // is preserved here rather than quietly repaired -- a failed search leaving
    // the phase somewhere it did not choose is a defect to fix against a bench
    // reading, not in a move.
    CHECK(Adc::phaseSyncProcessor() == 11);
}

TEST_CASE("a separator too starved to judge by skips the search")
{
    // The sweep scores phases by the sync processor's count, and a starved
    // separator makes that noise -- so the mid of the field is the whole of
    // the answer and the search is not worth its 34 steps.
    Wire.reset();
    Adc::PLLAD_MD::write(2230);
    g_sweepDivider = 2230;
    g_badFrom = 0; g_badTo = Adc::PhaseMax;
    g_feeds = 0;
    Adc::choosePhaseSyncProcessor(3);
    Adc::choosePhaseAdc(3);

    CHECK(Adc::acquirePhase(2, false, false, lineSamplesAtPhase, countFeed));

    CHECK(Adc::phaseSyncProcessor() == 16);
    CHECK(Adc::phaseAdc() == 16);
    CHECK(g_feeds == 0);

    SUBCASE("four times oversampling asks for half a sample more on the ADC") {
        Adc::acquirePhase(4, false, false, lineSamplesAtPhase, countFeed);
        CHECK(Adc::phaseAdc() == 0);   // 16 + 16, round the five-bit field
    }
}

// --- which input the capture path is reading ---------------------------------

TEST_CASE("the ADC holds which input it selected, and says whether it is component")
{
    // ADC_INPUT_SEL 0 is the component pair. Held rather than read back: a
    // register is where a value is written to, never where it is kept, and the
    // engine needs the answer to realign luma against chroma.
    Wire.reset();

    Adc::selectInput(1);
    CHECK_FALSE(Adc::inputIsComponent());

    Adc::selectInput(0);
    CHECK(Adc::inputIsComponent());
}

TEST_CASE("moving to the other input moves the held answer with it")
{
    Wire.reset();
    Adc::selectInput(0);

    CHECK(Adc::selectOtherInput() == 0);
    CHECK_FALSE(Adc::inputIsComponent());

    CHECK(Adc::selectOtherInput() == 1);
    CHECK(Adc::inputIsComponent());
}

TEST_CASE("a bounce puts the same input back, so the held answer does not move")
{
    // The bounce clears a railed HPERIOD_IF by taking the input away and
    // putting the SAME one back. Nothing about the source changed.
    Wire.reset();
    Adc::selectInput(1);

    Adc::bounceInput();

    CHECK_FALSE(Adc::inputIsComponent());
}
