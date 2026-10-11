// Host-compiled unit tests for Audio::LineVolume -- `make -C test line-volume`.
// The setting IS decibels of attenuation, so 0 is the loudest the PT2257 can be
// asked for. docs/audio-path.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/audio/LineVolume.h"

using namespace Audio;

TEST_CASE("the loudest setting asks for no attenuation")
{
    CHECK(LineVolume::attenuationDb(0) == 0);
}

TEST_CASE("the quietest setting asks for the whole span")
{
    CHECK(LineVolume::attenuationDb(LineVolume::Maximum) == LineVolume::Maximum);
}

TEST_CASE("a setting past the maximum clamps rather than wrapping")
{
    CHECK(LineVolume::attenuationDb(207) == LineVolume::Maximum);
}

TEST_CASE("a fresh unit leaves headroom for a hot source")
{
    CHECK(LineVolume::Default > 0);
    CHECK(LineVolume::Default < LineVolume::Maximum);
}

TEST_CASE("the overlay reads louder as a larger number")
{
    CHECK(LineVolume::displayLevel(0) == LineVolume::Maximum);
    CHECK(LineVolume::displayLevel(LineVolume::Maximum) == 0);
}

TEST_CASE("stepping stops at either end")
{
    CHECK(LineVolume::louder(0) == 0);
    CHECK(LineVolume::quieter(LineVolume::Maximum) == LineVolume::Maximum);
    CHECK(LineVolume::louder(12) == 11);
    CHECK(LineVolume::quieter(12) == 13);
}
