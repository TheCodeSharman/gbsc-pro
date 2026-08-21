// Host-compiled unit tests for Tv5725::Axis -- `make -C test axis`.
// The bench measurements of 2026-08-03 to 2026-08-06 are the acceptance
// criteria. docs/firmware-geometry-engine.md.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// solved grid for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "CheckNear.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/AxisSolution.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PictureOrigin.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/RasterFit.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

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
