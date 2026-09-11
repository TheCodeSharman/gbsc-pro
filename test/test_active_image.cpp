// Host-compiled unit tests for Tv5725::ActiveImage -- `make -C test active-image`.
// The framing is held as state and every window is derived from it, so a pad
// press recomputes rather than inherits. docs/firmware-geometry-engine.md.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// windows this class derives, for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "CheckNear.h"
#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ActiveImage.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BlankingTiming.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PanAndZoom.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"

using namespace Tv5725;

// --- the framing, held as state rather than read back ------------------------


// The framing is proportions now, so a test that means "40 units of zoom" has
// to say so in units against a line. Seeds the default for that line and moves
// it, which is what a press does.
static Tv5725::ActiveImage framed(const Tv5725::VideoSourceLine &line, float rate,
                                  const Tv5725::Axis &axis, int16_t zoomUnits,
                                  int16_t panUnits, uint16_t raster = 0)
{
    Tv5725::ActiveImage f;
    if (zoomUnits != 0)
        f.zoomBy(line, rate, axis, zoomUnits);
    if (panUnits != 0)
        f.panBy(line, rate, axis, panUnits);
    return f;
}

TEST_CASE("the framing is held as state and the window is derived")
{
    BlankingTiming wide = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
    BlankingTiming centred = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);

    SUBCASE("the default framing takes the default capture width") {
        ActiveImage at_rest;
        BlankingTiming got = at_rest.capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(got.start() - got.stop() == ActiveImage::defaultWidth(VideoSourceLine(1126), 50.0f, AxisHorizontal));
    }

    SUBCASE("one unit of zoom is one unit of capture") {
        // The point of the absolute framing: a tap has to mean one pixel, and a
        // proportional control cannot. 6% of an 890 unit capture is 53 units,
        // and a ratio small enough to give 1 there rounds to nothing at a
        // narrow capture.
        for (int16_t units : {1, 2, 7, 40, 300}) {
            BlankingTiming in = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, units, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
            CHECK((wide.start() - wide.stop()) - (in.start() - in.stop()) == units);
        }
    }

    SUBCASE("one unit is one unit at a narrow capture too") {
        BlankingTiming narrow = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 800, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        BlankingTiming narrower = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 801, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK((narrow.start() - narrow.stop()) - (narrower.start() - narrower.stop())
              == 1);
    }

    SUBCASE("zoom out and back returns the window exactly") {
        // Integer units, so this is exact by construction rather than by the
        // rounding happening to cancel.
        BlankingTiming there = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 137, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        BlankingTiming back = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(((back.stop() == wide.stop()) && (back.start() == wide.start())));
        CHECK(there.start() - there.stop() < wide.start() - wide.stop());
    }

    SUBCASE("panning moves the window and keeps its width") {
        // Zoomed first, because the default window is wide enough that a pan of
        // 40 units runs into the end of the line and is clamped there.
        BlankingTiming cropped = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 200, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        BlankingTiming moved = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 200, +40, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(moved.stop() == cropped.stop() + 40);
        CHECK(moved.start() - moved.stop() == cropped.start() - cropped.stop());
    }

    SUBCASE("a pan is clamped to the line rather than crossing it") {
        BlankingTiming far_right = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, +5000, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(far_right.start() <= 1126);
        CHECK(far_right.start() - far_right.stop() == centred.start() - centred.stop());
        BlankingTiming far_left = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, -5000, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(far_left.stop() == 0);
        CHECK(far_left.start() - far_left.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a zoom in never crops the capture away to nothing") {
        BlankingTiming tiny = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 5000, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(tiny.start() - tiny.stop() >= MinimumCapture);
    }

    SUBCASE("zooming out never puts the capture stop on the wrap point") {
        // IF_VB_ST rolls at the frame it is counted on and does not clamp, so
        // a window written onto that value rolls the frame -- which reads as the
        // picture jumping rather than as a capture fault. Three steps of
        // zoom-out reach it: 0..624 of a 624-unit frame.
        for (int16_t units : {-1, -20, -60, -100, -500, -5000}) {
            BlankingTiming w = framed(VideoSourceLine(624), 50.0f, AxisVertical, units, 0, 0).capture(VideoSourceLine(624), 50.0f, AxisVertical);
            CHECK(w.start() <= 623);
        }
    }

    SUBCASE("the same wrap bound applies horizontally") {
        for (int16_t units : {-1, -60, -200, -300, -5000}) {
            BlankingTiming w = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, units, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
            CHECK(w.start() <= 1276);
        }
    }

    SUBCASE("a pan cannot put the capture stop on the wrap point either") {
        // pan_capture() bounds it the same way, for the same reason.
        for (int16_t p : {+5000, +600, -5000}) {
            CHECK(framed(VideoSourceLine(624), 50.0f, AxisVertical, 0, p, 0).capture(VideoSourceLine(624), 50.0f, AxisVertical).start()
                  <= VideoSourceLine(624).lastCapture());
            CHECK(framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, p, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal).start()
                  <= VideoSourceLine(1126).lastCapture());
        }
    }

    SUBCASE("the capture stop never lands on the line reset itself") {
        for (int16_t p : {+5000, +600, +132}) {
            CHECK(framed(VideoSourceLine(1265), 50.0f, AxisHorizontal, 0, p, 0).capture(VideoSourceLine(1265), 50.0f, AxisHorizontal).start() <= 1263);
        }
    }

    SUBCASE("a zoom out never runs past the line") {
        BlankingTiming huge = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, -5000, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(huge.start() <= 1126);
        CHECK(huge.stop() <= huge.start());
    }

    SUBCASE("the vertical axis derives from its own frame the same way") {
        BlankingTiming v = framed(VideoSourceLine(624), 50.0f, AxisVertical, 0, 0, 0).capture(VideoSourceLine(624), 50.0f, AxisVertical);
        CHECK(v.start() - v.stop() == ActiveImage::defaultWidth(VideoSourceLine(624), 50.0f, AxisVertical));
        CHECK_NEAR((int)v.stop(), AxisVertical.activeStart() * 624.0f, 1.0);
    }
}

