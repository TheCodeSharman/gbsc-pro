// Host-compiled unit tests for Tv5725::ColourBalance -- `make -C test colour-balance`.
//
// The user's picture colour, held in the basis the menu shows -- a red, green
// and blue balance and a luma gain -- and written as the three YUV offsets and
// the gain the chip takes. docs/osd-menu.md
//
// The chain derived the balance by reading the offsets back, which lost a step
// to rounding every time and lost the lot whenever a colour space change
// rewrote them.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ColourBalance.h"

using namespace Tv5725;

static const uint8_t Poisons[2] = {0xA5, 0x5A};

// The offsets are signed bytes in unsigned fields.
static int8_t offsetY() { return (int8_t)VideoProcessor::VDS_Y_OFST::read(); }
static int8_t offsetU() { return (int8_t)VideoProcessor::VDS_U_OFST::read(); }
static int8_t offsetV() { return (int8_t)VideoProcessor::VDS_V_OFST::read(); }

static ColourBalance applied(const ColourBalance &balance)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    balance.apply();
    return balance;
}

TEST_CASE("a neutral balance writes the rest the colour space asked for")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);
    applied(balance);

    CHECK(offsetY() == 0x0E);
    CHECK(offsetU() == 0x03);
    CHECK(offsetV() == 0x04);
    CHECK(VideoProcessor::VDS_Y_GAIN::read() == 0x80);
}

TEST_CASE("a neutral balance reads as neutral whatever the rest is")
{
    // The chain's value was derived from the registers, so a component source
    // rested at 147/138/147 and an RGB one at 128/128/128 -- the same picture
    // reported two ways.
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);

    CHECK(balance.red() == ColourBalance::Neutral);
    CHECK(balance.green() == ColourBalance::Neutral);
    CHECK(balance.blue() == ColourBalance::Neutral);
    CHECK(balance.lumaGain() == ColourBalance::Neutral);
}

TEST_CASE("more red lifts luma and V and drops U")
{
    // The matrix is the one the sketch mixed by hand: Y 0.299, U -0.169,
    // V 0.500 per unit of red.
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 20; ++i)
        balance.nudgeRed(+1);
    applied(balance);

    CHECK(balance.red() == ColourBalance::Neutral + 20);
    CHECK(offsetY() == 5);
    CHECK(offsetU() == -3);
    CHECK(offsetV() == 10);
}

TEST_CASE("more green lifts luma and drops both chroma offsets")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 20; ++i)
        balance.nudgeGreen(+1);
    applied(balance);

    CHECK(offsetY() == 11);
    CHECK(offsetU() == -6);
    CHECK(offsetV() == -8);
}

TEST_CASE("more blue lifts luma and U and drops V")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 20; ++i)
        balance.nudgeBlue(+1);
    applied(balance);

    CHECK(offsetY() == 2);
    CHECK(offsetU() == 10);
    CHECK(offsetV() == -1);
}

TEST_CASE("the opposite direction puts the offsets back")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);
    for (int i = 0; i < 9; ++i)
        balance.nudgeBlue(+1);
    balance.nudgeRed(-4);
    for (int i = 0; i < 9; ++i)
        balance.nudgeBlue(-1);
    balance.nudgeRed(+4);
    applied(balance);

    CHECK(balance.red() == ColourBalance::Neutral);
    CHECK(offsetY() == 0x0E);
    CHECK(offsetU() == 0x03);
    CHECK(offsetV() == 0x04);
}

TEST_CASE("the balance outlives the rest it was set against")
{
    // A colour space change is what lost the chain's setting: it rewrote the
    // offsets and the balance was whatever they then read back as.
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 20; ++i)
        balance.nudgeRed(+1);

    balance.restFor(ColourBalance::Component);
    applied(balance);

    CHECK(balance.red() == ColourBalance::Neutral + 20);
    CHECK(offsetY() == 0x0E + 5);
    CHECK(offsetV() == 0x04 + 10);
}

TEST_CASE("a balance stops at the ends rather than wrapping round them")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 200; ++i)
        balance.nudgeRed(+1);

    CHECK(balance.red() == 255);
}

TEST_CASE("an offset clamps rather than wrapping into the opposite sign")
{
    // The offsets are signed bytes: a wrap turns a bright picture dark, and the
    // balance can ask for more than the field holds.
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);
    balance.nudgeRed(+127);
    balance.nudgeGreen(+127);
    balance.nudgeBlue(+127);
    applied(balance);

    // The three together are a unit of luma each, so 127 of them plus a rest of
    // 14 asks for 141 out of a signed byte.
    CHECK(offsetY() == 127);
}

TEST_CASE("the luma gain steps from the rest the colour space asked for")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    for (int i = 0; i < 16; ++i)
        balance.nudgeLumaGain(+1);
    applied(balance);

    CHECK(balance.lumaGain() == ColourBalance::Neutral + 16);
    CHECK(VideoProcessor::VDS_Y_GAIN::read() == 0x90);
}

TEST_CASE("resetting returns every axis to neutral")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);
    balance.nudgeRed(+10);
    balance.nudgeLumaGain(-10);

    balance.reset();
    applied(balance);

    CHECK(balance.red() == ColourBalance::Neutral);
    CHECK(balance.lumaGain() == ColourBalance::Neutral);
    CHECK(offsetY() == 0x0E);
}

TEST_CASE("a balance adopts a stored set without stepping to it")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Rgb);
    balance.adopt(140, 120, 128, 130);

    CHECK(balance.red() == 140);
    CHECK(balance.green() == 120);
    CHECK(balance.blue() == 128);
    CHECK(balance.lumaGain() == 130);
}

TEST_CASE("it writes the four registers it owns and no others")
{
    ColourBalance balance;
    balance.restFor(ColourBalance::Component);

    uint32_t chroma[2];
    uint32_t luma[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        balance.apply();
        chroma[i] = VideoProcessor::VDS_UCOS_GAIN::read();
        luma[i] = VideoProcessor::VDS_Y_GAIN::read();
    }

    CHECK(luma[0] == luma[1]);
    CHECK(chroma[0] != chroma[1]);
}
