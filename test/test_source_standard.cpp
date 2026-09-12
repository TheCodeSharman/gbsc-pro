// Host-compiled coverage for what a video standard implies -- `make -C test
// source-standard`.
//
// The standard is Mode Detect's classification of the source, and a handful of
// registers follow from it alone. Every one of them has other writers, so the
// question a test has to answer is not "what does the register hold" but "did
// this write it" -- which is what the complementary poisons below are for.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <Arduino.h>

#include "Si5351Stubs.h"
#include "fake/Wire.h"

// The two SourceMeasurement.cpp needs. getPllRate() is a test-bus measurement
// and nothing here reads it any more, so it answers a value no band could want.
float getSourceFieldRate(boolean) { return 50.08f; }
void tv5725Log(const char *) {}

uint32_t getPllRate() { return 0; }


FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Deinterlacer.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceStandard.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

static const uint8_t Poison = 0xA5;
static const uint32_t NotWritten = 0xFFFFFFFFu;

// The sync processor's line count. Presented explicitly so a case can vary it
// and show that nothing here branches on it.
static const uint16_t OrdinarySourceLines = 524;
static const uint16_t TallSourceLines = 700;
static uint16_t presentedLines = OrdinarySourceLines;

static void presentLineCount(uint16_t lines)
{
    Wire.bank[0][0x1B] = static_cast<uint8_t>(lines & 0xFF);
    Wire.bank[0][0x1C] = static_cast<uint8_t>(lines >> 8);
}

static void apply(uint8_t standard, bool inputIsYpBpR, uint8_t poison)
{
    Wire.reset();
    Wire.poison(poison);
    presentLineCount(presentedLines);
    SourceStandard(standard, inputIsYpBpR).apply();
}

// A field the run left at the poison was never written. One poison proves
// nothing where its bits already match the wanted value, so the run is repeated
// under the complement and a field the two disagree about is NotWritten.
static uint32_t written(uint8_t standard, bool inputIsYpBpR, uint8_t segment,
                        uint8_t reg, uint8_t offset, uint8_t width)
{
    const uint8_t poisons[2] = {Poison, static_cast<uint8_t>(~Poison)};
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        apply(standard, inputIsYpBpR, poisons[i]);
        under[i] = Wire.field(segment, reg, offset, width);
    }
    return under[0] == under[1] ? under[0] : NotWritten;
}

// A hand-written address does not error, it returns a plausible number, so a
// field is named only through its own typedef.
#define WRITTEN(standard, ypbpr, Field)                                        \
    written(standard, ypbpr, Field::segment, Field::byteOffset,                \
            Field::bitOffset, Field::bitWidth)

TEST_CASE("no standard writes the ADC PLL group")
{
    // Adc::applySampleRate() is the single owner: it takes the divider and the
    // line rate the engine measured and derives the row, the gain and the tap
    // from the clock those two make. A literal here is a second derivation of
    // the same thing against a post divider nobody measured.
    for (uint8_t standard : {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14}) {
        CAPTURE(standard);
        CHECK(WRITTEN(standard, false, Adc::PLLAD_KS) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::PLLAD_FS) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::PLLAD_ICP) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::PLLAD_CKOS) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::ADC_CLK_ICLK1X) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::ADC_CLK_ICLK2X) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::DEC1_BYPS) == NotWritten);
        CHECK(WRITTEN(standard, false, Adc::DEC2_BYPS) == NotWritten);
    }
}

TEST_CASE("interlaced SD narrows the ADC's analog filter")
{
    // ADC_FLTR 3 is the 40 MHz corner, the narrowest RD-5725-1.1 offers.
    CHECK(WRITTEN(1, false, Adc::ADC_FLTR) == 3);
    CHECK(WRITTEN(2, false, Adc::ADC_FLTR) == 3);
}

TEST_CASE("no standard writes what the scan mode decides")
{
    // IF_SEL_WEN is the line doubler's own write enable and IF_HS_SEL_LPF, 
    // VDS_V_DELAY and MADPT_Y_DELAY realign the 422/444 conversion around it,
    // so all four follow whether the doubler is in the path -- which the engine
    // measures and no classification can improve on.
    for (uint8_t standard : {1, 2, 3, 4, 8, 9}) {
        CAPTURE(standard);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_SEL_WEN) == NotWritten);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_HS_SEL_LPF) == NotWritten);
        CHECK(WRITTEN(standard, false, VideoProcessor::VDS_V_DELAY) == NotWritten);
        CHECK(WRITTEN(standard, true, VideoProcessor::VDS_V_DELAY) == NotWritten);
        CHECK(WRITTEN(standard, false, Deinterlacer::MADPT_Y_DELAY) == NotWritten);
    }
}