// capture() clamps the WINDOW, and the FRAMING has to be clamped with it, or a
// press past the edge leaves the framing beyond anything achievable and every
// smaller press back produces an identical window -- the control dies in that
// direction. Only the accelerating hold ramp presses that far.
//
// On the bench line: 1126 units, default width 890, so the centred start is 118
// and the largest the line allows is 1124 - 890 = 234, giving a largest
// achievable horizontalPan of 116.
TEST_CASE("a source running a published raster is captured where that raster puts picture")
{
    // 640x480@60 spends 18.0% of the line reaching picture and 80.0% of it on
    // picture, and nothing on this chip can measure that -- so a source running
    // a raster the standards state is placed from the standard rather than from
    // the assumption an unrecognised one takes.
    const VideoSourceLine line(1126);
    const SourceTiming dmt = SourceTiming::matching(524, 59.94f, 96.0f / 800.0f);

    BlankingTiming got = ActiveImage().capture(line, dmt, AxisHorizontal);

    CHECK_NEAR(got.stop(), 0.180f * 1126.0f, 1.0f);
    CHECK_NEAR(got.width(), 0.800f * 1126.0f, 1.0f);
}

TEST_CASE("a source matching no published raster is captured across the envelope")
{
    // Nothing can measure where a border ends and back porch begins, so a
    // source running no raster the standards state is captured across the
    // widest active region any real source puts on a line: black edges are
    // visible and one press trims them, where a cropped edge looks like a
    // fault. Measured against stock AKF50, whose 15 kHz modes put picture
    // between 15.4% and 90.4% of the line and 7.1% and 99.4% of the frame.
    const VideoSourceLine line(1126);

    BlankingTiming got = ActiveImage().capture(line, 50.0f, AxisHorizontal);
    CHECK_NEAR(got.stop(), 0.117f * 1126.0f, 1.0f);
    CHECK_NEAR(got.start(), 0.981f * 1126.0f, 1.0f);

    SUBCASE("and the vertical envelope does not split on field rate") {
        BlankingTiming fifty = ActiveImage().capture(VideoSourceLine(624), 50.0f,
                                                     AxisVertical);
        BlankingTiming sixty = ActiveImage().capture(VideoSourceLine(624), 60.0f,
                                                     AxisVertical);
        CHECK(fifty.stop() == sixty.stop());
        CHECK(fifty.start() == sixty.start());
        CHECK_NEAR(fifty.stop(), 0.061f * 624.0f, 1.0f);
        CHECK_NEAR(fifty.start(), 0.994f * 624.0f, 1.0f);
    }
}

