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
    // 525 lines, 2 of sync and 33 of back porch before 480 of picture, counted
    // from the pulse's leading edge as the horizontal is. Where the counter's
    // origin sits after that edge is VideoSourceLine's.
    SourceTiming dmt = SourceTiming::matching(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisVertical), 35.0f / 525.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisVertical), 480.0f / 525.0f, 0.0005f);
}

TEST_CASE("the vertical start is the standard's own, whatever the pulse")
{
    // 800x600@60 is 628 lines: 4 of sync, 23 of back porch, 600 of picture.
    SourceTiming dmt = SourceTiming::matching(
        SourceKey(627, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive));
    REQUIRE(dmt.published());

    CHECK_NEAR(dmt.activeStart(AxisVertical), 27.0f / 628.0f, 0.0005f);
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
    CHECK_FALSE(SourceTiming::matching(SourceKey(311, 50.08f, 0.140f, SourceKey::Positive, SourceKey::Positive)).published());
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

// AKF50's 15.6 kHz PAL family: 512 pixel clocks at 8 MHz, 36 of sync, 30 of
// back porch and a 44-pixel border either side of 320 of picture; vertically
// 312 lines, 3 of sync, 16 of back porch and a 17-line border either side of
// 256. The tier states the PICTURE, not the border around it, so a source
// running one of these opens filling the screen rather than showing its own
// border as black.
TEST_CASE("an Acorn 15 kHz mode is placed on the picture inside its border")
{
    SourceTiming akf = SourceTiming::matching(
        SourceKey(311, 50.08f, 36.0f / 512.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 110.0f / 512.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisHorizontal), 320.0f / 512.0f, 0.0005f);
}

TEST_CASE("the Acorn mode's vertical picture starts inside its top border")
{
    SourceTiming akf = SourceTiming::matching(
        SourceKey(311, 50.08f, 36.0f / 512.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisVertical), 36.0f / 312.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisVertical), 256.0f / 312.0f, 0.0005f);
}

TEST_CASE("an Acorn mode no standard states is published from the mode file")
{
    // AKF50's 640x200@60, 262 lines at 15.7 kHz: nothing in DMT or CEA runs
    // 262 lines, so the mode file is the only thing that can say where its
    // picture sits inside the borders.
    SourceTiming akf = SourceTiming::matching(
        SourceKey(261, 60.0f, 72.0f / 1020.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 234.0f / 1020.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisHorizontal), 640.0f / 1020.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisVertical), 200.0f / 262.0f, 0.0005f);
}

TEST_CASE("a standard's raster wins over a mode file describing the same one")
{
    // AKF50 states 640x480@60 as well, spending 94 pixels of 800 on sync where
    // DMT spends 96 -- a quarter of a point apart, which is inside what the
    // sync width can be measured to. A source running either reads as both, and
    // what it is EMITTING is the standard: the mode file is one machine's
    // description of it, and its borders are placed differently.
    SourceTiming akf = SourceTiming::matching(
        SourceKey(524, 60.0f, 94.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 144.0f / 800.0f, 0.0005f);
}

// VESA DMT 640x350@85 and 640x400@85 share one horizontal raster, one frame of
// 445 lines and one field rate, and differ in the polarity pair alone: +H -V
// against -H +V. The polarity is the whole of what tells them apart, and the
// standard keyed them on it deliberately.
TEST_CASE("two rasters differing only in polarity are told apart by it")
{
    const float duty = 64.0f / 832.0f;

    SourceTiming lines350 = SourceTiming::matching(
        SourceKey(444, 85.08f, duty, SourceKey::Positive, SourceKey::Negative));
    SourceTiming lines400 = SourceTiming::matching(
        SourceKey(444, 85.08f, duty, SourceKey::Negative, SourceKey::Positive));

    REQUIRE(lines350.published());
    REQUIRE(lines400.published());
    CHECK_NEAR(lines350.activeExtent(AxisVertical), 350.0f / 445.0f, 0.0005f);
    CHECK_NEAR(lines400.activeExtent(AxisVertical), 400.0f / 445.0f, 0.0005f);
}

TEST_CASE("an undetermined polarity still finds the first raster on the key")
{
    // Composite sync and sync on green state no polarity, and a source on
    // either is still emitting a raster the standards state.
    SourceTiming t = SourceTiming::matching(
        SourceKey(444, 85.08f, 64.0f / 832.0f, SourceKey::Undetermined, SourceKey::Undetermined));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeExtent(AxisVertical), 350.0f / 445.0f, 0.0005f);
}

TEST_CASE("a polarity no raster on the key carries still finds the first one")
{
    // A source that has the timing of a standard and inverts one pulse is
    // still placed from that standard rather than from the envelope.
    SourceTiming t = SourceTiming::matching(
        SourceKey(444, 85.08f, 64.0f / 832.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeExtent(AxisVertical), 350.0f / 445.0f, 0.0005f);
}
