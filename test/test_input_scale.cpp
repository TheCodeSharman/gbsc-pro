// Host-compiled unit tests for Tv5725::InputScale -- `make -C test input-scale`.
//
// The input formatter's own scaling-down block, which is the only minification
// the part has: VDS_?SCALE divides 1024 and tops out at 1023, so the video
// display scaler's produced picture is never narrower than its capture.
// docs/scaling-down-path.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CheckNear.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputScale.h"

using namespace Tv5725;

TEST_CASE("no scaling is an increment of zero")
{
    const InputScale none;

    CHECK(none.increment() == 0);
    CHECK_FALSE(none.minifies());
    CHECK(none.unitsFor(1444) == 1444);
}

// RD-5725-1.1 states the increment for a scaling ratio n/m as
// 4095 x (m - n) / n, so a ratio of one half is the whole twelve bits.
TEST_CASE("the increment is the datasheet's, for the ratio it is asked for")
{
    CHECK(InputScale::forRatio(1, 1).increment() == 0);
    CHECK(InputScale::forRatio(3, 4).increment() == 1365);
    CHECK(InputScale::forRatio(1, 2).increment() == 4095);
}

TEST_CASE("the ratio a given increment gives is 4095 over 4095 plus it")
{
    CHECK_NEAR(InputScale(0).ratio(), 1.0f, 0.0005f);
    CHECK_NEAR(InputScale(1365).ratio(), 0.75f, 0.0005f);
    CHECK_NEAR(InputScale(4095).ratio(), 0.5f, 0.0005f);
}

// Half is the whole twelve bits, so nothing below it is reachable without the
// coarse factor -- which the line doubler already owns, and which cannot carry
// two meanings at once.
TEST_CASE("a ratio under a half is refused rather than wrapped")
{
    CHECK(InputScale::forRatio(1, 3).increment() == 4095);
    CHECK_NEAR(InputScale::forRatio(1, 3).ratio(), 0.5f, 0.0005f);
}

TEST_CASE("a ratio over unity does not magnify, the block only scaling down")
{
    CHECK(InputScale::forRatio(5, 4).increment() == 0);
    CHECK_FALSE(InputScale::forRatio(5, 4).minifies());
}

TEST_CASE("a zero term asks for nothing rather than dividing by it")
{
    CHECK(InputScale::forRatio(0, 800).increment() == 0);
    CHECK(InputScale::forRatio(800, 0).increment() == 0);
}

// What the block does to the line: the source arrives compressed into fewer IF
// units, so every count placed in those units follows it -- the capture window,
// and the counter the line wraps at. docs/scaling-down-path.md
TEST_CASE("a count in IF units follows the ratio")
{
    const InputScale threeQuarters = InputScale::forRatio(3, 4);

    CHECK(threeQuarters.unitsFor(1444) == 1083);
    CHECK(threeQuarters.unitsFor(800) == 600);
    CHECK(threeQuarters.unitsFor(0) == 0);
}

// The reason the block is worth having: a capture wider than the room has no
// way to reach a declared shape, because the part cannot minify.
// docs/aspect-ratio.md
TEST_CASE("the ratio that fits a capture into a narrower room")
{
    // 800x600@60 into 1080p: 4:3 asks for 1042 output units from a capture of
    // 1090, which is a ratio of 0.956 and an increment of 187.
    const InputScale fitted = InputScale::forRatio(1042, 1090);

    CHECK(fitted.increment() == 188);
    CHECK(fitted.minifies());
    CHECK(fitted.unitsFor(1090) == 1042);
}

// The eight segments are a NON-LINEAR scaler -- each eighth of the line may
// scale differently, which is what an anamorphic stretch wants. One value on
// all eight is the linear case, and the only one the engine asks for.
TEST_CASE("every segment carries the same increment, which is the linear case")
{
    const InputScale threeQuarters = InputScale::forRatio(3, 4);

    CHECK(threeQuarters.segment() == 1365 >> 4);
    CHECK(threeQuarters.low() == (1365 & 0x0F));
}
