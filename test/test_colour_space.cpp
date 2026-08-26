// Host-compiled unit tests for Tv5725::ColourSpace -- `make -C test colour-space`.
//
// The colour space a source arrives in decides the ADC's R-Y select, the two
// matrix bypasses and the video processor's gains and offsets. No source here
// is component, so the values are the ones the trace fixtures recorded, not
// values anyone judged by eye.
//
// "Not written" is proved by running under two COMPLEMENTARY poisons and
// checking the two disagree. One poison cannot tell a field written 0 from a
// field left at a poison whose bit is already 0.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourSpace.h"

using namespace Tv5725;

static const uint8_t Poisons[2] = {0xA5, 0x5A};

template <typename Field>
static uint32_t after(void (*apply)())
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    apply();
    return Field::read();
}

template <typename Field>
static bool wasWritten(void (*apply)())
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        apply();
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("component takes the ADC's R-Y select on R and B, not on G")
{
    CHECK(after<Adc::ADC_RYSEL_R>(ColourSpace::applyYuv) == 1);
    CHECK(after<Adc::ADC_RYSEL_G>(ColourSpace::applyYuv) == 0);
    CHECK(after<Adc::ADC_RYSEL_B>(ColourSpace::applyYuv) == 1);
}

TEST_CASE("RGB takes no R-Y select at all")
{
    CHECK(after<Adc::ADC_RYSEL_R>(ColourSpace::applyRgb) == 0);
    CHECK(after<Adc::ADC_RYSEL_G>(ColourSpace::applyRgb) == 0);
    CHECK(after<Adc::ADC_RYSEL_B>(ColourSpace::applyRgb) == 0);
}

TEST_CASE("the decimator matrix is bypassed for component and run for RGB")
{
    CHECK(after<ColourSpace::DEC_MATRIX_BYPS>(ColourSpace::applyYuv) == 1);
    CHECK(after<ColourSpace::DEC_MATRIX_BYPS>(ColourSpace::applyRgb) == 0);
}

TEST_CASE("the input formatter's matrix is bypassed either way")
{
    CHECK(after<InputFormatter::IF_MATRIX_BYPS>(ColourSpace::applyYuv) == 1);
    CHECK(after<InputFormatter::IF_MATRIX_BYPS>(ColourSpace::applyRgb) == 1);
}

TEST_CASE("the gains differ only in how they are spelled")
{
    CHECK(after<VideoProcessor::VDS_Y_GAIN>(ColourSpace::applyYuv) == 0x80);
    CHECK(after<VideoProcessor::VDS_UCOS_GAIN>(ColourSpace::applyYuv) == 0x1C);
    CHECK(after<VideoProcessor::VDS_VCOS_GAIN>(ColourSpace::applyYuv) == 0x29);

    CHECK(after<VideoProcessor::VDS_Y_GAIN>(ColourSpace::applyRgb) == 0x80);
    CHECK(after<VideoProcessor::VDS_UCOS_GAIN>(ColourSpace::applyRgb) == 0x1C);
    CHECK(after<VideoProcessor::VDS_VCOS_GAIN>(ColourSpace::applyRgb) == 0x29);
}

TEST_CASE("component carries an offset on every channel and RGB carries none")
{
    CHECK(after<VideoProcessor::VDS_Y_OFST>(ColourSpace::applyYuv) == 0x0E);
    CHECK(after<VideoProcessor::VDS_U_OFST>(ColourSpace::applyYuv) == 0x03);
    CHECK(after<VideoProcessor::VDS_V_OFST>(ColourSpace::applyYuv) == 0x04);

    CHECK(after<VideoProcessor::VDS_Y_OFST>(ColourSpace::applyRgb) == 0x00);
    CHECK(after<VideoProcessor::VDS_U_OFST>(ColourSpace::applyRgb) == 0x00);
    CHECK(after<VideoProcessor::VDS_V_OFST>(ColourSpace::applyRgb) == 0x00);
}

TEST_CASE("only component sets the ADC gains, and RGB leaves them alone")
{
    CHECK(after<Adc::ADC_RGCTRL>(ColourSpace::applyYuv) == 0x33);
    CHECK(after<Adc::ADC_GGCTRL>(ColourSpace::applyYuv) == 0x33);
    CHECK(after<Adc::ADC_BGCTRL>(ColourSpace::applyYuv) == 0x33);

    // The RGB half never wrote them, so a move that starts writing 0 here would
    // be a behaviour change wearing the clothes of an extraction.
    CHECK_FALSE(wasWritten<Adc::ADC_RGCTRL>(ColourSpace::applyRgb));
    CHECK_FALSE(wasWritten<Adc::ADC_GGCTRL>(ColourSpace::applyRgb));
    CHECK_FALSE(wasWritten<Adc::ADC_BGCTRL>(ColourSpace::applyRgb));
}

TEST_CASE("neither half touches the ADC PLL or the capture window")
{
    CHECK_FALSE(wasWritten<Adc::PLLAD_MD>(ColourSpace::applyYuv));
    CHECK_FALSE(wasWritten<Adc::PLLAD_MD>(ColourSpace::applyRgb));
    CHECK_FALSE(wasWritten<InputFormatter::IF_HB_ST2>(ColourSpace::applyYuv));
    CHECK_FALSE(wasWritten<InputFormatter::IF_HB_ST2>(ColourSpace::applyRgb));
}
