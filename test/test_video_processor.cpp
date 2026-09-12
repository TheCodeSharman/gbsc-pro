// Host-compiled unit tests for the VideoProcessor picture controls --
// `make -C test video-processor`.
//
// Every register here is a BYPS bit, so each is the inverse of the preference
// that names it. Getting the inversion backwards leaves a picture that is
// merely a bit different rather than obviously wrong.
//
// "Not written" is proved by running under two COMPLEMENTARY poisons and
// checking the two disagree. One poison cannot tell a field written 0 from a
// field left at a poison whose bit is already 0.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

static const uint8_t Poisons[2] = {0xA5, 0x5A};

template <typename Field>
static uint32_t applied(void (*control)(bool), bool wanted)
{
    Wire.reset();
    Wire.poison(Poisons[0]);
    control(wanted);
    return Field::read();
}

template <typename Field>
static bool wasWritten(void (*control)(bool), bool wanted)
{
    uint32_t under[2];
    for (int i = 0; i < 2; ++i) {
        Wire.reset();
        Wire.poison(Poisons[i]);
        control(wanted);
        under[i] = Field::read();
    }
    return under[0] == under[1];
}

TEST_CASE("wanting the line filter clears the bypass that would defeat it")
{
    CHECK(applied<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setLineFilter, true) == 0);
    CHECK(applied<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setLineFilter, false) == 1);
}

TEST_CASE("wanting peaking clears the bypass that would defeat it")
{
    CHECK(applied<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setPeaking, true) == 0);
    CHECK(applied<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setPeaking, false) == 1);
}

TEST_CASE("wanting the chroma step response clears the bypass that would defeat it")
{
    CHECK(applied<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setStepResponse, true) == 0);
    CHECK(applied<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setStepResponse, false) == 1);
}

TEST_CASE("wanting the six-tap filter clears the bypass that would defeat it")
{
    CHECK(applied<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setSixTapFilter, true) == 0);
    CHECK(applied<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setSixTapFilter, false) == 1);
}

TEST_CASE("each control writes its own register and no other")
{
    CHECK(wasWritten<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setLineFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setLineFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setLineFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setLineFilter, true));

    CHECK(wasWritten<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setPeaking, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setPeaking, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setPeaking, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setPeaking, true));

    CHECK(wasWritten<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setStepResponse, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setStepResponse, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setStepResponse, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setStepResponse, true));

    CHECK(wasWritten<VideoProcessor::VDS_TAP6_BYPS>(VideoProcessor::setSixTapFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_D_RAM_BYPS>(VideoProcessor::setSixTapFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_PK_Y_H_BYPS>(VideoProcessor::setSixTapFilter, true));
    CHECK_FALSE(wasWritten<VideoProcessor::VDS_UV_STEP_BYPS>(VideoProcessor::setSixTapFilter, true));
}

// --- the 422/444 conversion delays, which follow the scan mode ----------------

TEST_CASE("the chroma conversion delay follows the scan mode")
{
    // VDS_V_DELAY realigns V through the 422-to-444 conversion, and how far it
    // has to move depends on whether the line doubler is in the path. Measured:
    // 0 on the 15 kHz RGBHV bench raster, 1 on component 480p.
    Wire.reset();
    Wire.poison(Poisons[0]);
    VideoProcessor::applyScanMode(false, false);
    CHECK(Wire.field(3, 0x24, 2, 1) == 1);

    VideoProcessor::applyScanMode(true, false);
    CHECK(Wire.field(3, 0x24, 2, 1) == 0);
}

TEST_CASE("only a line-doubled RGB source shortens the luma delay")
{
    // VDS_Y_DELAY compensates the filter's own luma delay. A component source
    // needs the full compensation whatever the scan mode; an RGB source needs
    // it only where the doubler is out of the path.
    Wire.reset();
    Wire.poison(Poisons[0]);

    VideoProcessor::applyScanMode(true, false);
    CHECK(Wire.field(3, 0x24, 4, 2) == 2);

    VideoProcessor::applyScanMode(true, true);
    CHECK(Wire.field(3, 0x24, 4, 2) == 3);

    VideoProcessor::applyScanMode(false, false);
    CHECK(Wire.field(3, 0x24, 4, 2) == 3);
}
