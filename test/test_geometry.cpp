// Host-compiled unit tests for the geometry -- `make -C test geometry`.
// The bench measurements of 2026-08-03 to 2026-08-06 are the acceptance
// criteria. docs/firmware-geometry-engine.md.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// solved grid for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "fake/Wire.h"

// The bus the register-touching sources link against. CaptureWindow reads the
// rasters it places a window against, so it comes with the seam.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/AxisSolution.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BlankingTiming.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Memory.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/ActiveImage.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PictureOrigin.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/RasterFit.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/RegisterSolution.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

// An absolute tolerance, because every tolerance here is a count of pixels
// rather than a proportion.
#define CHECK_NEAR(got, want, tol)                                             \
    CHECK_MESSAGE(std::fabs((double)(got) - (double)(want)) <= (double)(tol),  \
                  #got " = " << (double)(got) << ", wanted " << (double)(want) \
                             << " +-" << (double)(tol))

using namespace Tv5725;

// --- where the scaler starts writing -----------------------------------------

// measure_origin.py, 2026-08-05: the near edge crept up until the frozen
// scratch band vanished. (magnification, offset from VDS_?B_SP).
TEST_CASE("the write start is not a constant")
{
    SUBCASE("the horizontal write start matches every reading") {
        CHECK_NEAR(AxisHorizontal.originOffset(1.0009775171065494f), 80, 1.0);
        CHECK_NEAR(AxisHorizontal.originOffset(2.0f), 105, 1.0);
        CHECK_NEAR(AxisHorizontal.originOffset(3.2f), 135, 1.0);
    }

    SUBCASE("the vertical write start matches every reading") {
        // Nearly flat: a line buffer, with no interpolator to feed.
        CHECK_NEAR(AxisVertical.originOffset(1.0009775171065494f), 1, 1.0);
        CHECK_NEAR(AxisVertical.originOffset(2.0f), 2, 1.0);
        CHECK_NEAR(AxisVertical.originOffset(3.4133333333333336f), 3, 1.0);
    }

    SUBCASE("the readings recorded as unexplained also fit") {
        // 78 and 94 were carried as irreconcilable. One formula, four
        // magnifications -- they were measured at different scales.
        CHECK_NEAR(AxisHorizontal.originOffset(1.001f), 80, 1.5);
        CHECK_NEAR(AxisHorizontal.originOffset(1.575f), 93, 1.5);
    }

    SUBCASE("the bezel is not the write start") {
        // CORNER_V 63 is PANEL_VISIBLE_TOP: 37 + 26 was the panel's edge.
        CHECK(AxisVertical.originOffset(2.0f) < 5);
        CHECK_NEAR(AxisHorizontal.originOffset(1.58f), 94, 1.0);
    }
}

// --- produced is a pure multiply ---------------------------------------------

TEST_CASE("produced is a pure multiply")
{
    SUBCASE("produced is a pure multiply on both axes") {
        CHECK_NEAR(Scale(512).produced(400), 800.0, 0.001);
        CHECK_NEAR(Scale(300).produced(200), 682.67, 0.01);
    }

    SUBCASE("the two axes differ only in where the write starts") {
        // The old model had them as different shapes. Same shape; what differs
        // is the pipeline latency before the first write.
        CHECK(Scale(512).produced(400) == Scale(512).produced(400));
        CHECK(AxisHorizontal.startPerMag() > 20 * AxisVertical.startPerMag());
    }

    SUBCASE("a scale of zero is a dropped read, not a setting") {
        CHECK(Scale(0).magnification() == 0.0f);
        CHECK(Scale(0).produced(798) == 0.0f);
    }
}

// The horizontal capture position is effective only in steps of 2 IF units, so
// a press that rounds to 1 moves the register without moving the picture.
// Vertical moves on every unit. docs/scaler-geometry-model.md
TEST_CASE("one step is visible to the user")
{
    // 1024/606, the magnification these steps are checked at.
    const float measured = Scale(606).magnification();

    SUBCASE("one horizontal tap moves a whole granule") {
        CHECK(AxisHorizontal.captureGranularity() == 2);
        CHECK(AxisHorizontal.stepUnits(1, measured) == 2);
        CHECK(AxisHorizontal.stepUnits(-1, measured) == -2);
    }

    SUBCASE("every horizontal step is a multiple of the granule") {
        for (int16_t pixels = 1; pixels <= 40; ++pixels) {
            int16_t units = AxisHorizontal.stepUnits(pixels, measured);
            CHECK(units >= AxisHorizontal.captureGranularity());
            CHECK(units % AxisHorizontal.captureGranularity() == 0);
        }
    }

    SUBCASE("a bigger request keeps its size rather than being rounded up") {
        // The pads ask for 8 output pixels, which is 4.73 units here. Nearest
        // granule is 4 -- 6.8 px -- not 6, which would overshoot by more than
        // rounding down undershoots.
        CHECK(AxisHorizontal.stepUnits(8, measured) == 4);
    }

    SUBCASE("the vertical axis moves at least one line") {
        const float vertical = Scale(487).magnification();
        CHECK(AxisVertical.captureGranularity() == 1);
        CHECK(AxisVertical.stepUnits(1, vertical) == 1);
        CHECK(AxisVertical.stepUnits(-1, vertical) == -1);
        CHECK(AxisVertical.stepUnits(8, vertical) == 4);
    }
}

// Measured 2026-08-05 with measure_produced.py, floor() of the real value.
// Taken with VDS_HB_SP 35 / VDS_VB_SP 37 against a corner assumed constant at
// 129 / 63; the corner was not constant, so they are re-expressed against where
// the scaler actually starts. Nothing is refitted.
struct Reading { unsigned capture; unsigned scale; int recorded; };

static const Reading MeasuredH[] = {
    {798, 1023, 785}, {798, 800, 1014}, {400, 1023, 386},
    {400, 512, 811}, {200, 320, 680},
};
static const Reading MeasuredV[] = {
    {511, 1023, 487}, {511, 700, 723}, {511, 512, 997},
    {300, 1023, 275}, {300, 512, 575}, {200, 300, 658},
};

static double recordedFarEdge(const Reading &r, const Axis &axis, int winSp,
                              int assumedCorner)
{
    double m = 1024.0 / r.scale;
    double writeStart = winSp + axis.originOffset((float)m);
    return writeStart + r.capture * m - assumedCorner;
}

