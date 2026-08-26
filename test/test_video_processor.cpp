// Host-compiled unit tests for VideoProcessor::applyPictureOptions() --
// `make -C test video-processor`.
//
// The registers are BYPS bits, so every one of them is the inverse of the
// preference that names it. That inversion is the whole content of the
// function, and getting it backwards leaves a picture that is merely a bit
// different rather than obviously wrong.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

static void apply(bool lineFilter, bool peaking, bool stepResponse)
{
    Wire.reset();
    Wire.poison(0xA5);
    VideoProcessor::applyPictureOptions(lineFilter, peaking, stepResponse);
}

TEST_CASE("wanting an option clears the bypass that would defeat it")
{
    apply(true, true, true);

    CHECK(VideoProcessor::VDS_D_RAM_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_PK_Y_H_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_UV_STEP_BYPS::read() == 0);
}

TEST_CASE("declining an option sets its bypass")
{
    apply(false, false, false);

    CHECK(VideoProcessor::VDS_D_RAM_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_PK_Y_H_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_UV_STEP_BYPS::read() == 1);
}

TEST_CASE("each preference reaches its own register and no other")
{
    apply(true, false, false);
    CHECK(VideoProcessor::VDS_D_RAM_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_PK_Y_H_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_UV_STEP_BYPS::read() == 1);

    apply(false, true, false);
    CHECK(VideoProcessor::VDS_D_RAM_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_PK_Y_H_BYPS::read() == 0);
    CHECK(VideoProcessor::VDS_UV_STEP_BYPS::read() == 1);

    apply(false, false, true);
    CHECK(VideoProcessor::VDS_D_RAM_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_PK_Y_H_BYPS::read() == 1);
    CHECK(VideoProcessor::VDS_UV_STEP_BYPS::read() == 0);
}

TEST_CASE("the six-tap filter is not a preference and is never bypassed")
{
    apply(false, false, false);

    CHECK(VideoProcessor::VDS_TAP6_BYPS::read() == 0);
}