TEST_CASE("a press that overshoots the edge leaves no dead zone")
{
    const uint16_t Units = 1126;
    const float Rate = 50.0f;

    SUBCASE("the capture floor is the line's, not the output raster's") {
        // Cropping past what the magnification can put back letterboxes the
        // picture -- fitToRaster clamps the scale and produced shrinks -- rather
        // than stopping the control. The only floor left is the one that stops a
        // press cropping to nothing, and it is a property of the line, so the
        // same framing crops the same amount whatever the output is doing.
        ActiveImage f;
        f.zoomBy(VideoSourceLine(623), Rate, AxisVertical, 5000);  // hard against the stop
        f.clampToLine(VideoSourceLine(623), Rate, AxisVertical);
        BlankingTiming got = f.capture(VideoSourceLine(623), Rate, AxisVertical);
        CHECK(got.start() - got.stop() == MinimumCapture);
    }

    SUBCASE("an overshooting pan is brought back to what the line allows") {
        ActiveImage f;
        f.panBy(VideoSourceLine(Units), Rate, AxisHorizontal, 200);  // way past
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        // The framing is a proportion now, so the reachable edge is asserted on
        // the window it lands on rather than on the units behind it.
        BlankingTiming pinned = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);
        CHECK(pinned.start() == VideoSourceLine(Units).lastCapture());
    }

    SUBCASE("and one unit back then actually moves the window") {
        ActiveImage f;
        f.panBy(VideoSourceLine(Units), Rate, AxisHorizontal, 200);
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        BlankingTiming at_edge = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);

        f.panBy(VideoSourceLine(Units), Rate, AxisHorizontal, -1);
        BlankingTiming back = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);
        CHECK(back.stop() < at_edge.stop());
    }

    SUBCASE("the same holds at the other end of the line") {
        ActiveImage f;
        f.panBy(VideoSourceLine(Units), Rate, AxisHorizontal, -200);
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        CHECK(f.capture(VideoSourceLine(Units), Rate, AxisHorizontal).stop()
              == VideoSourceLine(Units).firstCapture());

        BlankingTiming at_edge = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);
        f.panBy(VideoSourceLine(Units), Rate, AxisHorizontal, +1);
        CHECK(f.capture(VideoSourceLine(Units), Rate, AxisHorizontal).stop() > at_edge.stop());
    }

    SUBCASE("vertically too, which is the 'or bottom' half of the report") {
        ActiveImage f;
        f.panBy(VideoSourceLine(624), Rate, AxisVertical, -400);
        f.clampToLine(VideoSourceLine(624), Rate, AxisVertical);
        BlankingTiming at_edge = f.capture(VideoSourceLine(624), Rate, AxisVertical);
        CHECK(at_edge.stop() == 0);

        f.panBy(VideoSourceLine(624), Rate, AxisVertical, +1);
        CHECK(f.capture(VideoSourceLine(624), Rate, AxisVertical).stop() > at_edge.stop());
    }

    SUBCASE("an overshooting zoom is brought back the same way") {
        // clampWidth() pins the width at MinimumCapture, and the same dead zone
        // forms in horizontalZoom_ if the framing is left holding the overshoot.
        ActiveImage f;
        f.zoomBy(VideoSourceLine(Units), Rate, AxisHorizontal, 5000);
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        BlankingTiming tightest = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);
        CHECK(tightest.start() - tightest.stop() == MinimumCapture);

        f.zoomBy(VideoSourceLine(Units), Rate, AxisHorizontal, -1);
        BlankingTiming wider = f.capture(VideoSourceLine(Units), Rate, AxisHorizontal);
        CHECK(wider.start() - wider.stop() > tightest.start() - tightest.stop());
    }

    SUBCASE("clamping a reachable framing again changes nothing") {
        // The first clamp puts the proportions on this line's grid, which a
        // hand-written pair is not on. Every clamp after it must be a no-op, or
        // the window walks a unit at a time each time the mode is re-applied.
        ActiveImage f{PanAndZoom(0.10f, 0.78f, 0.09f, 0.80f)};
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        f.clampToLine(VideoSourceLine(624), Rate, AxisVertical);

        ActiveImage settled = f;
        f.clampToLine(VideoSourceLine(Units), Rate, AxisHorizontal);
        f.clampToLine(VideoSourceLine(624), Rate, AxisVertical);
        CHECK(f == settled);
    }
}