TEST_CASE("every measured reading is reproduced")
{
    SUBCASE("a pure multiply reproduces every horizontal reading") {
        for (const Reading &r : MeasuredH)
            CHECK_NEAR(recordedFarEdge(r, AxisHorizontal, 35, 129), r.recorded,
                       AxisHorizontal.margin());
    }

    SUBCASE("a pure multiply reproduces every vertical reading") {
        for (const Reading &r : MeasuredV)
            CHECK_NEAR(recordedFarEdge(r, AxisVertical, 37, 63), r.recorded,
                       AxisVertical.margin());
    }
}

// --- placing the picture ------------------------------------------------------

TEST_CASE("the picture is centred on the raster")
{
    PictureOrigin p = AxisHorizontal.placePicture(845, 1445, 2.0f);

    SUBCASE("the picture is centred on the raster, not pinned to a panel edge") {
        // PANEL_VISIBLE_LEFT was carried as 127 and measured 90 on the bench TV.
        CHECK(p.corner() == 300);
        CHECK(1445 - (p.corner() + 845) == p.corner());
    }

    SUBCASE("centring moves the memory window, not the picture") {
        // At x2 the scaler starts 105 px after VDS_?B_SP.
        CHECK_NEAR(p.windowStop() + AxisHorizontal.originOffset(2.0f), p.corner(), 0.5);
    }

    SUBCASE("a picture too wide to centre is pinned as far over as it goes") {
        PictureOrigin wide = AxisHorizontal.placePicture(1400, 1445, 2.0f);
        CHECK(wide.windowStop() == AxisHorizontal.windowStopMin());
        CHECK(wide.corner() == (int32_t)lrintf(AxisHorizontal.windowStopMin()
                                              + AxisHorizontal.originOffset(2.0f)));
    }

    SUBCASE("the memory window is never placed where the picture corrupts") {
        // Below 8 the display corrupts.
        const float magnifications[] = {1.001f, 1.416f, 2.0f, 3.2f, 4.0f};
        for (float m : magnifications) {
            PictureOrigin any = AxisHorizontal.placePicture(1400, 1445, m);
            CHECK(any.windowStop() >= 8);
        }
    }
}

// --- the picture is computed, never inherited ---------------------------------

TEST_CASE("the picture is made as big as the raster allows")
{
    RasterFit fit = AxisHorizontal.fitToRaster(798, 1445);

    SUBCASE("the picture is made as big as the raster allows") {
        // Not "as big as the room": the write offset costs startPerMag x
        // magnification at both ends. The observable is that nothing more could
        // be claimed -- the memory window lands hard against its floor.
        CHECK(((fit.scale().reg() >= AxisHorizontal.scaleMin())
               && (fit.scale().reg() <= Scale::Max)));
        PictureOrigin placed = AxisHorizontal.placePicture(fit.produced(), 1445,
                                              fit.scale().magnification());
        CHECK(placed.windowStop() >= AxisHorizontal.windowStopMin());
        CHECK(placed.windowStop() <= AxisHorizontal.windowStopMin() + 4);
    }

    SUBCASE("the picture gives up exactly the write offset and nothing more") {
        // Once, not twice: the offset is paid before the first write and there
        // is nothing after the last one.
        CHECK_NEAR(fit.produced(),
                   AxisHorizontal.maxDisplayWindow(1445)
                       - AxisHorizontal.startPerMag() * Scale::Unity
                             / fit.scale().reg(),
                   2.0);
    }

    SUBCASE("a smaller capture still fills the raster") {
        // 800, not 400. Below Scale::minimumCapture(1445) = 706 the
        // magnification runs out and the picture CANNOT fill the raster, so a
        // capture under it is testing the ceiling rather than the fit.
        RasterFit small = AxisHorizontal.fitToRaster(800, 1445);
        PictureOrigin smallPlaced = AxisHorizontal.placePicture(
            small.produced(), 1445, Scale::Unity / (float)small.scale().reg());
        CHECK(smallPlaced.windowStop() <= AxisHorizontal.windowStopMin() + 4);
    }

    SUBCASE("the scale register bounds how big the picture can get") {
        // A capture too small to fill the raster is bounded at 2.048x. That is
        // a limit, not a failure.
        RasterFit tiny = AxisHorizontal.fitToRaster(60, 1445);
        CHECK(tiny.scale().reg() == AxisHorizontal.scaleMin());
        CHECK(tiny.produced() < AxisHorizontal.maxDisplayWindow(1445));
    }

    SUBCASE("the placed picture always clears the write floor") {
        // fit_to_raster gives a scale step back rather than overflow: rounding
        // the scale down makes the picture a shade larger than solved for,
        // which can push the near edge below VDS_?B_SP's floor.
        for (unsigned capture = 100; capture <= 1200; capture += 50) {
            RasterFit f = AxisHorizontal.fitToRaster(capture, 1445);
            PictureOrigin p = AxisHorizontal.placePicture(f.produced(), 1445,
                                             f.scale().magnification());
            CHECK(p.windowStop() >= AxisHorizontal.windowStopMin());
        }
    }
}

// --- the framing, held as state rather than read back ------------------------

