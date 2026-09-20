// Host-compiled unit tests for src/tv5725/RgbhvOutput.h -- `make -C test rgbhv-output`.
//
// Whether a scaled mode is established for a source Mode Detect names nothing
// for. The standard byte says the source is RGBHV; this says what it is getting,
// which is an output question and not a classification of the source.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/RgbhvOutput.h"

using Tv5725::RgbhvOutput;

TEST_CASE("an RGBHV source is bypassed until a scaled mode is established")
{
    RgbhvOutput::chooseBypass();

    CHECK(RgbhvOutput::isScaling() == false);
}

TEST_CASE("establishing a scaled mode is what the steering reads")
{
    RgbhvOutput::chooseBypass();
    RgbhvOutput::chooseScaling();

    CHECK(RgbhvOutput::isScaling());
}
