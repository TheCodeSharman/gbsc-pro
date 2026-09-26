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

#include "DebugPinStub.h"
uint32_t debugPinPulseTicks() { return ticksForHz(0.0f); }
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
    SourceTiming dmt = SourceTiming::matching(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisHorizontal), 144.0f / 800.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisHorizontal), 640.0f / 800.0f, 0.0005f);
}

TEST_CASE("the vertical axis comes from the same raster")
{
    // 525 lines, 2 of sync and 33 of back porch before 480 of picture, and a
    // counter whose origin is 7 lines after the pulse's leading edge.
    SourceTiming dmt = SourceTiming::matching(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisVertical), 28.0f / 525.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisVertical), 480.0f / 525.0f, 0.0005f);
}

// THE VERTICAL COUNTER'S ORIGIN IS A FIXED DISTANCE AFTER THE VSYNC LEADING
// EDGE, AND IT IS NOT THE PULSE WIDTH. Measured on the RiscPC on vga by forcing
// a 100% framing and reading the card's one-line green frame off the emitted
// picture, which gives the counter line the source's own first active line
// arrives at:
//
//     mode           sync + back porch   arrives at   difference
//     640x480@60          2 + 33 = 35        27.7       -7.3
//     800x600@60          4 + 23 = 27        19.7       -7.3
//     1024x768@60         6 + 29 = 35        27.6       -7.4
//     640x480@75          3 + 16 = 19        11.1       -7.9
//
// Four pulse widths and one difference, so the origin does not follow the
// pulse: what the standard states from the leading edge is where video lands,
// less the origin. Taking the back porch alone instead is right only where the
// pulse happens to be 7 lines, and cost five lines off the top of 640x480@60
// with the source's own blanking shown at the bottom in their place.
// docs/investigations/the-vertical-capture-window-is-placed-late.md
TEST_CASE("the vertical start is the standard's, less the counter's origin")
{
    // 800x600@60 is 628 lines: 4 of sync, 23 of back porch, 600 of picture.
    SourceTiming dmt = SourceTiming::matching(
        SourceKey(627, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive));
    REQUIRE(dmt.published());

    CHECK_NEAR(dmt.activeStart(AxisVertical), 20.0f / 628.0f, 0.0005f);
}

TEST_CASE("a measured rate anywhere in the bucket still matches")
{
    // The engine re-solves on the measured rate and that reading wobbles, so a
    // match on the float misses the raster the source is running.
    CHECK(SourceTiming::matching(SourceKey(627, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK(SourceTiming::matching(SourceKey(627, 59.85f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive)).published());
}

TEST_CASE("two standards on one line count are told apart by the sync width")
{
    // 525 lines at 60 Hz is 640x480 DMT and it is 720x480p, and they put active
    // video 2.2% of the line apart. DMT spends 96 pixels of 800 on sync where
    // CEA spends 62 of 858.
    SourceTiming dmt = SourceTiming::matching(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));
    SourceTiming cea = SourceTiming::matching(SourceKey(524, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    REQUIRE(cea.published());
    CHECK_NEAR(cea.activeStart(AxisHorizontal), 122.0f / 858.0f, 0.0005f);
    CHECK(cea.activeStart(AxisHorizontal) < dmt.activeStart(AxisHorizontal));
}

TEST_CASE("a source running neither standard is left unpublished")
{
    // Placing a source from a raster it is not emitting crops picture, so an
    // unrecognised sync width takes the assumption rather than the nearest row.
    CHECK_FALSE(SourceTiming::matching(SourceKey(524, 59.94f, 0.20f, SourceKey::Negative, SourceKey::Negative)).published());
    CHECK_FALSE(SourceTiming::matching(SourceKey(311, 50.08f, 0.071f, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK_FALSE(SourceTiming::matching(SourceKey(97, 50.08f, 0.12f, SourceKey::Positive, SourceKey::Positive)).published());
}

// The bench RISC PC on AKF50's 800x600@60, measured 2026-09-10: VTOTAL 627,
// PLLAD_MD 1124, HLOW_LEN 137, line rate 37879 -> 60.32 Hz. AKF50's own timings
// are 128,48,40,800,40,0 of 1056 at 40 MHz, so its 800 active pixels start at
// 216 exactly where DMT puts them -- the 40-pixel borders sit in the porches.
TEST_CASE("the bench RISC PC at 800x600@60 is recognised as the published mode")
{
    SourceTiming t = SourceTiming::matching(SourceKey(627, 60.32f, 137.0f / 1124.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeStart(AxisHorizontal), 216.0f / 1056.0f, 0.0005f);
    CHECK_NEAR(t.activeExtent(AxisHorizontal), 800.0f / 1056.0f, 0.0005f);
}

// The bench measured this source's line rate at both 37879 and 38135 Hz within
// one session -- 60.32 Hz and 60.72 Hz over its 628 lines. DMT states 60.317.
TEST_CASE("a field rate that wobbles across half a hertz still finds its mode")
{
    const float duty = 137.0f / 1124.0f;

    CHECK(SourceTiming::matching(SourceKey(627, 60.32f, duty, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK(SourceTiming::matching(SourceKey(627, 60.72f, duty, SourceKey::Positive, SourceKey::Positive)).published());
}

TEST_CASE("the line active video starts on, for a caller that cannot scale")
{
    // Pass-through plays the source's own raster out, so the only thing it can
    // blank correctly is what the raster says is not picture. 720x480p spends
    // 6 lines on sync and 30 on back porch. THE BLANKING IS PLACED AGAINST THE
    // HD CHANNEL'S COUNTER, not the input formatter's, and where that one
    // zeroes has not been measured -- so it is taken as the pulse's end and
    // active video is line 30 of 525.
    const SourceTiming cea = SourceTiming::matching(SourceKey(524, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));
    REQUIRE(cea.published());

    CHECK(cea.activeStartLine(525) == 30);
}

TEST_CASE("a source matching no raster names no line to blank to")
{
    // Blanking any of it would be a guess at the picture's expense, and the
    // source's own porches are already black.
    const SourceTiming unknown(51.3f);
    REQUIRE_FALSE(unknown.published());

    CHECK(unknown.activeStartLine(311) == 0);
}
