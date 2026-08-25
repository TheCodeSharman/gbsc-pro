// Host-compiled unit tests for Tv5725::SourceTiming -- `make -C test source-timing`.
//
// Where a published raster says active video sits, for the sources that emit
// one. docs/investigations/vesa-modes-are-clipped-by-default.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CheckNear.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"

float getSourceFieldRate(bool) { return 0.0f; }
void tv5725Log(const char *) {}

using namespace Tv5725;

TEST_CASE("a source matching no published raster carries only its rate")
{
    SourceTiming measured(50.08f);

    CHECK_FALSE(measured.published());
    CHECK_NEAR(measured.fieldRateHz(), 50.08f, 0.001f);
}

TEST_CASE("a VESA source is placed where its own raster puts active video")
{
    // 640x480@60: 800 pixels, 96 sync and 48 back porch before 640 of picture.
    // 524 rather than 525 because the sync processor counts from zero, which is
    // what the bench reads on a source running this mode.
    SourceTiming dmt = SourceTiming::matching(524, 59.94f, 96.0f / 800.0f);

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisHorizontal), 144.0f / 800.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisHorizontal), 640.0f / 800.0f, 0.0005f);
}

TEST_CASE("the vertical axis comes from the same raster")
{
    // 525 lines, 2 of sync and 33 of back porch before 480 of picture.
    SourceTiming dmt = SourceTiming::matching(524, 59.94f, 96.0f / 800.0f);

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisVertical), 35.0f / 525.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisVertical), 480.0f / 525.0f, 0.0005f);
}

TEST_CASE("a measured rate anywhere in the bucket still matches")
{
    // The engine re-solves on the measured rate and that reading wobbles, so a
    // match on the float misses the raster the source is running.
    CHECK(SourceTiming::matching(627, 60.32f, 128.0f / 1056.0f).published());
    CHECK(SourceTiming::matching(627, 59.85f, 128.0f / 1056.0f).published());
}

TEST_CASE("two standards on one line count are told apart by the sync width")
{
    // 525 lines at 60 Hz is 640x480 DMT and it is 720x480p, and they put active
    // video 2.2% of the line apart. DMT spends 96 pixels of 800 on sync where
    // CEA spends 62 of 858.
    SourceTiming dmt = SourceTiming::matching(524, 59.94f, 96.0f / 800.0f);
    SourceTiming cea = SourceTiming::matching(524, 59.94f, 62.0f / 858.0f);

    REQUIRE(dmt.published());
    REQUIRE(cea.published());
    CHECK_NEAR(cea.activeStart(AxisHorizontal), 122.0f / 858.0f, 0.0005f);
    CHECK(cea.activeStart(AxisHorizontal) < dmt.activeStart(AxisHorizontal));
}

TEST_CASE("a source running neither standard is left unpublished")
{
    // Placing a source from a raster it is not emitting crops picture, so an
    // unrecognised sync width takes the assumption rather than the nearest row.
    CHECK_FALSE(SourceTiming::matching(524, 59.94f, 0.20f).published());
    CHECK_FALSE(SourceTiming::matching(311, 50.08f, 0.071f).published());
    CHECK_FALSE(SourceTiming::matching(97, 50.08f, 0.12f).published());
}
