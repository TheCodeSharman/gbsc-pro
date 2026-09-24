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

// The horizontal capture position is effective only in steps of 2 IF units, so
// a press that rounds to 1 moves the register without moving the picture.
// Vertical moves on every unit. docs/scaler-geometry-model.md
TEST_CASE("one step is visible to the user")
{
    // 1024/606, the magnification these steps are checked at.
    const float measured = Scale(606).magnification();

    SUBCASE("one horizontal tap moves a whole granule") {
        CHECK(AxisHorizontal.captureGranularity() == 2);
        CHECK(AxisHorizontal.stepUnits(1, measured) == 2);
        CHECK(AxisHorizontal.stepUnits(-1, measured) == -2);
    }

    SUBCASE("every horizontal step is a multiple of the granule") {
        for (int16_t pixels = 1; pixels <= 40; ++pixels) {
            int16_t units = AxisHorizontal.stepUnits(pixels, measured);
            CHECK(units >= AxisHorizontal.captureGranularity());
            CHECK(units % AxisHorizontal.captureGranularity() == 0);
        }
    }

    SUBCASE("a bigger request keeps its size rather than being rounded up") {
        // The pads ask for 8 output pixels, which is 4.73 units here. Nearest
        // granule is 4 -- 6.8 px -- not 6, which would overshoot by more than
        // rounding down undershoots.
        CHECK(AxisHorizontal.stepUnits(8, measured) == 4);
    }

    SUBCASE("the vertical axis moves at least one line") {
        const float vertical = Scale(487).magnification();
        CHECK(AxisVertical.captureGranularity() == 1);
        CHECK(AxisVertical.stepUnits(1, vertical) == 1);
        CHECK(AxisVertical.stepUnits(-1, vertical) == -1);
        CHECK(AxisVertical.stepUnits(8, vertical) == 4);
    }
}
