// Host-compiled unit tests for Tv5725::Aspect -- `make -C test aspect`.
//
// The shape a picture is shown in, and how far the room has to narrow for the
// raster to show it. Pure arithmetic, and the only place it is stated: every
// bar on the screen comes out of these two fractions. docs/aspect-ratio.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CheckNear.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Aspect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"

using namespace Tv5725;

TEST_CASE("a shape matching the raster's narrows neither axis")
{
    const Aspect wide(Aspect::SixteenNine);

    CHECK_NEAR(wide.roomFraction(AxisHorizontal, Aspect(Aspect::SixteenNine)), 1.0f, 0.0001f);
    CHECK_NEAR(wide.roomFraction(AxisVertical, Aspect(Aspect::SixteenNine)), 1.0f, 0.0001f);
}

TEST_CASE("a picture narrower than the raster loses width, and keeps its height")
{
    // 4:3 into 1080p: 1440 of the emitted 1920 columns, a bar of 240 each side.
    const Aspect square(Aspect::FourThree);

    CHECK_NEAR(square.roomFraction(AxisHorizontal, Aspect(Aspect::SixteenNine)),
               13333.0f / 17778.0f, 0.0001f);
    CHECK_NEAR(square.roomFraction(AxisVertical, Aspect(Aspect::SixteenNine)), 1.0f, 0.0001f);
}

TEST_CASE("a picture wider than the raster loses height, and keeps its width")
{
    // 16:9 into 960p, which is a 4:3 raster: 720 of its 960 lines.
    const Aspect wide(Aspect::SixteenNine);

    CHECK_NEAR(wide.roomFraction(AxisVertical, Aspect(Aspect::FourThree)),
               13333.0f / 17778.0f, 0.0001f);
    CHECK_NEAR(wide.roomFraction(AxisHorizontal, Aspect(Aspect::FourThree)), 1.0f, 0.0001f);
}

TEST_CASE("fill asks for the whole raster whichever way round it is stated")
{
    // Pass-through has no raster and so no shape, and a user asking to fill has
    // declined to state one. Neither may narrow anything.
    CHECK_NEAR(Aspect(Aspect::Fill).roomFraction(AxisHorizontal, Aspect(Aspect::SixteenNine)),
               1.0f, 0.0001f);
    CHECK_NEAR(Aspect(Aspect::FourThree).roomFraction(AxisHorizontal, Aspect(Aspect::Fill)),
               1.0f, 0.0001f);
    CHECK_NEAR(Aspect(Aspect::FourThree).roomFraction(AxisVertical, Aspect(Aspect::Fill)),
               1.0f, 0.0001f);
}

TEST_CASE("a 5:4 picture on a widescreen raster is narrower still than a 4:3 one")
{
    const float five = Aspect(Aspect::FiveFour).roomFraction(AxisHorizontal, Aspect(Aspect::SixteenNine));
    const float four = Aspect(Aspect::FourThree).roomFraction(AxisHorizontal, Aspect(Aspect::SixteenNine));

    CHECK(five < four);
    CHECK_NEAR(five, 12500.0f / 17778.0f, 0.0001f);
}

TEST_CASE("a default-constructed shape fills")
{
    CHECK(Aspect().fills());
    CHECK(Aspect(Aspect::FourThree) != Aspect());
}
