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

// The two SourceMeasurement.cpp needs, and the PLL rate this suite drives.
float getSourceFieldRate(boolean) { return 50.08f; }
void tv5725Log(const char *) {}

static uint32_t pllRateHz = 0;
uint32_t getPllRate() { return pllRateHz; }


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

// The post divider in force when the load reaches the ladder. Nothing about it
// is meaningful -- it is the previous mode's -- which is why it is the caller's
// to supply and why an SD standard replaces it.
static const uint8_t Inherited = 1;

static uint8_t apply(uint8_t standard, bool inputIsYpBpR, uint8_t poison)
{
    Wire.reset();
    Wire.poison(poison);
    return SourceStandard(standard, inputIsYpBpR).apply(Inherited);
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

TEST_CASE("interlaced SD samples four times over, on its own post divider")
{
    // NTSC and PAL SD carry the least horizontal detail and the most room to
    // oversample, and the crossover row for that clock is the caller's no
    // longer: the standard replaces whatever the previous mode left.
    CHECK(apply(2, false, Poison) == 4);

    CHECK(WRITTEN(2, false, Adc::PLLAD_KS) == 2);
    CHECK(WRITTEN(2, false, Adc::PLLAD_CKOS) == 0);
    CHECK(WRITTEN(2, false, Adc::ADC_CLK_ICLK1X) == 1);
    CHECK(WRITTEN(2, false, Adc::ADC_CLK_ICLK2X) == 1);
    CHECK(WRITTEN(2, false, Adc::DEC1_BYPS) == 0);
    CHECK(WRITTEN(2, false, Adc::DEC2_BYPS) == 0);
}

TEST_CASE("interlaced SD narrows the ADC's analog filter")
{
    // ADC_FLTR 3 is the 40 MHz corner, the narrowest RD-5725-1.1 offers.
    CHECK(WRITTEN(1, false, Adc::ADC_FLTR) == 3);
    CHECK(WRITTEN(2, false, Adc::ADC_FLTR) == 3);
}

TEST_CASE("interlaced SD takes the input formatter off write enable")
{
    CHECK(WRITTEN(1, false, InputFormatter::IF_SEL_WEN) == 0);
    CHECK(WRITTEN(2, false, InputFormatter::IF_SEL_WEN) == 0);
}

TEST_CASE("a component SD source gets the luma and chroma delays")
{
    // Only YPbPr arrives with luma and chroma on separate paths, so only YPbPr
    // needs them realigned.
    CHECK(WRITTEN(2, true, InputFormatter::IF_HS_TAP11_BYPS) == 0);
    CHECK(WRITTEN(2, true, InputFormatter::IF_HS_Y_PDELAY) == 2);
    CHECK(WRITTEN(2, true, VideoProcessor::VDS_V_DELAY) == 0);
    CHECK(WRITTEN(2, true, VideoProcessor::VDS_Y_DELAY) == 3);
}

TEST_CASE("an RGB SD source is left with the delays it had")
{
    CHECK(WRITTEN(2, false, InputFormatter::IF_HS_TAP11_BYPS) == NotWritten);
    CHECK(WRITTEN(2, false, InputFormatter::IF_HS_Y_PDELAY) == NotWritten);
    CHECK(WRITTEN(2, false, VideoProcessor::VDS_V_DELAY) == NotWritten);
    CHECK(WRITTEN(2, false, VideoProcessor::VDS_Y_DELAY) == NotWritten);
}

TEST_CASE("a standard with nothing of its own keeps the inherited post divider")
{
    // Standard 14 is RGBHV, which the geometry engine measures and re-samples
    // for itself. What it takes from here is the oversampling alone.
    CHECK(apply(14, false, Poison) == 2);

    CHECK(WRITTEN(14, false, Adc::ADC_FLTR) == NotWritten);
    CHECK(WRITTEN(14, false, InputFormatter::IF_SEL_WEN) == NotWritten);
    CHECK(WRITTEN(14, false, Adc::PLLAD_KS) == NotWritten);
}

// --- the progressive standards -----------------------------------------------

// The sync processor's line count, which standard 9 reads to tell a tall source
// from an ordinary one. Written into the bank so the run sees a chosen value
// rather than the poison, which lands either side of the threshold.
static void sourceLines(uint16_t lines)
{
    Wire.bank[0][0x1B] = static_cast<uint8_t>(lines & 0xFF);
    Wire.bank[0][0x1C] = static_cast<uint8_t>(lines >> 8);
}

TEST_CASE("a progressive standard samples twice over, on its own post divider")
{
    CHECK(apply(3, false, Poison) == 2);

    CHECK(WRITTEN(3, false, Adc::PLLAD_KS) == 1);
    CHECK(WRITTEN(3, false, Adc::PLLAD_CKOS) == 0);
    CHECK(WRITTEN(3, false, Adc::ADC_CLK_ICLK1X) == 1);
    CHECK(WRITTEN(3, false, Adc::ADC_CLK_ICLK2X) == 0);
    CHECK(WRITTEN(3, false, Adc::DEC1_BYPS) == 1);
    CHECK(WRITTEN(3, false, Adc::DEC2_BYPS) == 0);
}

TEST_CASE("a progressive standard puts the input formatter on write enable")
{
    CHECK(WRITTEN(4, false, InputFormatter::IF_SEL_WEN) == 1);
    CHECK(WRITTEN(4, false, InputFormatter::IF_HS_SEL_LPF) == 0);
    CHECK(WRITTEN(4, false, InputFormatter::IF_HB_SP) == 0);
    CHECK(WRITTEN(4, false, Adc::ADC_FLTR) == 3);
}

TEST_CASE("a progressive standard realigns luma and chroma whatever the colour space")
{
    // Unlike interlaced SD, where only a component source gets these.
    CHECK(WRITTEN(4, false, InputFormatter::IF_HS_TAP11_BYPS) == 0);
    CHECK(WRITTEN(4, false, InputFormatter::IF_HS_Y_PDELAY) == 3);
    CHECK(WRITTEN(4, false, VideoProcessor::VDS_V_DELAY) == 1);
    CHECK(WRITTEN(4, false, VideoProcessor::VDS_Y_DELAY) == 3);
    CHECK(WRITTEN(4, false, Deinterlacer::MADPT_Y_DELAY_UV_DELAY) == 1);
}

TEST_CASE("standard 3 opens the SD vsync window later than its neighbours")
{
    CHECK(WRITTEN(4, false, SyncProcessor::SP_SDCS_VSST_REG_L) == 14);
    CHECK(WRITTEN(4, false, SyncProcessor::SP_SDCS_VSSP_REG_L) == 11);

    CHECK(WRITTEN(3, false, SyncProcessor::SP_SDCS_VSST_REG_L) == 16);
    CHECK(WRITTEN(3, false, SyncProcessor::SP_SDCS_VSSP_REG_L) == 13);
}

TEST_CASE("standards 3 and 4 each blank their own input line")
{
    CHECK(WRITTEN(3, false, InputFormatter::IF_HB_ST) == 30);
    CHECK(WRITTEN(3, false, InputFormatter::IF_HBIN_ST) == 0x20);
    CHECK(WRITTEN(3, false, InputFormatter::IF_HBIN_SP) == 0x60);

    CHECK(WRITTEN(4, false, InputFormatter::IF_HB_ST) == 0x30);
    CHECK(WRITTEN(4, false, InputFormatter::IF_HBIN_ST) == 0x20);
    CHECK(WRITTEN(4, false, InputFormatter::IF_HBIN_SP) == 0x40);
}

TEST_CASE("standard 9 takes a taller source down an octave")
{
    // Past 650 lines the clock the ordinary post divider produces is outside
    // its crossover row. The count is read twice with a settle between, so one
    // sample caught mid-transition does not move the divider.
    Wire.reset();
    Wire.poison(Poison);
    sourceLines(700);

    SourceStandard(9, false).apply(Inherited);

    CHECK(Wire.field(5, Adc::PLLAD_KS::byteOffset, Adc::PLLAD_KS::bitOffset,
                     Adc::PLLAD_KS::bitWidth) == 0);
}

TEST_CASE("standard 9 on an ordinary source keeps the progressive post divider")
{
    Wire.reset();
    Wire.poison(Poison);
    sourceLines(524);

    SourceStandard(9, false).apply(Inherited);

    CHECK(Wire.field(5, Adc::PLLAD_KS::byteOffset, Adc::PLLAD_KS::bitOffset,
                     Adc::PLLAD_KS::bitWidth) == 1);
}

// --- the HD standards, and the one with a measurement of its own --------------

TEST_CASE("an HD standard opens the ADC filter and takes the line whole")
{
    // 5, 6 and 7 reach here through the HD bypass switch. ADC_FLTR 1 is the
    // 110 MHz corner, four times the SD one, and the line doubler comes off.
    CHECK(apply(5, false, Poison) == 2);

    CHECK(WRITTEN(5, false, Adc::ADC_FLTR) == 1);
    CHECK(WRITTEN(5, false, InputFormatter::IF_PRGRSV_CNTRL) == 1);
    CHECK(WRITTEN(5, false, InputFormatter::IF_HS_DEC_FACTOR) == 0);
    CHECK(WRITTEN(5, false, VideoProcessor::VDS_Y_DELAY) == 3);
}

TEST_CASE("the two tallest HD standards drop an octave, 720p does not")
{
    CHECK(WRITTEN(6, false, Adc::PLLAD_KS) == 0);
    CHECK(WRITTEN(7, false, Adc::PLLAD_KS) == 0);

    CHECK(WRITTEN(5, false, Adc::PLLAD_KS) == NotWritten);
}

TEST_CASE("standard 8 opens the filter its progressive neighbours narrowed")
{
    // It is in the progressive group as well, so applyProgressive() runs first
    // and this overrides the parts it disagrees with.
    CHECK(WRITTEN(8, false, Adc::ADC_FLTR) == 1);
    CHECK(WRITTEN(8, false, InputFormatter::IF_HB_ST) == 30);
    CHECK(WRITTEN(8, false, InputFormatter::IF_HBIN_SP) == 0x60);
    CHECK(WRITTEN(8, false, Adc::PLLAD_ICP) == 6);

    // and keeps what it does not disagree with
    CHECK(WRITTEN(8, false, InputFormatter::IF_SEL_WEN) == 1);
    CHECK(WRITTEN(8, false, Adc::PLLAD_KS) == 1);
}

TEST_CASE("standard 8 drops the VCO gain for a PLL running in the middle band")
{
    pllRateHz = 1000;
    CHECK(WRITTEN(8, false, Adc::PLLAD_FS) == 0);
    pllRateHz = 0;
}

TEST_CASE("outside that band standard 8 leaves the VCO gain alone")
{
    pllRateHz = 100;
    CHECK(WRITTEN(8, false, Adc::PLLAD_FS) == NotWritten);

    pllRateHz = 2000;
    CHECK(WRITTEN(8, false, Adc::PLLAD_FS) == NotWritten);
    pllRateHz = 0;
}