// The pulse is at the HEAD, and its width is derived from HLOW_LEN over
// PLLAD_MD rather than held as a constant. The tail is deliberately unbounded.
// docs/scaler-geometry-model.md "The two green regions in an IF line".
TEST_CASE("the capture window never takes the hsync pulse")
{
    // The bench RiscPC: a 7.1% hsync duty, HLOW_LEN 181 of PLLAD_MD 2553, read
    // here at the 2250 the write limit caps the divider to.
    // 160 x 1126 / 2250 = 80.07 -> 81.
    const uint16_t HsyncLow = 160, AdcLine = 2250, LineUnits = 1126;
    const VideoSourceLine SourceLine = VideoSourceLine::measured(LineUnits, HsyncLow, AdcLine, 0, true);
    const float Rate = 50.0f;

    SUBCASE("zooming all the way out stops clear of the sync") {
        BlankingTiming huge = framed(SourceLine, Rate, AxisHorizontal, -5000, 0, 0).capture(SourceLine, Rate, AxisHorizontal);
        CHECK(huge.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("and widens to what the capture path will write, not to both ends") {
        // The ends leave 1043 units between them and the path writes 1024, so
        // zooming out stops on the width rather than reaching the line reset.
        // Panning is what reaches the far end. docs/investigations/tail-green.md
        BlankingTiming huge = framed(SourceLine, Rate, AxisHorizontal, -5000, 0, 0).capture(SourceLine, Rate, AxisHorizontal);
        CHECK(huge.start() - huge.stop() == SourceLine.maxCaptureWidth());
        CHECK(huge.stop() == SourceLine.firstCapture());
    }

    SUBCASE("panning to the left stop cannot walk into the sync") {
        BlankingTiming left = framed(SourceLine, Rate, AxisHorizontal, 0, -5000, 0).capture(SourceLine, Rate, AxisHorizontal);
        CHECK(left.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("panning to the right stop reaches the last unit before the reset") {
        BlankingTiming right = framed(SourceLine, Rate, AxisHorizontal, 0, +5000, 0).capture(SourceLine, Rate, AxisHorizontal);
        CHECK(right.start() == SourceLine.lastCapture());
    }

    SUBCASE("the resting picture is untouched") {
        // The default capture is 890 units of a 1126 unit line and the pulse
        // takes 81 at the head, so 1045 remain: the picture nobody complained
        // about must not move by so much as a unit.
        BlankingTiming guarded = ActiveImage().capture(SourceLine, Rate, AxisHorizontal);
        BlankingTiming whole = ActiveImage().capture(VideoSourceLine(LineUnits), Rate, AxisHorizontal);
        CHECK(guarded.stop() == whole.stop());
        CHECK(guarded.start() == whole.start());
    }

    SUBCASE("zoom out still has somewhere to go") {
        // Clipping the sync must not cost the reach that finds active video the
        // 0.76 assumption crops.
        BlankingTiming rest = ActiveImage().capture(SourceLine, Rate, AxisHorizontal);
        BlankingTiming out = framed(SourceLine, Rate, AxisHorizontal, -40, 0, 0).capture(SourceLine, Rate, AxisHorizontal);
        CHECK(out.start() - out.stop() == (rest.start() - rest.stop()) + 40);
    }

    SUBCASE("the framing is clamped to the same bound the window is") {
        // capture() clamps the window and clampToLine() clamps the framing; a
        // difference of one unit between them is a dead zone.
        ActiveImage f;
        f.panBy(SourceLine, Rate, AxisHorizontal, -5000);
        f.clampToLine(SourceLine, Rate, AxisHorizontal);
        BlankingTiming at_edge = f.capture(SourceLine, Rate, AxisHorizontal);
        CHECK(at_edge.stop() == SourceLine.syncUnits());

        f.panBy(SourceLine, Rate, AxisHorizontal, +1);
        CHECK(f.capture(SourceLine, Rate, AxisHorizontal).stop() > at_edge.stop());
    }
}

// --- a capture window when there is not a usable one --------------------------

TEST_CASE("a nonsense capture is replaced, not trusted")
{
    // The stock preset commonly leaves IF_VB_ST <= IF_VB_SP, and what the chip
    // means by that is not established. The engine computes a window rather
    // than decoding one.
    BlankingTiming w = framed(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(1126), 50.0f, AxisHorizontal);

    SUBCASE("a default capture is placed on the line, not centred in it") {
        // Active video is never centred: sync plus back porch runs far longer
        // than the front porch, so a centred window is offset left of the
        // picture and crops its far edge.
        CHECK(w.start() > w.stop());
        CHECK_NEAR((int)w.stop(), AxisHorizontal.activeStart() * 1126.0f, 1.0);
        CHECK((int)w.start() > 1126 - (int)w.stop());
    }

    SUBCASE("a default capture over-captures rather than cropping") {
        // Black edges are visible and adjustable; a cropped edge looks like a
        // tuning fault and sends you hunting for a problem that is not there.
        CHECK(w.start() - w.stop() > 1126 * 0.76);
    }

    SUBCASE("a default capture never exceeds the line it sits in") {
        for (uint16_t units : {64, 256, 624, 1277, 2559}) {
            BlankingTiming any = framed(VideoSourceLine(units), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(units), 50.0f, AxisHorizontal);
            CHECK(any.start() <= units);
            CHECK(any.start() > any.stop());
        }
    }

    SUBCASE("the default is one envelope, whatever the field rate") {
        // The envelope contains what every real source puts on a line, so
        // splitting it by field rate would only make one of the two crop.
        CHECK(ActiveImage::defaultWidth(VideoSourceLine(624), 60.0f, AxisVertical)
              == ActiveImage::defaultWidth(VideoSourceLine(624), 50.0f, AxisVertical));
        CHECK(ActiveImage::defaultWidth(VideoSourceLine(1126), 50.0f, AxisHorizontal)
              == ActiveImage::defaultWidth(VideoSourceLine(1126), 60.0f, AxisHorizontal));
    }

    SUBCASE("a line of zero yields nothing rather than a wrapped window") {
        BlankingTiming none = framed(VideoSourceLine(0), 50.0f, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(0), 50.0f, AxisHorizontal);
        CHECK(((none.stop() == 0) && (none.start() == 0)));
    }
}

TEST_CASE("no framing puts the capture stop past what the line can write")
{
    const uint16_t lines[] = {624, 1126, 1265, 1277, 2250};
    const int16_t pans[] = {0, +40, +600, +5000, -600, -5000};
    const int16_t zooms[] = {0, +137, +800, +5000, -40, -5000};

    for (uint16_t units : lines) {
        for (bool vertical : {false, true}) {
            const VideoSourceLine line = vertical ? VideoSourceLine(units)
                                            : VideoSourceLine::measured(units, 181, 2553, 0, true);
            CAPTURE(units);
            CAPTURE(vertical);
            CAPTURE(line.lastCapture());

            for (int16_t p : pans) {
                for (int16_t z : zooms) {
                    CAPTURE(p);
                    CAPTURE(z);
                    const ActiveImage image =
                        framed(line, 50.0f, vertical ? AxisVertical : AxisHorizontal,
                               z, p, 1916);
                    const BlankingTiming got =
                        image.capture(line, 50.0f, vertical ? AxisVertical : AxisHorizontal);

                    CHECK(got.start() <= line.lastCapture());
                    CHECK(got.stop() >= line.firstCapture());
                }
            }
        }
    }
}

// `--dump` prints the derived windows for inspection by hand.
static void dumpGrid()
{
    // The horizontal default, which geometry_math has a twin for. There is no
    // Python vertical equivalent, so that half is covered by the host tests only.
    for (uint16_t units : {320, 624, 1277, 2048, 2559})
        for (float rate : {50.0f, 60.0f}) {
            BlankingTiming w = framed(VideoSourceLine(units), rate, AxisHorizontal, 0, 0, 0).capture(VideoSourceLine(units), rate, AxisHorizontal);
            std::printf("default %u %.0f %u %u\n", units, rate, w.stop(), w.start());
        }

    // capture() and clampToLine() recompute the same window and must agree
    // exactly, so both are printed either side of the clamp: a divergence shows
    // as the two windows differing on one row.
    for (uint16_t units : {320, 624, 1277, 2048})
        for (int16_t zoom : {0, 40, 200, -60})
            for (int16_t pan : {0, 50, 400, -400})
                for (int vertical = 0; vertical < 2; ++vertical) {
                    const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
                    VideoSourceLine line(units, vertical ? 0 : (uint16_t)(units / 14));

                    // Seed the untuned default for this line, then move it by the
                    // same units the old four-integer framing carried, so the
                    // window columns stay comparable across the change.
                    ActiveImage framing;
                    framing.clampToLine(line, 50.0f, axis);
                    PanAndZoom moved = framing.framing();
                    moved.zoomBy(axis, zoom, line.capturable());
                    moved.panBy(axis, pan, line.capturable());
                    framing.setFraming(moved);

                    BlankingTiming before = framing.capture(line, 50.0f, axis);
                    framing.clampToLine(line, 50.0f, axis);
                    BlankingTiming after = framing.capture(line, 50.0f, axis);

                    long width = (long)after.start() - (long)after.stop();
                    long zoomUnits = (long)ActiveImage::defaultWidth(line, 50.0f, axis) - width;
                    long panUnits = (long)after.stop() - (long)(line.units() - width) / 2;

                    std::printf("framing %u %d %d %d %u %u %d %d %u %u\n",
                                units, zoom, pan, vertical,
                                before.stop(), before.start(),
                                (int)zoomUnits, (int)panUnits,
                                after.stop(), after.start());
                }
}

int main(int argc, char **argv)
{
    // Before the test runner, which exits non-zero on an option it does not
    // know.
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dumpGrid();
        return 0;
    }
    return doctest::Context(argc, argv).run();
}

// The default framing is where the picture sits before anyone frames it, and it
// comes from the standard's own active start -- a position in the SOURCE's line.
// Placing it at that fraction of the IF line assumes the two lines share an
// origin, which they do not.
// docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
TEST_CASE("the default capture starts where video lands, not where the standard states it")
{
    // 800x600@60 at PLLAD_MD 1124: HLOW_LEN 138 of 1124 is its 12.1% duty.
    const uint16_t Units = 1125, HsyncLow = 138, AdcLine = 1124;
    const uint16_t Lag = Tv5725::VideoSourceLine::CaptureLagUnits;
    const float Rate = 60.0f;

    SUBCASE("a positive pulse puts it a lag past the arithmetic") {
        VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, true);
        VideoSourceLine placed = VideoSourceLine::measured(Units, HsyncLow, AdcLine, 0, true);
        BlankingTiming got = ActiveImage().capture(line, Rate, AxisHorizontal);
        BlankingTiming was = ActiveImage().capture(placed, Rate, AxisHorizontal);
        CHECK(got.stop() - was.stop() == Lag);
    }

    SUBCASE("an inverted pulse puts it a sync width the other way") {
        VideoSourceLine positive = VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, true);
        VideoSourceLine inverted = VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, false);
        BlankingTiming at_head = ActiveImage().capture(positive, Rate, AxisHorizontal);
        BlankingTiming behind = ActiveImage().capture(inverted, Rate, AxisHorizontal);
        CHECK(at_head.stop() - behind.stop() == positive.syncUnits());
    }
}
