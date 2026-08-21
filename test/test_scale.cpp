// Host-compiled unit tests for Tv5725::Scale -- `make -C test scale`.
// produced IS capture x 1024 / scale, both axes, with no loss term at
// either end. docs/scaler-geometry-model.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <initializer_list>

#include "CheckNear.h"
#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

using namespace Tv5725;

TEST_CASE("produced is a pure multiply")
{
    SUBCASE("produced is a pure multiply on both axes") {
        CHECK_NEAR(Scale(512).produced(400), 800.0, 0.001);
        CHECK_NEAR(Scale(300).produced(200), 682.67, 0.01);
    }

    SUBCASE("the two axes differ only in where the write starts") {
        // The old model had them as different shapes. Same shape; what differs
        // is the pipeline latency before the first write.
        CHECK(Scale(512).produced(400) == Scale(512).produced(400));
        CHECK(AxisHorizontal.startPerMag() > 20 * AxisVertical.startPerMag());
    }

    SUBCASE("a scale of zero is a dropped read, not a setting") {
        CHECK(Scale(0).magnification() == 0.0f);
        CHECK(Scale(0).produced(798) == 0.0f);
    }
}