TEST_CASE("no standard writes the luma delay either")
{
    // It needs the colour path as well as the scan mode, and both are the
    // engine's: it is told which connector is live and it measures the doubler.
    for (uint8_t standard : {1, 2, 3, 4, 8, 9}) {
        CAPTURE(standard);
        for (bool ypbpr : {false, true}) {
            CHECK(WRITTEN(standard, ypbpr, InputFormatter::IF_HS_TAP11_BYPS) == NotWritten);
            CHECK(WRITTEN(standard, ypbpr, InputFormatter::IF_HS_Y_PDELAY) == NotWritten);
            CHECK(WRITTEN(standard, ypbpr, VideoProcessor::VDS_Y_DELAY) == NotWritten);
        }
    }
}

TEST_CASE("a standard with nothing of its own writes nothing at all")
{
    // Standard 14 is RGBHV, which the geometry engine measures and samples for
    // itself, and there is no line of that shape to prepare the rest of the
    // pipeline for.
    CHECK(WRITTEN(14, false, Adc::ADC_FLTR) == NotWritten);
    CHECK(WRITTEN(14, false, SyncProcessor::SP_SDCS_VSST_REG_L) == NotWritten);
    CHECK(WRITTEN(14, false, InputFormatter::IF_PRGRSV_CNTRL) == NotWritten);
    CHECK(WRITTEN(14, false, VideoProcessor::VDS_Y_DELAY) == NotWritten);
}

// --- the progressive standards -----------------------------------------------

TEST_CASE("a progressive standard narrows the ADC's analog filter")
{
    CHECK(WRITTEN(4, false, Adc::ADC_FLTR) == 3);
}

TEST_CASE("standard 3 opens the SD vsync window later than its neighbours")
{
    CHECK(WRITTEN(4, false, SyncProcessor::SP_SDCS_VSST_REG_L) == 14);
    CHECK(WRITTEN(4, false, SyncProcessor::SP_SDCS_VSSP_REG_L) == 11);

    CHECK(WRITTEN(3, false, SyncProcessor::SP_SDCS_VSST_REG_L) == 16);
    CHECK(WRITTEN(3, false, SyncProcessor::SP_SDCS_VSSP_REG_L) == 13);
}

TEST_CASE("the source's height changes nothing")
{
    // The progressive arm used to read the line count twice to pick between two
    // post dividers. The clock answers that, and it is measured rather than
    // bucketed, so no standard asks the source how tall it is.
    for (uint8_t standard : {2, 3, 4, 5, 8, 9}) {
        CAPTURE(standard);
        presentedLines = OrdinarySourceLines;
        Wire.reset();
        Wire.poison(Poison);
        presentLineCount(presentedLines);
        SourceStandard(standard, false).apply();
        const std::vector<FakeTwoWire::Traced> ordinary = Wire.trace;

        presentedLines = TallSourceLines;
        Wire.reset();
        Wire.poison(Poison);
        presentLineCount(presentedLines);
        SourceStandard(standard, false).apply();

        CHECK(Wire.trace.size() == ordinary.size());
    }
    presentedLines = OrdinarySourceLines;
}

// --- the HD standards, and the one with a measurement of its own --------------

TEST_CASE("an HD standard opens the ADC filter and takes the line whole")
{
    // 5, 6 and 7 reach here through the HD bypass switch. ADC_FLTR 1 is the
    // 110 MHz corner, four times the SD one, and the line doubler comes off.
    CHECK(WRITTEN(5, false, Adc::ADC_FLTR) == 1);
    CHECK(WRITTEN(5, false, InputFormatter::IF_PRGRSV_CNTRL) == 1);
    CHECK(WRITTEN(5, false, InputFormatter::IF_HS_DEC_FACTOR) == 0);
    CHECK(WRITTEN(5, false, VideoProcessor::VDS_Y_DELAY) == 3);
}

TEST_CASE("standard 8 opens the filter its progressive neighbours narrowed")
{
    // It is in the progressive group as well, so applyProgressive() runs first
    // and this overrides the parts it disagrees with.
    CHECK(WRITTEN(8, false, Adc::ADC_FLTR) == 1);

    // and keeps what it does not disagree with
    CHECK(WRITTEN(8, false, SyncProcessor::SP_SDCS_VSST_REG_L) == 14);
}

// --- the input line's horizontal blanking -------------------------------------

TEST_CASE("no standard writes the input line's horizontal blanking")
{
    // One capture-window quantity, one owner. A standard writing it is a
    // second owner, and the SD arm writes none of the four, so a progressive
    // standard's values survive into the next mode.
    // docs/investigations/if-hbin-second-capture-window.md
    for (uint8_t standard : {1, 2, 3, 4, 8, 9, 14}) {
        CAPTURE(standard);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_HB_ST) == NotWritten);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_HB_SP) == NotWritten);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_HBIN_ST) == NotWritten);
        CHECK(WRITTEN(standard, false, InputFormatter::IF_HBIN_SP) == NotWritten);
    }
}
