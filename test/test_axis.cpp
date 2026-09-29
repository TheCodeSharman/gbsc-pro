// Host-compiled unit tests for Tv5725::Axis -- `make -C test axis`.
//
// What the CAPTURE path does on one axis: the grid a window may move on, and
// the step a press turns into. Where the picture LANDS is OutputWindow's, and
// so are its tests. docs/firmware-geometry-engine.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

using namespace Tv5725;

// A press asks in OUTPUT PIXELS and the capture moves in its own units, so a
// step is the nearest move the hardware can make to what was asked. The
// horizontal position is effective only in steps of 2 IF units and the vertical
// moves on every one. docs/scaler-geometry-model.md
TEST_CASE("a press moves as near as the hardware can to what it asked for")
{
    // 1024/606, the magnification these steps are checked at.
    const float measured = Scale(606).magnification();

    SUBCASE("every horizontal step is a whole granule") {
        CHECK(AxisHorizontal.captureGranularity() == 2);
        for (int16_t pixels = 1; pixels <= 40; ++pixels)
            CHECK(AxisHorizontal.stepUnits(pixels, measured)
                  % AxisHorizontal.captureGranularity() == 0);
    }

    SUBCASE("a bigger request keeps its size rather than being rounded up") {
        // The pads ask for 8 output pixels, which is 4.73 units here. Nearest
        // granule is 4 -- 6.8 px -- not 6, which would overshoot by more than
        // rounding down undershoots.
        CHECK(AxisHorizontal.stepUnits(8, measured) == 4);
        CHECK(AxisVertical.stepUnits(8, Scale(487).magnification()) == 4);
    }

    // The step is what it asked for or nothing, never a granule the press did
    // not ask for: the smallest horizontal move is two units, which at the
    // bench magnification is five output pixels, and rounding a one-pixel press
    // up to it moves the picture five times as far as it was told to.
    SUBCASE("and a request under half a granule moves nothing") {
        CHECK(AxisHorizontal.stepUnits(1, measured) == 0);
        CHECK(AxisHorizontal.stepUnits(-1, measured) == 0);
        CHECK(AxisVertical.stepUnits(1, Scale(487).magnification()) == 0);
        CHECK(AxisVertical.stepUnits(-1, Scale(487).magnification()) == 0);
    }

    SUBCASE("a request the axis can reach is taken whole") {
        CHECK(AxisVertical.captureGranularity() == 1);
        CHECK(AxisVertical.stepUnits(2, Scale(487).magnification()) == 1);
        CHECK(AxisHorizontal.stepUnits(3, measured) == 2);
    }
}
