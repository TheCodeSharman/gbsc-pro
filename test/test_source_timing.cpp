// Host-compiled unit tests for Tv5725::SourceTiming -- `make -C test source-timing`.
//
// Where a published raster says active video sits, for the sources that emit
// one. docs/investigations/vesa-modes-are-clipped-by-default.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "CheckNear.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Aspect.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"

#include "DebugPinStub.h"
uint32_t debugPinSpanTicks(uint32_t pulses)
{
    return pulses * ticksForHz(0.0f);
}
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
    // The frame, which is what a key carries: the sync processor counts from
    // zero and SyncProcessor::frameLines() corrects it where it is read.
    SourceTiming dmt = SourceTiming::matching(SourceKey(525, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisHorizontal), 144.0f / 800.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisHorizontal), 640.0f / 800.0f, 0.0005f);
}

TEST_CASE("the vertical axis comes from the same raster")
{
    // 525 lines, 2 of sync and 33 of back porch before 480 of picture, counted
    // from the pulse's leading edge as the horizontal is. Where the counter's
    // origin sits after that edge is VideoSourceLine's.
    SourceTiming dmt = SourceTiming::matching(SourceKey(525, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    CHECK_NEAR(dmt.activeStart(AxisVertical), 35.0f / 525.0f, 0.0005f);
    CHECK_NEAR(dmt.activeExtent(AxisVertical), 480.0f / 525.0f, 0.0005f);
}

TEST_CASE("the vertical start is the standard's own, whatever the pulse")
{
    // 800x600@60 is 628 lines: 4 of sync, 23 of back porch, 600 of picture.
    SourceTiming dmt = SourceTiming::matching(
        SourceKey(628, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive));
    REQUIRE(dmt.published());

    CHECK_NEAR(dmt.activeStart(AxisVertical), 27.0f / 628.0f, 0.0005f);
}

TEST_CASE("a measured rate anywhere in the bucket still matches")
{
    // The engine re-solves on the measured rate and that reading wobbles, so a
    // match on the float misses the raster the source is running.
    CHECK(SourceTiming::matching(SourceKey(628, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK(SourceTiming::matching(SourceKey(628, 59.85f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive)).published());
}

TEST_CASE("two standards on one line count are told apart by the sync width")
{
    // 525 lines at 60 Hz is 640x480 DMT and it is 720x480p, and they put active
    // video 2.2% of the line apart. DMT spends 96 pixels of 800 on sync where
    // CEA spends 62 of 858.
    SourceTiming dmt = SourceTiming::matching(SourceKey(525, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));
    SourceTiming cea = SourceTiming::matching(SourceKey(525, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(dmt.published());
    REQUIRE(cea.published());
    CHECK_NEAR(cea.activeStart(AxisHorizontal), 122.0f / 858.0f, 0.0005f);
    CHECK(cea.activeStart(AxisHorizontal) < dmt.activeStart(AxisHorizontal));
}

TEST_CASE("a source running neither standard is left unpublished")
{
    // Placing a source from a raster it is not emitting crops picture, so an
    // unrecognised sync width takes the assumption rather than the nearest row.
    CHECK_FALSE(SourceTiming::matching(SourceKey(525, 59.94f, 0.20f, SourceKey::Negative, SourceKey::Negative)).published());
    CHECK_FALSE(SourceTiming::matching(SourceKey(312, 50.08f, 0.140f, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK_FALSE(SourceTiming::matching(SourceKey(98, 50.08f, 0.12f, SourceKey::Positive, SourceKey::Positive)).published());
}

// The bench RISC PC on AKF50's 800x600@60, measured 2026-09-10: VTOTAL 627,
// PLLAD_MD 1124, HLOW_LEN 137, line rate 37879 -> 60.32 Hz. AKF50's own timings
// are 128,48,40,800,40,0 of 1056 at 40 MHz, so its 800 active pixels start at
// 216 exactly where DMT puts them -- the 40-pixel borders sit in the porches.
TEST_CASE("the bench RISC PC at 800x600@60 is recognised as the published mode")
{
    SourceTiming t = SourceTiming::matching(SourceKey(628, 60.32f, 137.0f / 1124.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeStart(AxisHorizontal), 216.0f / 1056.0f, 0.0005f);
    CHECK_NEAR(t.activeExtent(AxisHorizontal), 800.0f / 1056.0f, 0.0005f);
}

// The bench measured this source's line rate at both 37879 and 38135 Hz within
// one session -- 60.32 Hz and 60.72 Hz over its 628 lines. DMT states 60.317.
TEST_CASE("a field rate that wobbles across half a hertz still finds its mode")
{
    const float duty = 137.0f / 1124.0f;

    CHECK(SourceTiming::matching(SourceKey(628, 60.32f, duty, SourceKey::Positive, SourceKey::Positive)).published());
    CHECK(SourceTiming::matching(SourceKey(628, 60.72f, duty, SourceKey::Positive, SourceKey::Positive)).published());
}

TEST_CASE("the line active video starts on, for a caller that cannot scale")
{
    // Pass-through plays the source's own raster out, so the only thing it can
    // blank correctly is what the raster says is not picture. 720x480p spends
    // 6 lines on sync and 30 on back porch. THE BLANKING IS PLACED AGAINST THE
    // HD CHANNEL'S COUNTER, not the input formatter's, and where that one
    // zeroes has not been measured -- so it is taken as the pulse's end and
    // active video is line 30 of 525.
    const SourceTiming cea = SourceTiming::matching(SourceKey(525, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));
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
        SourceKey(312, 50.08f, 36.0f / 512.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 110.0f / 512.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisHorizontal), 320.0f / 512.0f, 0.0005f);
}

TEST_CASE("the Acorn mode's vertical picture starts inside its top border")
{
    SourceTiming akf = SourceTiming::matching(
        SourceKey(312, 50.08f, 36.0f / 512.0f, SourceKey::Negative, SourceKey::Negative));

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
        SourceKey(262, 60.0f, 72.0f / 1020.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 234.0f / 1020.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisHorizontal), 640.0f / 1020.0f, 0.0005f);
    CHECK_NEAR(akf.activeExtent(AxisVertical), 200.0f / 262.0f, 0.0005f);
}

TEST_CASE("a standard answers for a mode file stating the same sync width")
{
    // AKF50's 800x600@60 is 128,48,40,800,40,0 and DMT's is 128,88,0,800,0,40:
    // one total, one sync width, and the 40-pixel borders sitting in the porches
    // put the picture at 216 either way. Nothing can tell those two rows apart,
    // so the array order does, and of the two it is the standard that answers.
    SourceTiming both = SourceTiming::matching(
        SourceKey(628, 60.32f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(both.published());
    CHECK_NEAR(both.activeStart(AxisHorizontal), 216.0f / 1056.0f, 0.0005f);
}

// VESA DMT 640x350@85 and 640x400@85 share one horizontal raster, one frame of
// 445 lines and one field rate, and differ in the polarity pair alone: +H -V
// against -H +V. The polarity is the whole of what tells them apart, and the
// standard keyed them on it deliberately.
TEST_CASE("two rasters differing only in polarity are told apart by it")
{
    const float duty = 64.0f / 832.0f;

    SourceTiming lines350 = SourceTiming::matching(
        SourceKey(445, 85.08f, duty, SourceKey::Positive, SourceKey::Negative));
    SourceTiming lines400 = SourceTiming::matching(
        SourceKey(445, 85.08f, duty, SourceKey::Negative, SourceKey::Positive));

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
        SourceKey(445, 85.08f, 64.0f / 832.0f, SourceKey::Undetermined, SourceKey::Undetermined));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeExtent(AxisVertical), 350.0f / 445.0f, 0.0005f);
}

TEST_CASE("a polarity no raster on the key carries still finds the first one")
{
    // A source that has the timing of a standard and inverts one pulse is
    // still placed from that standard rather than from the envelope.
    SourceTiming t = SourceTiming::matching(
        SourceKey(445, 85.08f, 64.0f / 832.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(t.published());
    CHECK_NEAR(t.activeExtent(AxisVertical), 350.0f / 445.0f, 0.0005f);
}


// --- The shape the picture is to be shown in
//
// Nothing on this chip can measure it: the source's pixel clock is unknowable,
// so 320x256 and 640x256 are one key and neither states a shape. What the
// published raster gives instead is the convention the mode was written for,
// which is a better default than filling whatever raster the output happens to
// have. docs/aspect-ratio.md

TEST_CASE("a published raster states the shape it is to be shown in")
{
    // 800x600@60, the bench Acorn desktop mode: square pixels, 4:3 on the
    // monitor the mode file was written for.
    SourceTiming dmt = SourceTiming::matching(
        SourceKey(628, 60.317f, 128.0f / 1056.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(dmt.published());
    CHECK(dmt.aspect() == Aspect(Aspect::FourThree));
}

TEST_CASE("the one 5:4 raster says so rather than inheriting its neighbours")
{
    // 1280x1024@60 is the only mode in any of the three tables that is not 4:3.
    SourceTiming dmt = SourceTiming::matching(
        SourceKey(1066, 60.0f, 112.0f / 1688.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(dmt.published());
    CHECK(dmt.aspect() == Aspect(Aspect::FiveFour));
}

TEST_CASE("the broadcast rasters are 4:3 although their pixels are not square")
{
    // 720x480p and 720x576p are 3:2 and 5:4 counted in pixels, and both are
    // shown as 4:3. The stated shape is the DISPLAY's, which is why it cannot
    // be derived from the active counts the table already carries.
    SourceTiming ntsc = SourceTiming::matching(
        SourceKey(525, 59.94f, 62.0f / 858.0f, SourceKey::Negative, SourceKey::Negative));
    SourceTiming pal = SourceTiming::matching(
        SourceKey(625, 50.0f, 64.0f / 864.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(ntsc.published());
    REQUIRE(pal.published());
    CHECK(ntsc.aspect() == Aspect(Aspect::FourThree));
    CHECK(pal.aspect() == Aspect(Aspect::FourThree));
}

TEST_CASE("an Acorn mode takes the shape of the monitor its mode file describes")
{
    SourceTiming acorn = SourceTiming::matching(
        SourceKey(312, 50.08f, 36.0f / 512.0f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(acorn.published());
    CHECK(acorn.aspect() == Aspect(Aspect::FourThree));
}

TEST_CASE("a source matching no raster is shown as 4:3 rather than filled")
{
    // The fallback is a shape and not Fill, because an unrecognised source is
    // far more often a 4:3 computer mode than a widescreen one, and filling is
    // the answer the user can always ask for.
    SourceTiming unknown(50.08f);

    REQUIRE_FALSE(unknown.published());
    CHECK(unknown.aspect() == Aspect(Aspect::FourThree));
}

TEST_CASE("a published raster says how wide one of the source's pixels is")
{
    // The chip sees sync edges and never a pixel clock, so a source pixel has
    // a width in ADC samples only where a raster states the line's own pixel
    // count. The bench 320x256@50 samples 2200 of them across 512 pixels.
    SourceTiming akf = SourceTiming::matching(
        SourceKey(312, 50.08f, 36.0f / 512.0f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.pixelSamples(2200), 2200.0f / 512.0f, 0.001f);

    SUBCASE("and a source matching none states no width at all") {
        SourceTiming unpublished(59.94f);

        CHECK(unpublished.pixelSamples(1446) == 0.0f);
    }

    SUBCASE("nor does a divider nothing is sampling at") {
        CHECK(akf.pixelSamples(0) == 0.0f);
    }
}


// --- Which row, where two authorities describe one key
//
// The bench RISC PC runs an Acorn monitor definition, and its entries share a
// frame, a field rate and very nearly a sync width with the VESA DMT entry for
// the same resolution while putting active video somewhere else. Measured on
// `vga` into 1920x1080 at a full framing, the emitted raster is the ACORN one
// on every mode where the two differ.
// docs/investigations/full-screen-framing-on-the-vesa-modes.md

TEST_CASE("the nearer sync width decides between two rasters on one key")
{
    // AKF50's 640x480@60 spends 94 pixels of 800 on sync and starts its picture
    // at 138; DMT spends 96 and starts at 144. Measured on the bench, the duty
    // reads 11.547% -- 0.203 points from AKF50 and 0.453 from DMT -- and the
    // emitted picture starts at 138.4. So the nearer row is the one the source
    // is running, and the authority it came from says nothing about that.
    SourceTiming akf = SourceTiming::matching(
        SourceKey(525, 59.94f, 0.11547f, SourceKey::Negative, SourceKey::Negative));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 138.0f / 800.0f, 0.0005f);
}

TEST_CASE("the bench RISC PC at 1024x768@60 is placed from its own mode file")
{
    // AKF60 states 128,36,60,1024,60,36 of 1344 at 65 MHz -- picture at 224,
    // positive on both pulses -- where DMT 16 states 136,160,0,1024,0,24 and
    // negative on both. One total, one clock, one frame, and 71 pixels between
    // the two picture starts.
    //
    // Measured on `vga` into 1920x1080 at a full framing: the card's green
    // frame spans 1024.0 source pixels starting at 225.0, the line totals
    // 1343.9 at 65.14 MHz over 806 lines, the duty reads 9.644% against AKF60's
    // 9.524% and DMT's 10.119%, and STATUS_SYNC_PROC_HSPOL reads positive. So
    // the machine is running the mode file and not the standard.
    SourceTiming akf = SourceTiming::matching(
        SourceKey(806, 60.0f, 0.09644f, SourceKey::Positive, SourceKey::Positive));

    REQUIRE(akf.published());
    CHECK_NEAR(akf.activeStart(AxisHorizontal), 224.0f / 1344.0f, 0.0005f);
}

TEST_CASE("a sync width further off than the measurement can be matches nothing")
{
    // The bound is what the DUTY MEASUREMENT can be wrong by, because that is
    // the only thing the chip can tell two rasters apart with. Measured on
    // `vga` across ten modes at a full framing, the reading sits 0.085 points
    // from the raster the source is emitting on average, sd 0.164, worst 0.300;
    // composite sync adds the separator's own offset, about 3 ADC samples, or
    // 0.23 points at the bench's dividers.
    //
    // A point away is none of that, so it is not this source's raster and
    // placing the window from it would crop picture. DMT's 640x480@60 states
    // 12.00%.
    CHECK_FALSE(SourceTiming::matching(
        SourceKey(525, 59.94f, 0.130f, SourceKey::Negative, SourceKey::Negative)).published());
}

// Every mode measured on the bench RISC PC under its loaded monitor
// definition, `vga` into 1920x1080 at a full framing with the card's green
// frame read off the USB capture. `duty` and `start` are the measurement; the
// row is what the source turned out to be running. The polarity is the mode
// file's, and at 1024x768@60 it was confirmed against STATUS_SYNC_PROC_HSPOL.
//
// 1280x720@60 is absent because it is CEA 4 and the Cea array holds only 480p
// and 576p -- it measured 258.1 against the standard's 260 and is still
// unpublished. 360x480@60 is absent for a reason no row can fix: Acorn spends
// a near-constant share of the line on sync across a resolution family, so its
// 12.030% and 640x480@60's 11.750% are 0.28 points apart against a measurement
// worth 0.30, and the whole family is one key. docs/known-issues.md
namespace {

struct Measured {
    uint16_t lines;
    float rateHz;
    float duty;
    SourceKey::Polarity hsync;
    SourceKey::Polarity vsync;
    float start;        // the fraction of the line the raster states
    const char *mode;
};

const Measured BenchModes[] = {
    { 312, 50.08f, 0.07084f, SourceKey::Positive, SourceKey::Positive, 110.0f / 512.0f,  "320x256@50 AKF50" },
    { 525, 60.00f, 0.10297f, SourceKey::Negative, SourceKey::Negative,  68.0f / 400.0f,  "320x480@60 AKF50" },
    { 525, 59.94f, 0.11547f, SourceKey::Negative, SourceKey::Negative, 138.0f / 800.0f,  "640x480@60 AKF50" },
    { 525, 59.94f, 0.11547f, SourceKey::Negative, SourceKey::Negative, 276.0f / 1600.0f, "1280x480@60 AKF50" },
    { 525, 59.94f, 0.06926f, SourceKey::Negative, SourceKey::Negative, 122.0f / 858.0f,  "720x480@60 CEA 2/3" },
    { 615, 60.00f, 0.07189f, SourceKey::Negative, SourceKey::Negative, 170.0f / 870.0f,  "640x512@60 AKF91" },
    { 628, 60.32f, 0.12212f, SourceKey::Positive, SourceKey::Positive, 216.0f / 1056.0f, "800x600@60 AKF50" },
    { 806, 60.00f, 0.09644f, SourceKey::Positive, SourceKey::Positive, 224.0f / 1344.0f, "1024x768@60 AKF60" },
};

}  // namespace

TEST_CASE("every mode the bench emits resolves to the raster it is emitting")
{
    for (unsigned i = 0; i < sizeof(BenchModes) / sizeof(BenchModes[0]); ++i) {
        const Measured &want = BenchModes[i];
        INFO(want.mode);
        SourceTiming got = SourceTiming::matching(
            SourceKey(want.lines, want.rateHz, want.duty, want.hsync, want.vsync));

        REQUIRE(got.published());
        CHECK_NEAR(got.activeStart(AxisHorizontal), want.start, 0.0005f);
    }
}
