// Host-compiled unit tests for src/tv5725/PresetLoad.h -- `make -C test preset-load`.
//
// One flag is all that is left of the class besides the standard byte's own
// constants: whether the output in force is scaling RGBHV. State rather than a
// chip register, because the firmware kept it in an address RD-5725-1.1 does
// not document, where a load cleared it and every reader had to be ordered
// around that.
//
// Not the same question as Tv5725::RgbhvOutput's, which says what the source is
// entitled to rather than what the last load enabled.
// docs/investigations/the-rgbhv-question-is-two-questions.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PresetLoad.h"

using Tv5725::PresetLoad;

TEST_CASE("nothing is scaling RGBHV until a load says so")
{
    PresetLoad::forgetScalingRgbhv();

    CHECK_FALSE(PresetLoad::scalingRgbhvInForce());
}

TEST_CASE("a load that enables scaling RGBHV is remembered")
{
    PresetLoad::forgetScalingRgbhv();

    PresetLoad::rememberScalingRgbhv();

    CHECK(PresetLoad::scalingRgbhvInForce());
}

TEST_CASE("the next load forgets what the last one enabled")
{
    PresetLoad::forgetScalingRgbhv();
    PresetLoad::rememberScalingRgbhv();

    PresetLoad::forgetScalingRgbhv();

    CHECK_FALSE(PresetLoad::scalingRgbhvInForce());
}
