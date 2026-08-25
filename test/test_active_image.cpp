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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PanAndZoom.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

using namespace Tv5725;

// --- the framing, held as state rather than read back ------------------------


// The framing is proportions now, so a test that means "40 units of zoom" has
// to say so in units against a line. Seeds the default for that line and moves
// it, which is what a press does.
static Tv5725::ActiveImage framed(const Tv5725::InputLine &line, float rate,
                                  const Tv5725::Axis &axis, int16_t zoomUnits,
                                  int16_t panUnits, uint16_t raster = 0)
{
    Tv5725::ActiveImage f;
    if (zoomUnits != 0)
        f.zoomBy(line, rate, axis, zoomUnits, raster);
    if (panUnits != 0)
        f.panBy(line, rate, axis, panUnits, raster);
    return f;
}

TEST_CASE("the framing is held as state and the window is derived")
{
    BlankingTiming wide = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
    BlankingTiming centred = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);

    SUBCASE("the default framing takes the default capture width") {
        ActiveImage at_rest;
        BlankingTiming got = at_rest.capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(got.start() - got.stop() == ActiveImage::defaultWidth(InputLine(1126), 50.0f, AxisHorizontal));
    }

    SUBCASE("one unit of zoom is one unit of capture") {
        // The point of the absolute framing: a tap has to mean one pixel, and a
        // proportional control cannot. 6% of an 890 unit capture is 53 units,
        // and a ratio small enough to give 1 there rounds to nothing at a
        // narrow capture.
        for (int16_t units : {1, 2, 7, 40, 300}) {
            BlankingTiming in = framed(InputLine(1126), 50.0f, AxisHorizontal, units, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
            CHECK((wide.start() - wide.stop()) - (in.start() - in.stop()) == units);
        }
    }

    SUBCASE("one unit is one unit at a narrow capture too") {
        BlankingTiming narrow = framed(InputLine(1126), 50.0f, AxisHorizontal, 800, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        BlankingTiming narrower = framed(InputLine(1126), 50.0f, AxisHorizontal, 801, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK((narrow.start() - narrow.stop()) - (narrower.start() - narrower.stop())
              == 1);
    }

    SUBCASE("zoom out and back returns the window exactly") {
        // Integer units, so this is exact by construction rather than by the
        // rounding happening to cancel.
        BlankingTiming there = framed(InputLine(1126), 50.0f, AxisHorizontal, 137, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        BlankingTiming back = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(((back.stop() == wide.stop()) && (back.start() == wide.start())));
        CHECK(there.start() - there.stop() < wide.start() - wide.stop());
    }

    SUBCASE("panning moves the window and keeps its width") {
        BlankingTiming moved = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, +40, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(moved.stop() == centred.stop() + 40);
        CHECK(moved.start() - moved.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a pan is clamped to the line rather than crossing it") {
        BlankingTiming far_right = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, +5000, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(far_right.start() <= 1126);
        CHECK(far_right.start() - far_right.stop() == centred.start() - centred.stop());
        BlankingTiming far_left = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, -5000, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(far_left.stop() == 0);
        CHECK(far_left.start() - far_left.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a zoom in never crops the capture away to nothing") {
        BlankingTiming tiny = framed(InputLine(1126), 50.0f, AxisHorizontal, 5000, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(tiny.start() - tiny.stop() >= MinimumCapture);
    }

    SUBCASE("zooming out never puts the capture stop on the wrap point") {
        // IF_VB_ST rolls at the frame it is counted on and does not clamp, so
        // a window written onto that value rolls the frame -- which reads as the
        // picture jumping rather than as a capture fault. Three steps of
        // zoom-out reach it: 0..624 of a 624-unit frame.
        for (int16_t units : {-1, -20, -60, -100, -500, -5000}) {
            BlankingTiming w = framed(InputLine(624), 50.0f, AxisVertical, units, 0, 0).capture(InputLine(624), 50.0f, AxisVertical, 0);
            CHECK(w.start() <= 623);
        }
    }

    SUBCASE("the same wrap bound applies horizontally") {
        for (int16_t units : {-1, -60, -200, -300, -5000}) {
            BlankingTiming w = framed(InputLine(1126), 50.0f, AxisHorizontal, units, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
            CHECK(w.start() <= 1276);
        }
    }

    SUBCASE("a pan cannot put the capture stop on the wrap point either") {
        // pan_capture() bounds it the same way, for the same reason.
        for (int16_t p : {+5000, +600, -5000}) {
            CHECK(framed(InputLine(624), 50.0f, AxisVertical, 0, p, 0).capture(InputLine(624), 50.0f, AxisVertical, 0).start()
                  <= InputLine(624).lastCapture());
            CHECK(framed(InputLine(1126), 50.0f, AxisHorizontal, 0, p, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0).start()
                  <= InputLine(1126).lastCapture());
        }
    }

    SUBCASE("the capture stop never lands on the line reset itself") {
        for (int16_t p : {+5000, +600, +132}) {
            CHECK(framed(InputLine(1265), 50.0f, AxisHorizontal, 0, p, 0).capture(InputLine(1265), 50.0f, AxisHorizontal, 0).start() <= 1263);
        }
    }

    SUBCASE("a zoom out never runs past the line") {
        BlankingTiming huge = framed(InputLine(1126), 50.0f, AxisHorizontal, -5000, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(huge.start() <= 1126);
        CHECK(huge.stop() <= huge.start());
    }

    SUBCASE("the vertical axis derives from its own frame the same way") {
        BlankingTiming v = framed(InputLine(624), 50.0f, AxisVertical, 0, 0, 0).capture(InputLine(624), 50.0f, AxisVertical, 0);
        CHECK(v.start() - v.stop() == ActiveImage::defaultWidth(InputLine(624), 50.0f, AxisVertical));
        CHECK_NEAR((int)v.stop(), 624 - (int)v.start(), 1.0);
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
TEST_CASE("a press that overshoots the edge leaves no dead zone")
{
    const uint16_t Units = 1126;
    const float Rate = 50.0f;

    SUBCASE("zoom stops at the magnification ceiling instead of letterboxing") {
        // Zooming past the ceiling otherwise shrinks the capture while the
        // scale sits at its minimum, so the picture gets smaller on screen and
        // the display window closes in around it. At 4.0x a 1126 raster floors
        // at 1126 * 256 / 1024 = 282, the last capture that fills the screen
        // before the bars.
        const uint16_t Raster = 1126;
        CHECK(AxisVertical.minimumCapture(Raster) == 282);

        ActiveImage f;
        f.zoomBy(InputLine(623), Rate, AxisVertical, 5000, Raster);  // hard against the stop
        f.clampToLine(InputLine(623), Rate, AxisVertical, Raster);
        BlankingTiming got = f.capture(InputLine(623), Rate, AxisVertical, Raster);
        CHECK(got.start() - got.stop() == 282);
    }

    SUBCASE("and the floor is the raster's, not a constant") {
        // A different output raster has a different ceiling, so the floor has
        // to be computed from it rather than remembered.
        CHECK(AxisHorizontal.minimumCapture(1445) == 362);
        CHECK(AxisHorizontal.minimumCapture(720) == 180);
    }

    SUBCASE("a caller with no raster keeps the old floor") {
        // defaultWidth() has no raster -- the default is a property of the line
        // alone -- so 0 means "no scale floor" rather than "floor of zero".
        ActiveImage f;
        f.zoomBy(InputLine(1126), Rate, AxisHorizontal, 5000, 0);
        f.clampToLine(InputLine(1126), Rate, AxisHorizontal, 0);
        BlankingTiming got = f.capture(InputLine(1126), Rate, AxisHorizontal, 0);
        CHECK(got.start() - got.stop() == MinimumCapture);
    }

    SUBCASE("an overshooting pan is brought back to what the line allows") {
        ActiveImage f;
        f.panBy(InputLine(Units), Rate, AxisHorizontal, 200, 0);  // way past
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        // The framing is a proportion now, so the reachable edge is asserted on
        // the window it lands on rather than on the units behind it.
        BlankingTiming pinned = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(pinned.start() == InputLine(Units).lastCapture());
    }

    SUBCASE("and one unit back then actually moves the window") {
        ActiveImage f;
        f.panBy(InputLine(Units), Rate, AxisHorizontal, 200, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        BlankingTiming at_edge = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);

        f.panBy(InputLine(Units), Rate, AxisHorizontal, -1, 0);
        BlankingTiming back = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(back.stop() < at_edge.stop());
    }

    SUBCASE("the same holds at the other end of the line") {
        ActiveImage f;
        f.panBy(InputLine(Units), Rate, AxisHorizontal, -200, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(f.capture(InputLine(Units), Rate, AxisHorizontal, 0).stop()
              == InputLine(Units).firstCapture());

        BlankingTiming at_edge = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        f.panBy(InputLine(Units), Rate, AxisHorizontal, +1, 0);
        CHECK(f.capture(InputLine(Units), Rate, AxisHorizontal, 0).stop() > at_edge.stop());
    }

    SUBCASE("vertically too, which is the 'or bottom' half of the report") {
        ActiveImage f;
        f.panBy(InputLine(624), Rate, AxisVertical, -400, 0);
        f.clampToLine(InputLine(624), Rate, AxisVertical, 0);
        BlankingTiming at_edge = f.capture(InputLine(624), Rate, AxisVertical, 0);
        CHECK(at_edge.stop() == 0);

        f.panBy(InputLine(624), Rate, AxisVertical, +1, 0);
        CHECK(f.capture(InputLine(624), Rate, AxisVertical, 0).stop() > at_edge.stop());
    }

    SUBCASE("an overshooting zoom is brought back the same way") {
        // clampWidth() pins the width at MinimumCapture, and the same dead zone
        // forms in horizontalZoom_ if the framing is left holding the overshoot.
        ActiveImage f;
        f.zoomBy(InputLine(Units), Rate, AxisHorizontal, 5000, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        BlankingTiming tightest = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(tightest.start() - tightest.stop() == MinimumCapture);

        f.zoomBy(InputLine(Units), Rate, AxisHorizontal, -1, 0);
        BlankingTiming wider = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(wider.start() - wider.stop() > tightest.start() - tightest.stop());
    }

    SUBCASE("clamping a reachable framing again changes nothing") {
        // The first clamp puts the proportions on this line's grid, which a
        // hand-written pair is not on. Every clamp after it must be a no-op, or
        // the window walks a unit at a time each time the mode is re-applied.
        ActiveImage f{PanAndZoom(0.10f, 0.78f, 0.09f, 0.80f)};
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        f.clampToLine(InputLine(624), Rate, AxisVertical, 0);

        ActiveImage settled = f;
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        f.clampToLine(InputLine(624), Rate, AxisVertical, 0);
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
    const InputLine SourceLine = InputLine::measured(LineUnits, HsyncLow, AdcLine);
    const float Rate = 50.0f;

    SUBCASE("zooming all the way out stops clear of the sync") {
        BlankingTiming huge = framed(SourceLine, Rate, AxisHorizontal, -5000, 0, 0).capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(huge.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("and takes the tail down to the line reset, which it may not have") {
        BlankingTiming huge = framed(SourceLine, Rate, AxisHorizontal, -5000, 0, 0).capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(huge.start() == LineUnits - 2);
    }

    SUBCASE("panning to the left stop cannot walk into the sync") {
        BlankingTiming left = framed(SourceLine, Rate, AxisHorizontal, 0, -5000, 0).capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(left.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("panning to the right stop reaches the last unit before the reset") {
        BlankingTiming right = framed(SourceLine, Rate, AxisHorizontal, 0, +5000, 0).capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(right.start() == LineUnits - 2);
    }

    SUBCASE("the resting picture is untouched") {
        // The default capture is 890 units of a 1126 unit line and the pulse
        // takes 81 at the head, so 1045 remain: the picture nobody complained
        // about must not move by so much as a unit.
        BlankingTiming guarded = ActiveImage().capture(SourceLine, Rate, AxisHorizontal, 0);
        BlankingTiming whole = ActiveImage().capture(InputLine(LineUnits), Rate, AxisHorizontal, 0);
        CHECK(guarded.stop() == whole.stop());
        CHECK(guarded.start() == whole.start());
    }

    SUBCASE("zoom out still has somewhere to go") {
        // Clipping the sync must not cost the reach that finds active video the
        // 0.76 assumption crops.
        BlankingTiming rest = ActiveImage().capture(SourceLine, Rate, AxisHorizontal, 0);
        BlankingTiming out = framed(SourceLine, Rate, AxisHorizontal, -40, 0, 0).capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(out.start() - out.stop() == (rest.start() - rest.stop()) + 40);
    }

    SUBCASE("the framing is clamped to the same bound the window is") {
        // capture() clamps the window and clampToLine() clamps the framing; a
        // difference of one unit between them is a dead zone.
        ActiveImage f;
        f.panBy(SourceLine, Rate, AxisHorizontal, -5000, 0);
        f.clampToLine(SourceLine, Rate, AxisHorizontal, 0);
        BlankingTiming at_edge = f.capture(SourceLine, Rate, AxisHorizontal, 0);
        CHECK(at_edge.stop() == SourceLine.syncUnits());

        f.panBy(SourceLine, Rate, AxisHorizontal, +1, 0);
        CHECK(f.capture(SourceLine, Rate, AxisHorizontal, 0).stop() > at_edge.stop());
    }
}

// --- a capture window when there is not a usable one --------------------------

TEST_CASE("a nonsense capture is replaced, not trusted")
{
    // The stock preset commonly leaves IF_VB_ST <= IF_VB_SP, and what the chip
    // means by that is not established. The engine computes a window rather
    // than decoding one.
    BlankingTiming w = framed(InputLine(1126), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);

    SUBCASE("a default capture is centred on the line") {
        CHECK(w.start() > w.stop());
        CHECK_NEAR((int)w.stop(), 1126 - (int)w.start(), 1.0);
    }

    SUBCASE("a default capture over-captures rather than cropping") {
        // Black edges are visible and adjustable; a cropped edge looks like a
        // tuning fault and sends you hunting for a problem that is not there.
        CHECK(w.start() - w.stop() > 1126 * 0.76);
    }

    SUBCASE("a default capture never exceeds the line it sits in") {
        for (uint16_t units : {64, 256, 624, 1277, 2559}) {
            BlankingTiming any = framed(InputLine(units), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(units), 50.0f, AxisHorizontal, 0);
            CHECK(any.start() <= units);
            CHECK(any.start() > any.stop());
        }
    }

    SUBCASE("the vertical default splits on field rate") {
        // A 50 Hz source carries the same active height in a longer frame, so
        // the fraction is not the same. Horizontal barely moves, does not split.
        CHECK(ActiveImage::defaultWidth(InputLine(624), 60.0f, AxisVertical)
              > ActiveImage::defaultWidth(InputLine(624), 50.0f, AxisVertical));
        CHECK(ActiveImage::defaultWidth(InputLine(1126), 50.0f, AxisHorizontal)
              == ActiveImage::defaultWidth(InputLine(1126), 60.0f, AxisHorizontal));
    }

    SUBCASE("a line of zero yields nothing rather than a wrapped window") {
        BlankingTiming none = framed(InputLine(0), 50.0f, AxisHorizontal, 0, 0, 0).capture(InputLine(0), 50.0f, AxisHorizontal, 0);
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
            const InputLine line = vertical ? InputLine(units)
                                            : InputLine::measured(units, 181, 2553);
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
                        image.capture(line, 50.0f, vertical ? AxisVertical : AxisHorizontal, 1916);

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
            BlankingTiming w = framed(InputLine(units), rate, AxisHorizontal, 0, 0, 0).capture(InputLine(units), rate, AxisHorizontal, 0);
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
                    InputLine line(units, vertical ? 0 : (uint16_t)(units / 14));

                    // Seed the untuned default for this line, then move it by the
                    // same units the old four-integer framing carried, so the
                    // window columns stay comparable across the change.
                    ActiveImage framing;
                    framing.clampToLine(line, 50.0f, axis, 1916);
                    PanAndZoom moved = framing.framing();
                    moved.zoomBy(axis, zoom, line.capturable());
                    moved.panBy(axis, pan, line.capturable());
                    framing.setFraming(moved);

                    BlankingTiming before = framing.capture(line, 50.0f, axis, 1916);
                    framing.clampToLine(line, 50.0f, axis, 1916);
                    BlankingTiming after = framing.capture(line, 50.0f, axis, 1916);

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