TEST_CASE("the framing is held as state and the window is derived")
{
    BlankingTiming wide = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
    BlankingTiming centred = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);

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
            BlankingTiming in = ActiveImage(PanAndZoom(units, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
            CHECK((wide.start() - wide.stop()) - (in.start() - in.stop()) == units);
        }
    }

    SUBCASE("one unit is one unit at a narrow capture too") {
        // Where the proportional control was at its coarsest relative to width.
        BlankingTiming narrow = ActiveImage(PanAndZoom(800, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        BlankingTiming narrower = ActiveImage(PanAndZoom(801, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK((narrow.start() - narrow.stop()) - (narrower.start() - narrower.stop())
              == 1);
    }

    SUBCASE("zoom out and back returns the window exactly") {
        // Integer units, so this is exact by construction rather than by the
        // rounding happening to cancel.
        BlankingTiming there = ActiveImage(PanAndZoom(137, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        BlankingTiming back = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(((back.stop() == wide.stop()) && (back.start() == wide.start())));
        CHECK(there.start() - there.stop() < wide.start() - wide.stop());
    }

    SUBCASE("panning moves the window and keeps its width") {
        BlankingTiming moved = ActiveImage(PanAndZoom(0, 0, +40, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(moved.stop() == centred.stop() + 40);
        CHECK(moved.start() - moved.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a pan is clamped to the line rather than crossing it") {
        BlankingTiming far_right = ActiveImage(PanAndZoom(0, 0, +5000, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(far_right.start() <= 1126);
        CHECK(far_right.start() - far_right.stop() == centred.start() - centred.stop());
        BlankingTiming far_left = ActiveImage(PanAndZoom(0, 0, -5000, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(far_left.stop() == 0);
        CHECK(far_left.start() - far_left.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a zoom in never crops the capture away to nothing") {
        BlankingTiming tiny = ActiveImage(PanAndZoom(5000, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(tiny.start() - tiny.stop() >= MinimumCapture);
    }

    SUBCASE("zooming out never puts the capture stop on the wrap point") {
        // IF_VB_ST rolls at 2 x (VTOTAL + 1) and does not clamp, so a window
        // written onto that value rolls the frame -- which reads as the picture
        // jumping rather than as a capture fault. Three steps of zoom-out reach
        // it: 0..624 of a 624 half-line frame.
        for (int16_t units : {-1, -20, -60, -100, -500, -5000}) {
            BlankingTiming w = ActiveImage(PanAndZoom(0, units, 0, 0)).capture(InputLine(624), 50.0f, AxisVertical, 0);
            CHECK(w.start() <= 623);
        }
    }

    SUBCASE("the same wrap bound applies horizontally") {
        for (int16_t units : {-1, -60, -200, -300, -5000}) {
            BlankingTiming w = ActiveImage(PanAndZoom(units, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
            CHECK(w.start() <= 1276);
        }
    }

    SUBCASE("a pan cannot put the capture stop on the wrap point either") {
        // pan_capture() bounds it the same way, for the same reason.
        for (int16_t p : {+5000, +600, -5000}) {
            CHECK(ActiveImage(PanAndZoom(0, 0, 0, p)).capture(InputLine(624), 50.0f, AxisVertical, 0).start()
                  <= InputLine(624).lastCapture());
            CHECK(ActiveImage(PanAndZoom(0, 0, p, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0).start()
                  <= InputLine(1126).lastCapture());
        }
    }

    SUBCASE("the capture stop never lands on the line reset itself") {
        for (int16_t p : {+5000, +600, +132}) {
            CHECK(ActiveImage(PanAndZoom(0, 0, p, 0)).capture(InputLine(1265), 50.0f, AxisHorizontal, 0).start() <= 1263);
        }
    }

    SUBCASE("a zoom out never runs past the line") {
        BlankingTiming huge = ActiveImage(PanAndZoom(-5000, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);
        CHECK(huge.start() <= 1126);
        CHECK(huge.stop() <= huge.start());
    }

    SUBCASE("the vertical axis derives from half-lines the same way") {
        BlankingTiming v = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(624), 50.0f, AxisVertical, 0);
        CHECK(v.start() - v.stop() == ActiveImage::defaultWidth(InputLine(624), 50.0f, AxisVertical));
        CHECK_NEAR((int)v.stop(), 624 - (int)v.start(), 1.0);
    }
}

// --- the framing must never hold a value the line cannot realise --------------

// capture() clamps the WINDOW, and the FRAMING has to be clamped with it, or a
// press past the edge leaves the framing beyond anything achievable and every
// smaller press back produces an identical window -- the control dies in that
// direction. Only the accelerating hold ramp presses that far.
//
// On the bench line: 1126 units, default width 890, so the centred start is 118
// and the largest the line allows is 1124 - 890 = 234, giving a largest
// achievable horizontalPan of 116.
TEST_CASE("the scale floor is derived from the magnification, on both axes")
{
    // Nothing in the part settles the floor -- RD-5725-1.1 states no minimum for
    // VDS_HSCALE -- so it is derived from a magnification chosen deliberately.
    // A swept constant cannot hold it: Axis::minimumCapture() is raster over
    // magnification while ActiveImage::defaultWidth() depends on the input line
    // alone, so widening the raster eats the zoom travel.
    CHECK(AxisHorizontal.scaleMin() == Scale::Min);
    CHECK_NEAR(Scale(AxisHorizontal.scaleMin()).magnification(), 4.0, 0.001);

    SUBCASE("the VERTICAL keeps the register's own floor") {
        // The floor belongs to the AXIS rather than to Scale even though the two
        // agree today: an axis wanting a different magnification should be able
        // to say so without moving the register's own limit.
        CHECK(AxisVertical.scaleMin() == Scale::Min);
        CHECK(AxisVertical.scaleMin() == 256);
    }

    SUBCASE("no capture, however small, is scaled past its axis floor") {
        for (uint16_t capture = 16; capture <= 1126; capture += 7) {
            CHECK(AxisHorizontal.fitToRaster(capture, 1445).scale() >= AxisHorizontal.scaleMin());
            CHECK(AxisVertical.fitToRaster(capture, 1126).scale() >= AxisVertical.scaleMin());
        }
    }

    SUBCASE("and the zoom stops there rather than shrinking the picture") {
        // ActiveImage::clampWidth stops the capture where the magnification runs
        // out, so the picture stays full size and the control simply stops.
        CHECK(AxisHorizontal.minimumCapture(1445) == 362);
        CHECK(AxisVertical.minimumCapture(1126) == 282);
    }
}

TEST_CASE("a press that overshoots the edge leaves no dead zone")
{
    const uint16_t Units = 1126;
    const float Rate = 50.0f;

    SUBCASE("zoom stops at the magnification ceiling instead of letterboxing") {
        // Zooming past the ceiling otherwise shrinks the capture while the
        // scale sits at its minimum, so the picture gets smaller on screen and
        // the display window closes in around it. At 4.0x a 1126 raster floors
        // at 1126 * 256 / 1024 = 282, measured 2026-08-09 as the last capture
        // that fills the screen before the bars.
        const uint16_t Raster = 1126;
        CHECK(AxisVertical.minimumCapture(Raster) == 282);

        ActiveImage f;
        f.zoomBy(0, 5000);                     // hard against the stop
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
        f.zoomBy(5000, 0);
        f.clampToLine(InputLine(1126), Rate, AxisHorizontal, 0);
        BlankingTiming got = f.capture(InputLine(1126), Rate, AxisHorizontal, 0);
        CHECK(got.start() - got.stop() == MinimumCapture);
    }

    SUBCASE("an overshooting pan is brought back to what the line allows") {
        ActiveImage f;
        f.panBy(200, 0);                       // one accelerated press, way past
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(f.horizontalPan() == 116);
    }

    SUBCASE("and one unit back then actually moves the window") {
        ActiveImage f;
        f.panBy(200, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        BlankingTiming at_edge = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);

        f.panBy(-1, 0);
        BlankingTiming back = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(back.stop() < at_edge.stop());
    }

    SUBCASE("the same holds at the other end of the line") {
        ActiveImage f;
        f.panBy(-200, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(f.horizontalPan() == -118);

        BlankingTiming at_edge = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        f.panBy(+1, 0);
        CHECK(f.capture(InputLine(Units), Rate, AxisHorizontal, 0).stop() > at_edge.stop());
    }

    SUBCASE("vertically too, which is the 'or bottom' half of the report") {
        ActiveImage f;
        f.panBy(0, -400);
        f.clampToLine(InputLine(624), Rate, AxisVertical, 0);
        BlankingTiming at_edge = f.capture(InputLine(624), Rate, AxisVertical, 0);
        CHECK(at_edge.stop() == 0);

        f.panBy(0, +1);
        CHECK(f.capture(InputLine(624), Rate, AxisVertical, 0).stop() > at_edge.stop());
    }

    SUBCASE("an overshooting zoom is brought back the same way") {
        // clampWidth() pins the width at MinimumCapture, and the same dead zone
        // forms in horizontalZoom_ if the framing is left holding the overshoot.
        ActiveImage f;
        f.zoomBy(5000, 0);
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        BlankingTiming tightest = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(tightest.start() - tightest.stop() == MinimumCapture);

        f.zoomBy(-1, 0);
        BlankingTiming wider = f.capture(InputLine(Units), Rate, AxisHorizontal, 0);
        CHECK(wider.start() - wider.stop() > tightest.start() - tightest.stop());
    }

    SUBCASE("clamping a framing that is already reachable changes nothing") {
        ActiveImage f{PanAndZoom(7, 5, 20, -13)};
        ActiveImage before = f;
        f.clampToLine(InputLine(Units), Rate, AxisHorizontal, 0);
        f.clampToLine(InputLine(624), Rate, AxisVertical, 0);
        CHECK(f == before);
    }
}

// --- the capture window may never take the hsync pulse ------------------------

// The pulse is at the HEAD, and its width is derived from HLOW_LEN over
// PLLAD_MD rather than held as a constant. The tail is deliberately unbounded.
// docs/scaler-geometry-model.md "The two green regions in an IF line".

// IF_LINE_ST/SP is the input formatter's PROGRESSIVE line window -- line double
// timing, so deinterlacing's rather than the picture's -- and it has to span
// exactly one line from wherever it starts.
TEST_CASE("the progressive line window spans exactly one line")
{
    const InputLine Bench = InputLine::measured(1126, 160, 2250);

    SUBCASE("it starts where IF_LINE_ST says and runs a whole line") {
        // The bench value: 64 + 1126 = 1190.
        CHECK(Bench.progressiveStop(64) == 1190);
    }

    SUBCASE("a different start moves the stop with it") {
        // ofw_RGBS and ofw_ypbpr ship IF_LINE_ST 0x18.
        CHECK(Bench.progressiveStop(24) == 1150);
    }

    SUBCASE("a longer line makes a longer window") {
        // The whole reason this cannot be a constant: PLLAD_MD moves and the
        // line moves with it.
        CHECK(InputLine::measured(1057, 128, 2114).progressiveStop(64) == 1121);
    }

    SUBCASE("it may run past the end of the line, and that is not a fault") {
        // A stop of 1190 on a 1126 unit line was once reported as a stray write. It is a stop position measured from a start, not a
        // position within the raster, so it rolls.
        CHECK(Bench.progressiveStop(64) > Bench.units());
    }
}

TEST_CASE("the capture window never takes the hsync pulse")
{
    // The bench RiscPC: a 7.1% hsync duty, measured 2026-08-09 as HLOW_LEN 181
    // of PLLAD_MD 2553 and read here at the 2250 the write limit caps the
    // divider to. 160 x 1126 / 2250 = 80.07 -> 81.
    const uint16_t BenchHlow = 160, BenchAdcLine = 2250, BenchUnits = 1126;
    const InputLine Bench = InputLine::measured(BenchUnits, BenchHlow, BenchAdcLine);
    const float Rate = 50.0f;

    SUBCASE("the pulse width comes from the hsync duty") {
        CHECK(Bench.syncUnits() == 81);
    }

    SUBCASE("a wider pulse excludes proportionally more") {
        // 800x600@60 is hsync 128 of 1056, a duty of 0.121 -- nearly twice the
        // bench source's. A fixed guard would under-clip it.
        CHECK(InputLine::measured(1126, 128, 1056).syncUnits() == 137);
    }

    SUBCASE("zooming all the way out stops clear of the sync") {
        BlankingTiming huge = ActiveImage(PanAndZoom(-5000, 0, 0, 0)).capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(huge.stop() >= Bench.syncUnits());
    }

    SUBCASE("and takes the tail down to the line reset, which it may not have") {
        BlankingTiming huge = ActiveImage(PanAndZoom(-5000, 0, 0, 0)).capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(huge.start() == BenchUnits - 2);
    }

    SUBCASE("panning to the left stop cannot walk into the sync") {
        BlankingTiming left = ActiveImage(PanAndZoom(0, 0, -5000, 0)).capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(left.stop() >= Bench.syncUnits());
    }

    SUBCASE("panning to the right stop reaches the last unit before the reset") {
        BlankingTiming right = ActiveImage(PanAndZoom(0, 0, +5000, 0)).capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(right.start() == BenchUnits - 2);
    }

    SUBCASE("the resting picture is untouched") {
        // The default capture is 890 units of a 1126 unit line and the pulse
        // takes 81 at the head, so 1045 remain: the picture nobody complained
        // about must not move by so much as a unit.
        BlankingTiming guarded = ActiveImage(PanAndZoom()).capture(Bench, Rate, AxisHorizontal, 0);
        BlankingTiming whole = ActiveImage(PanAndZoom()).capture(InputLine(BenchUnits), Rate, AxisHorizontal, 0);
        CHECK(guarded.stop() == whole.stop());
        CHECK(guarded.start() == whole.start());
    }

    SUBCASE("zoom out still has somewhere to go") {
        // Clipping the sync must not cost the reach that finds active video the
        // 0.76 assumption crops.
        BlankingTiming rest = ActiveImage(PanAndZoom()).capture(Bench, Rate, AxisHorizontal, 0);
        BlankingTiming out = ActiveImage(PanAndZoom(-40, 0, 0, 0)).capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(out.start() - out.stop() == (rest.start() - rest.stop()) + 40);
    }

    SUBCASE("the framing is clamped to the same bound the window is") {
        // capture() clamps the window and clampToLine() clamps the framing; a
        // difference of one unit between them is the dead zone of 2026-08-09.
        ActiveImage f;
        f.panBy(-5000, 0);
        f.clampToLine(Bench, Rate, AxisHorizontal, 0);
        BlankingTiming at_edge = f.capture(Bench, Rate, AxisHorizontal, 0);
        CHECK(at_edge.stop() == Bench.syncUnits());

        f.panBy(+1, 0);
        CHECK(f.capture(Bench, Rate, AxisHorizontal, 0).stop() > at_edge.stop());
    }

    SUBCASE("an unmeasurable duty falls back to what the retimer is set for") {
        // HLOW_LEN is a live measurement and rails; the firmware discards a
        // reading outside 0.041..0.152 too (gbs-control.ino:4858). Failing open
        // would restore the green bands, so the fallback is the fraction
        // SP_RT_HS_SP = PLLAD_MD x 0.93 configures the retimer for.
        for (uint16_t railed : {(uint16_t)0, (uint16_t)4095, (uint16_t)10}) {
            // ceil(1126 x 0.07) = 79, against the 81 the duty measures.
            CHECK(InputLine::measured(1126, railed, 2250).syncUnits() == 79);
        }
    }

    SUBCASE("a line with nothing measured keeps all of itself") {
        CHECK(InputLine(1126).syncUnits() == 0);
        CHECK(InputLine(1126).firstCapture() == 0);
        CHECK(InputLine(1126).lastCapture() == 1124);
    }
}

TEST_CASE("the capture stops at the write limit, however long the line is")
{
    // SourceMeasurement caps the divider so the line arrives inside the limit, but
    // adopt() takes whatever a custom preset or a bypass switch left in
    // PLLAD_MD, so a longer line still reaches the engine. Past the limit
    // nothing is written, and a window that reaches there loses the picture in
    // it rather than showing it. docs/capture-limits.md
    CHECK(InputLine(1277).lastCapture() == InputLine::WriteLimitUnits);

    SUBCASE("a line already inside it is bound by its own wrap") {
        // The two bounds meet at the divider SourceMeasurement now chooses: 1126 units,
        // where the wrap is the tighter by two.
        CHECK(InputLine(1126).lastCapture() == 1124);
        CHECK(InputLine(1126).lastCapture() < InputLine::WriteLimitUnits);
    }

    SUBCASE("the head guard still applies, and the two do not cross") {
        InputLine bench = InputLine::measured(1277, 181, 2553);
        CHECK(bench.firstCapture() < bench.lastCapture());
        CHECK(bench.capturable() == InputLine::WriteLimitUnits - bench.syncUnits());
    }
}

// --- solving one axis's four output registers ---------------------------------

TEST_CASE("the solver places every output register")
{
    // Bench reference: capture 798 at HSCALE 650 produces 1257 px on a 1445 px
    // line, so centred puts the corner at 94. It cannot go there -- at x1.575
    // the write start is 94.4 px after VDS_HB_SP, needing the register below its
    // floor of 8 -- so the picture is pushed right to 102.
    AxisSolution solved = AxisHorizontal.solve(798, Scale(650), 1445);

    SUBCASE("the solver centres the picture as far as the hardware allows") {
        CHECK(solved.origin() == 102);
        CHECK(solved.windowStop() == AxisHorizontal.windowStopMin());
    }

    SUBCASE("the memory window is exactly the display window") {
        // Allocate only what is displayed. Taking the whole raster is not free:
        // memory past the picture is memory the playback stage still walks, and
        // on the bench it showed as artefacts down the LEFT edge. EQUAL to the
        // display window, not merely under the last usable value.
        CHECK(solved.windowStart() == solved.displayStart());
        CHECK(solved.windowStart() <= 1443);
    }

    SUBCASE("the display window hugs the picture") {
        // A window sized for a different picture invalidated two of the
        // 2026-08-05 headroom measurements, by blanking where tearing shows.
        CHECK(solved.displayStop() == solved.origin());
        CHECK(solved.displayStart()
              == solved.origin() + (int32_t)solved.produced() - AxisHorizontal.margin());
    }

    SUBCASE("the solver corrects the thirteen pixel offset seen on the bench") {
        // VDS_DIS_HB_SP 129 against an origin of 116.
        CHECK(solved.displayStop() != 129);
    }

    SUBCASE("no returned register reaches the value that wraps") {
        // Tested at the boundary itself: off-by-one is the risk.
        AxisSolution h = AxisHorizontal.solve(500, Scale(650), 1445);
        CHECK(h.windowStart() < 1444);
        CHECK(h.displayStart() < 1444);
        AxisSolution v = AxisVertical.solve(500, Scale(650), 1126);
        CHECK(v.windowStart() < 1125);
        CHECK(v.displayStart() < 1125);
    }

    SUBCASE("the display window never runs past the last written pixel") {
        // VDS_DIS_?B_ST is where blanking STARTS, so it may equal origin +
        // produced but never exceed it. Rounding up shows scratch.
        AxisSolution tall = AxisVertical.solve(513, Scale(487), 1126);
        CHECK(tall.displayStart() <= tall.origin() + tall.produced());
    }

    SUBCASE("a vertical solve treats IF_VB as half lines") {
        // Reading them as whole lines doubles the picture -- the likeliest bug
        // here. 513 half-lines at VSCALE 660 is 795.9 output lines, not 1591.
        AxisSolution half = AxisVertical.solve(513, Scale(660), 1125);
        CHECK(((half.produced() > 795) && (half.produced() < 797)));
        CHECK(half.windowStart() < 1125);
    }
}

// --- everything from the capture and the raster alone -------------------------

TEST_CASE("nothing is inherited from the registers")
{
    // The bench state: 798 IF units captured on a 1126-unit line, 513 half-lines
    // of a 312-line frame, onto a 1445 x 1126 output raster.
    RegisterSolution s(798, 513, 1445, 1126);

    SUBCASE("both scales are computed, not read") {
        CHECK(((s.horizontalScale() >= AxisHorizontal.scaleMin()) && (s.horizontalScale() <= Scale::Max)));
        CHECK(((s.verticalScale() >= AxisVertical.scaleMin()) && (s.verticalScale() <= Scale::Max)));
    }

    SUBCASE("both memory windows clear their floor") {
        CHECK(s.horizontal().windowStop() >= AxisHorizontal.windowStopMin());
        CHECK(s.vertical().windowStop() >= AxisVertical.windowStopMin());
    }

    SUBCASE("neither window reaches the value that wraps") {
        CHECK(((s.horizontal().windowStart() < 1444) && (s.horizontal().displayStart() < 1444)));
        CHECK(((s.vertical().windowStart() < 1125) && (s.vertical().displayStart() < 1125)));
    }

    SUBCASE("the vertical picture is not doubled by reading half lines as lines") {
        // ~2200 would mean IF_VB was read as whole lines.
        CHECK(((s.vertical().produced() > 900) && (s.vertical().produced() < 1130)));
    }

    SUBCASE("the same capture always gives the same answer") {
        RegisterSolution again = RegisterSolution(798, 513, 1445, 1126);
        CHECK(((again.horizontalScale() == s.horizontalScale())
               && (again.verticalScale() == s.verticalScale())));
        CHECK(again.horizontal().windowStop() == s.horizontal().windowStop());
        CHECK(again.vertical().displayStart() == s.vertical().displayStart());
    }

    SUBCASE("a capture that reads zero yields no picture rather than a wrong one") {
        RegisterSolution dropped = RegisterSolution(0, 0, 1445, 1126);
        CHECK(dropped.horizontal().produced() == 0.0f);
        CHECK(dropped.vertical().produced() == 0.0f);
    }
}

// --- a capture window when there is not a usable one --------------------------

TEST_CASE("a nonsense capture is replaced, not trusted")
{
    // The stock preset leaves IF_VB_ST <= IF_VB_SP on 56 of 66 archived
    // snapshots, and what the chip means by that is not established. Rather
    // than decode a register state we are deleting, compute one.
    BlankingTiming w = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(1126), 50.0f, AxisHorizontal, 0);

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
            BlankingTiming any = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(units), 50.0f, AxisHorizontal, 0);
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
        BlankingTiming none = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(0), 50.0f, AxisHorizontal, 0);
        CHECK(((none.stop() == 0) && (none.start() == 0)));
    }
}

// --- the oracle for the drift check ------------------------------------------

// --- the active window, 2026-08-12 -------------------------------------------

TEST_CASE("the active window narrows the room before the picture")
{
    // The picture belongs in the raster's active window, not on the whole
    // raster. Charged ONCE, before the picture: a back porch is something the
    // line needs before active video, and there is no write floor after the last
    // pixel to mirror it onto.
    const uint16_t Raster = 1918;
    CHECK(AxisHorizontal.maxDisplayWindow(Raster, 140)
          < AxisHorizontal.maxDisplayWindow(Raster));
    CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 140), (Raster - 2) - 140, 0.01);

    SUBCASE("an active start below the write floor changes nothing") {
        // The write floor is physical and a back porch cannot argue with it, so
        // the room is bounded by whichever is LARGER.
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 40),
                   AxisHorizontal.maxDisplayWindow(Raster), 0.01);
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 0),
                   AxisHorizontal.maxDisplayWindow(Raster), 0.01);
    }

    SUBCASE("the vertical floor is not truncated to zero") {
        // AxisVertical's startConst is 0.2. An integer blankingBeforePicture would round it
        // away and move every vertical solve.
        CHECK(AxisVertical.blankingBeforePicture(0) > 0.0f);
        CHECK_NEAR(AxisVertical.blankingBeforePicture(0), 0.2, 0.001);
    }
}

// Only the near end has anything physical behind it: windowStopMin is the
// measured left-edge corruption floor and startConst is pipeline run-up before
// the first write. Nothing is written after the last pixel, so the far end owes
// neither. Charging either at the far end leaves a black bar down the right of
// every picture that no zoom closes, the scale being refitted on every solve.
TEST_CASE("only the near end pays the write floor")
{
    const uint16_t Raster = 1901;   // the bench raster at 108 MHz, 1125 lines
    const uint16_t Capture = 1055;
    const float FarEdge = Raster - 2;

    SUBCASE("the room gives up the floor once, not twice") {
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster),
                   FarEdge - (AxisHorizontal.windowStopMin()
                              + AxisHorizontal.startConst()), 0.01);
    }

    SUBCASE("the picture reaches the end of the line") {
        RasterFit fit = AxisHorizontal.fitToRaster(Capture, Raster);
        PictureOrigin placed = AxisHorizontal.placePicture(
            fit.produced(), Raster, fit.scale().magnification());
        float end = placed.corner() + fit.produced();
        CHECK(end <= FarEdge);
        CHECK(end > FarEdge - 8);
    }

    SUBCASE("the near edge still sits on the write floor") {
        // The left edge is already as far over as physics allows, and must not
        // move: below windowStopMin the picture corrupts.
        RasterFit fit = AxisHorizontal.fitToRaster(Capture, Raster);
        PictureOrigin placed = AxisHorizontal.placePicture(
            fit.produced(), Raster, fit.scale().magnification());
        CHECK(placed.windowStop() >= AxisHorizontal.windowStopMin());
        CHECK(placed.windowStop() <= AxisHorizontal.windowStopMin() + 2);
    }

    SUBCASE("a capture too small to fill the raster is still bounded") {
        RasterFit tiny = AxisHorizontal.fitToRaster(60, Raster);
        CHECK(tiny.scale().reg() == AxisHorizontal.scaleMin());
        CHECK(tiny.produced() < AxisHorizontal.maxDisplayWindow(Raster));
    }
}

TEST_CASE("the solution carries the front porch to both axes")
{
    const uint16_t Raster = 1916, Frame = 1126;
    const uint16_t StopH = 1852, StopV = 1121;

    RegisterSolution solved(1008, 532, Raster, Frame, StopH, StopV);
    CHECK(solved.horizontal().displayStart() <= (int32_t)StopH);
    CHECK(solved.vertical().displayStart() <= (int32_t)StopV);

    SUBCASE("and without one the raster edge still bounds it") {
        RegisterSolution plain(1008, 532, Raster, Frame);
        CHECK(plain.horizontal().displayStart() > (int32_t)StopH);
    }
}

// The raster's edge is the wrong far bound: a display window taken up to
// VDS_HSYNC_RST leaves too little front porch and the colours come out wrong.
// Measured on the bench, RiscPC 320x256@50 into a 1916 px raster, the window is
// good at 1900 and bad at 1910 -- a floor of about 16 px, which CEA-861's minimum
// front porch clears at 64. OutputRaster::activeStop is where it comes from.
TEST_CASE("the picture stops at the front porch, not at the raster edge")
{
    const uint16_t Raster = 1916;
    const uint16_t ActiveStop = 1852;   // 1916 less a 64 px front porch
    const uint16_t Capture = 1008;

    SUBCASE("the room gives up the front porch as well as the write floor") {
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 0, ActiveStop),
                   ActiveStop - (AxisHorizontal.windowStopMin()
                                 + AxisHorizontal.startConst()), 0.01);
    }

    SUBCASE("and the picture ends inside the front porch, not past it") {
        RasterFit fit = AxisHorizontal.fitToRaster(Capture, Raster, 0, ActiveStop);
        PictureOrigin placed = AxisHorizontal.placePicture(
            fit.produced(), Raster, fit.scale().magnification());
        float end = placed.corner() + fit.produced();
        CHECK(end <= (float)ActiveStop);
        CHECK(end > (float)ActiveStop - 8.0f);
    }

    SUBCASE("the display window closes by the front porch too") {
        AxisSolution solved = AxisHorizontal.solve(
            Capture, AxisHorizontal.fitToRaster(Capture, Raster, 0, ActiveStop).scale(),
            Raster, 0, ActiveStop);
        CHECK(solved.displayStart() <= (int32_t)ActiveStop);
    }

    SUBCASE("an activeStop of 0 keeps the raster edge, so nothing else moves") {
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 0, 0),
                   AxisHorizontal.maxDisplayWindow(Raster), 0.01);
    }
}

TEST_CASE("the picture starts no earlier than the back porch")
{
    const uint16_t Raster = 1918;
    PictureOrigin placed = AxisHorizontal.placePicture(1638.0f, Raster, 1.69f, 140);
    CHECK(placed.corner() >= 140);
    CHECK(placed.windowStop() >= (int32_t)AxisHorizontal.windowStopMin());

    SUBCASE("and symmetrically, so what is reserved near is reserved far") {
        CHECK_NEAR(Raster - (placed.corner() + 1638.0f), placed.corner(), 1.5);
    }

    SUBCASE("a picture too big to centre starts AT the back porch") {
        // Overscan off the far end rather than begin inside the blanking.
        CHECK(AxisHorizontal.placePicture(2400.0f, Raster, 2.0f, 140).corner() == 140);
    }

    SUBCASE("the default is the old behaviour exactly") {
        // This is what makes the change additive: the eleven bench readings this
        // suite encodes still describe the same arithmetic.
        for (uint16_t raster : {1445, 1918, 2301, 2877})
            for (float produced : {800.0f, 1253.0f, 2079.0f}) {
                PictureOrigin without = AxisHorizontal.placePicture(produced, raster, 1.5f);
                PictureOrigin zero = AxisHorizontal.placePicture(produced, raster, 1.5f, 0);
                CHECK(without.corner() == zero.corner());
                CHECK(without.windowStop() == zero.windowStop());
            }
    }
}

// `--dump` prints the solved grid for inspection by hand.
static void dumpGrid()
{
    const uint16_t rasters[] = {1445, 1716, 858};
    const Axis *axes[] = {&AxisHorizontal, &AxisVertical};
    const char *names[] = {"h", "v"};

    for (uint16_t raster : rasters) {
        for (unsigned capture = 60; capture <= 1200; capture += 37) {
            for (int i = 0; i < 2; ++i) {
                RasterFit f = axes[i]->fitToRaster(capture, raster);
                PictureOrigin p = axes[i]->placePicture(f.produced(), raster,
                                                    f.scale().magnification());
                std::printf("fit %s %u %u %u %.4f %d %d\n", names[i], raster,
                            capture, f.scale().reg(), f.produced(), p.corner(), p.windowStop());
            }
        }
    }

    // Three fixed scales, so all four output registers are compared.
    const uint16_t scales[] = {1023, 650, 320};
    for (uint16_t raster : rasters)
        for (unsigned capture = 100; capture <= 900; capture += 61)
            for (uint16_t scale : scales)
                for (int i = 0; i < 2; ++i) {
                    AxisSolution s = axes[i]->solve(capture, Scale(scale),
                                                    raster);
                    std::printf("solve %s %u %u %u %.4f %d %d %d %d %d\n",
                                names[i], raster, capture, scale, s.produced(),
                                s.origin(), s.windowStop(), s.windowStart(), s.displayStop(),
                                s.displayStart());
                }

    // solveFromCapture: the whole answer from capture and raster alone.
    for (uint16_t raster : rasters)
        for (unsigned ch = 100; ch <= 1100; ch += 83)
            for (unsigned cv = 100; cv <= 600; cv += 71) {
                RegisterSolution s(ch, cv, raster, 1126);
                std::printf("whole %u %u %u %u %u %d %d %d %d %d %d %d %d\n",
                            raster, ch, cv, s.horizontalScale().reg(), s.verticalScale().reg(),
                            s.horizontal().origin(), s.horizontal().windowStop(), s.horizontal().displayStop(), s.horizontal().displayStart(),
                            s.vertical().origin(), s.vertical().windowStop(), s.vertical().displayStop(), s.vertical().displayStart());
            }

    // The active window, which the grid above cannot see because it never varies
    // activeStart -- so a divergence in the new parameter would go unnoticed.
    for (uint16_t raster : {1918, 2301, 2877})
        for (uint16_t activeStart : {0, 40, 140, 167, 209, 400})
            for (unsigned capture = 200; capture <= 1200; capture += 143)
                for (int i = 0; i < 2; ++i) {
                    RasterFit f = axes[i]->fitToRaster(capture, raster, activeStart);
                    PictureOrigin p = axes[i]->placePicture(
                        f.produced(), raster, f.scale().magnification(), activeStart);
                    std::printf("active %s %u %u %u %u %.4f %d %d\n", names[i],
                                raster, activeStart, capture, f.scale().reg(),
                                f.produced(), p.corner(), p.windowStop());
                }

    // The horizontal default, which geometry_math has a twin for. There is no
    // Python vertical equivalent, so that half is covered by the host tests only.
    for (uint16_t units : {320, 624, 1277, 2048, 2559})
        for (float rate : {50.0f, 60.0f}) {
            BlankingTiming w = ActiveImage(PanAndZoom(0, 0, 0, 0)).capture(InputLine(units), rate, AxisHorizontal, 0);
            std::printf("default %u %.0f %u %u\n", units, rate, w.stop(), w.start());
        }

    // The framing path. capture() and clampToLine() recompute the same window
    // and must agree exactly, so both are printed either side of the clamp: a
    // divergence shows as the two windows differing on one row.
    for (uint16_t units : {320, 624, 1277, 2048})
        for (int16_t zoom : {0, 40, 200, -60})
            for (int16_t pan : {0, 50, 400, -400})
                for (int vertical = 0; vertical < 2; ++vertical) {
                    InputLine line(units, vertical ? 0 : (uint16_t)(units / 14));
                    ActiveImage framing{PanAndZoom(zoom, zoom, pan, pan)};
                    BlankingTiming before =
                        framing.capture(line, 50.0f, vertical ? AxisVertical : AxisHorizontal, 1916);
                    framing.clampToLine(line, 50.0f, vertical ? AxisVertical : AxisHorizontal, 1916);
                    BlankingTiming after =
                        framing.capture(line, 50.0f, vertical ? AxisVertical : AxisHorizontal, 1916);
                    std::printf("framing %u %d %d %d %u %u %d %d %u %u\n",
                                units, zoom, pan, vertical,
                                before.stop(), before.start(),
                                vertical ? framing.verticalZoom() : framing.horizontalZoom(),
                                vertical ? framing.verticalPan() : framing.horizontalPan(),
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

// --- the zoom stops before the memory bus tears the picture --------------------

// --- the memory bus is not solved here any more -------------------------------

TEST_CASE("the horizontal window goes where the geometry puts it")
{
    // Asserting an absence. Memory::fetchFor sizes PB_FETCH_NUM from the capture
    // width, which makes the beat independent of HSCALE, so there is no tearing
    // band left for the window to dodge and no table to consult.
    for (uint16_t capture = 400; capture <= 1009; capture += 3) {
        RegisterSolution solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        CHECK(solved.horizontal().windowStart() == solved.horizontal().displayStart());
    }
}

TEST_CASE("the scale is exactly what fitToRaster produced")
{
    // The scale must never fall back to a different HSCALE -- it has to move
    // fluidly with the zoom pad rather than jump. A shrink search moves it by up
    // to 56 counts and steps backwards 71 times across this range while the pad
    // goes one way.
    uint16_t previous = 0;
    for (uint16_t capture = 400; capture <= 1009; ++capture) {
        RegisterSolution solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        AxisSolution plain = AxisHorizontal.solve(capture, solved.horizontalScale(), 1445);
        CHECK(solved.horizontal().windowStop() == plain.windowStop());
        CHECK(solved.horizontal().windowStart() == plain.windowStart());
        CHECK(solved.horizontal().displayStop() == plain.displayStop());
        CHECK(solved.horizontal().displayStart() == plain.displayStart());
        CHECK(solved.horizontalScale().reg() >= previous);
        previous = solved.horizontalScale().reg();
    }
}

TEST_CASE("both axes allocate only the memory the picture occupies")
{
    // A property of the memory, not of one axis, so Axis::solve applies it and
    // both axes get it. The artefact was seen horizontally, but a rule holding
    // on one axis only would be a special case nobody measured.
    RegisterSolution solved(749, 512, 1445, 1126);
    CHECK(solved.horizontal().windowStart() == solved.horizontal().displayStart());
    CHECK(solved.vertical().windowStart() == solved.vertical().displayStart());

    SUBCASE("and neither reaches the value that wraps") {
        // VDS_VB_ST at VDS_VSYNC_RST rolls the frame; VDS_HB_ST at
        // VDS_HSYNC_RST wraps.
        CHECK(solved.horizontal().windowStart() <= 1445 - 2);
        CHECK(solved.vertical().windowStart() <= 1126 - 2);
    }
}


// --- the zoom floor follows from the magnification, not from a swept number ---

TEST_CASE("horizontal zoom keeps its travel when the raster widens")
{
    // The clamp is rasterTotal / maxMagnification: the smallest capture that
    // still fills the raster once VDS_HSCALE is at its floor. The numerator
    // moves with the output and the denominator does not, and the default
    // capture is a property of the INPUT line (1126 x 0.76 x 1.04), so the two
    // do not track -- when solveRaster() took the raster 1436 -> 1916, three
    // quarters of the horizontal zoom travel went with it, 307 units to 73.
    const uint16_t Raster = 1916;
    const uint16_t DefaultCapture = 890;  // ActiveImage::defaultWidth on this bench

    uint16_t floor = AxisHorizontal.minimumCapture(Raster);

    CHECK(floor == Raster / 4);  // 4.0x, the same the vertical axis already uses
    CHECK(DefaultCapture - floor > 400);
}

TEST_CASE("both axes magnify equally far, because nothing in the part says otherwise")
{
    // RD-5725-1.1 states no minimum for VDS_HSCALE -- regdef.txt:7684 gives only
    // the ratio, and the field is 10 bits -- so there is no hardware bound to
    // derive, which is why the horizontal floor was a swept number for so long.
    CHECK(AxisHorizontal.scaleMin() == AxisVertical.scaleMin());
}

TEST_CASE("a picture too small for the raster is blanked, not left open")
{
    // Below Scale::minimumCapture() the magnification runs out and `produced`
    // falls short of the raster. The room left over is not empty: playback
    // keeps fetching past the end of the written data, so an open window there
    // shows stale buffer. The display window has to stop where the picture does.
    const uint16_t raster = 1445;
    const uint16_t capture = 200;

    RasterFit fit = AxisHorizontal.fitToRaster(capture, raster);
    REQUIRE(fit.produced() < (float)raster);

    AxisSolution solved =
        AxisHorizontal.solve(capture, fit.scale(), raster, 0, 0);

    // the window IS the picture, and the room left over is black at both ends
    CHECK(solved.displayStart() - solved.displayStop()
          <= (int32_t)fit.produced() + 1);
    CHECK(solved.displayStop() > 100);
    CHECK((int32_t)raster - solved.displayStart() > 100);
}

// The capture stop is what the pan walks toward the end of the line, and past
// InputLine::lastCapture() the input formatter is writing blanking rather than
// video. The control has to stop before that rather than the output hiding it
// afterwards, so this is the invariant no framing may break.
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
                    const ActiveImage image{vertical ? PanAndZoom(0, z, 0, p)
                                                     : PanAndZoom(z, 0, p, 0)};
                    const BlankingTiming got =
                        image.capture(line, 50.0f, vertical ? AxisVertical : AxisHorizontal, 1916);

                    CHECK(got.start() <= line.lastCapture());
                    CHECK(got.stop() >= line.firstCapture());
                }
            }
        }
    }
}
