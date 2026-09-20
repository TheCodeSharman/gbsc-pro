// Host-compiled unit tests for Tv5725::Axis -- `make -C test axis`.
// The bench measurements are the acceptance criteria.
// docs/firmware-geometry-engine.md.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// solved grid for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "CheckNear.h"
#include "SketchSeam.h"
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

// (magnification, offset from VDS_?B_SP), measured at the near edge.
// docs/scaler-geometry-model.md
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
        // One formula over four magnifications: 78 and 94 are the same
        // relationship read at different scales.
        CHECK_NEAR(AxisHorizontal.originOffset(1.001f), 80, 1.5);
        CHECK_NEAR(AxisHorizontal.originOffset(1.575f), 93, 1.5);
    }

    SUBCASE("the bezel is not the write start") {
        // 63 is the panel's visible top, not where the scaler starts.
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

// Produced width, floored, expressed against where the scaler actually starts
// writing rather than against a fixed corner. docs/scaler-geometry-model.md
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

// Two units, which is a tolerance the readings can fail: the worst residual is
// 1.0 px horizontally and 1.6 lines vertically.
static const double ReadingTolerance = 2.0;

TEST_CASE("every measured reading is reproduced")
{
    SUBCASE("a pure multiply reproduces every horizontal reading") {
        for (const Reading &r : MeasuredH)
            CHECK_NEAR(recordedFarEdge(r, AxisHorizontal, 35, 129), r.recorded,
                       ReadingTolerance);
    }

    SUBCASE("a pure multiply reproduces every vertical reading") {
        for (const Reading &r : MeasuredV)
            CHECK_NEAR(recordedFarEdge(r, AxisVertical, 37, 63), r.recorded,
                       ReadingTolerance);
    }
}

// --- placing the picture ------------------------------------------------------

TEST_CASE("the picture is centred on the raster")
{
    PictureOrigin p = AxisHorizontal.placePicture(845, 1445, 2.0f);

    SUBCASE("the picture is centred on the raster, not pinned to a panel edge") {
        // PANEL_VISIBLE_LEFT is 90 on the bench TV.
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
        CHECK(((fit.scale().reg() >= Scale::Min)
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
        // 800, not 400. Below Axis::minimumCapture(1445) the magnification
        // runs out and the picture CANNOT fill the raster, so a capture under
        // it is testing the ceiling rather than the fit.
        RasterFit small = AxisHorizontal.fitToRaster(800, 1445);
        PictureOrigin smallPlaced = AxisHorizontal.placePicture(
            small.produced(), 1445, Scale::Unity / (float)small.scale().reg());
        CHECK(smallPlaced.windowStop() <= AxisHorizontal.windowStopMin() + 4);
    }

    SUBCASE("the scale register bounds how big the picture can get") {
        // A capture too small to fill the raster is bounded by the axis's
        // scale floor. That is a limit, not a failure.
        RasterFit tiny = AxisHorizontal.fitToRaster(60, 1445);
        CHECK(tiny.scale().reg() == Scale::Min);
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

TEST_CASE("the display window opens after the picture starts, not on it")
{
    // Where the first written pixel lands is MODELLED, and the model
    // under-estimates: measured at 1080p on a 2300 px raster, the engine opened
    // the window at 113 and the data did not arrive until about 145, so the gap
    // showed Y=U=V=0 -- a green band down the left, reachable by zooming out and
    // panning hard left. The far edge already gives back Axis::margin for the
    // same reason; the near edge gave back nothing.
    // docs/investigations/display-window-opens-early.md
    const uint16_t Raster = 2300, Capture = 1043;
    const AxisSolution solved = AxisHorizontal.solve(Capture, Scale(474), Raster);
    const PictureOrigin placed = AxisHorizontal.placePicture(
        Scale(474).produced(Capture), Raster, Scale(474).magnification());

    // One capture unit past the modelled corner: the origin marks where content
    // first appears, and that unit is only partly written.
    CHECK(solved.display().stop()
          > placed.corner());
    CHECK((float)solved.display().stop()
          <= (float)placed.corner() + Scale(474).magnification() + 1.0f);

    SUBCASE("the memory window still opens where the write does") {
        // It is the DISPLAY that must not show the gap. Opening the memory
        // window late moves the picture instead, which widens the band.
        CHECK(solved.memory().stop() == placed.windowStop());
    }
}

TEST_CASE("the capture is bounded by what the raster can actually show")
{
    // VDS_?SCALE divides 1024 and the register tops out at 1023, so the least
    // magnification the chip can express is 1.001: it cannot MINIFY. A capture
    // bigger than the room therefore produces a picture bigger than the room,
    // and the far end is cropped rather than shrunk -- with no register saying
    // so, because the scale is simply clamped.
    const uint16_t Raster = 525;      // a 480p frame
    const uint16_t ActiveStop = 516;  // less CEA's nine-line front porch
    const float room = AxisVertical.maxDisplayWindow(Raster, 0, ActiveStop);

    const uint16_t most = AxisVertical.maximumCapture(Raster, ActiveStop);
    CHECK(AxisVertical.fitToRaster(most, Raster, 0, ActiveStop).produced() <= room);

    SUBCASE("and the capture it bounds does not fit") {
        // 622 half-lines: what the line doubler offers on a 311-line source,
        // and what a framing free to take the whole capturable region asks for.
        CHECK(AxisVertical.fitToRaster(622, Raster, 0, ActiveStop).produced() > room);
    }

    SUBCASE("a raster with room for everything bounds nothing the input can hold") {
        // 1080p against the same source: the doubled frame fits with 1.8x to
        // spare, so the ceiling is above anything the input line can offer.
        CHECK(AxisVertical.maximumCapture(1126, 1117) > 622);
    }
}

TEST_CASE("the scale floor is derived from the magnification, on both axes")
{
    // Nothing in the part settles the floor -- RD-5725-1.1 states no minimum for
    // VDS_HSCALE -- so it is derived from a magnification chosen deliberately.
    // Past 3.0x the solve can no longer centre the picture and pins the memory
    // window at the write floor, where the scaler picks wrong samples and the
    // picture breaks up. Measured entering the floor at VDS_HSCALE 334 on two
    // sources, rasters 1920 and 1280. 1024/3 is 341.33, so 342 is the largest
    // magnification at or under 3.0. docs/known-issues.md
    CHECK(Scale(Scale::Min).magnification() <= 3.0f);
    CHECK_NEAR(Scale(Scale::Min).magnification(), 3.0, 0.01);

    SUBCASE("the floor clears the scale that enters the write floor") {
        CHECK(Scale::Min > 334);
    }

    SUBCASE("no capture, however small, is scaled past the floor") {
        for (uint16_t capture = 16; capture <= 1126; capture += 7) {
            CHECK(AxisHorizontal.fitToRaster(capture, 1445).scale() >= Scale::Min);
            CHECK(AxisVertical.fitToRaster(capture, 1126).scale() >= Scale::Min);
        }
    }

    SUBCASE("and the zoom stops there rather than shrinking the picture") {
        // VideoPath::zoom() stops the capture where the magnification runs out,
        // so the picture stays full size and the control simply stops.
        CHECK(AxisHorizontal.minimumCapture(1445) == 436);
        CHECK(AxisVertical.minimumCapture(1126) == 375);
    }
}

// --- solving one axis's four output registers ---------------------------------

TEST_CASE("the solver places every output register")
{
    // Bench reference: capture 798 at HSCALE 650 produces 1257 px on a 1445 px
    // line, so centred puts the corner at 94. It cannot go there -- at x1.575
    // the write start is 94.4 px after VDS_HB_SP, needing the register below its
    // floor of 8 -- so the picture is pushed right to 102.
    const Scale scale(650);
    AxisSolution solved = AxisHorizontal.solve(798, scale, 1445);

    SUBCASE("the solver centres the picture as far as the hardware allows") {
        // The MEMORY window opens where the write does; the display window
        // opens one capture unit later, on the first unit fully written.
        CHECK(solved.display().stop() == 104);
        CHECK(solved.memory().stop() == AxisHorizontal.windowStopMin());
    }

    SUBCASE("the memory window is exactly the display window") {
        // Allocate only what is displayed. Taking the whole raster is not free:
        // memory past the picture is memory the playback stage still walks, and
        // on the bench it showed as artefacts down the LEFT edge. EQUAL to the
        // display window, not merely under the last usable value.
        CHECK(solved.memory().start() == solved.display().start());
        CHECK(solved.memory().start() <= 1443);
    }

    SUBCASE("the display window hugs the picture") {
        // A window sized for a different picture blanks where tearing shows,
        // so a headroom measurement taken through one is worthless. It gives
        // back a margin at each end -- the write origin's at the near one, and
        // at the far one the capture unit the scaler interpolates past the last
        // one written, plus the unit flooring costs.
        const float picture = (float)solved.memory().stop()
                            + AxisHorizontal.originOffset(scale.magnification())
                            + solved.produced();
        CHECK((float)solved.display().start() <= picture);
        CHECK(picture - (float)solved.display().start()
              <= scale.magnification() + 1.0f);
    }

    SUBCASE("the solver corrects the thirteen pixel offset seen on the bench") {
        // VDS_DIS_HB_SP 129 against an origin of 116.
        CHECK(solved.display().stop() != 129);
    }

    SUBCASE("no returned register reaches the value that wraps") {
        // Tested at the boundary itself: off-by-one is the risk.
        AxisSolution h = AxisHorizontal.solve(500, Scale(650), 1445);
        CHECK(h.memory().start() < 1444);
        CHECK(h.display().start() < 1444);
        AxisSolution v = AxisVertical.solve(500, Scale(650), 1126);
        CHECK(v.memory().start() < 1125);
        CHECK(v.display().start() < 1125);
    }

    SUBCASE("the display window never runs past the last written pixel") {
        // VDS_DIS_?B_ST is where blanking STARTS, so it may equal origin +
        // produced but never exceed it. Rounding up shows scratch.
        AxisSolution tall = AxisVertical.solve(513, Scale(487), 1126);
        CHECK(tall.display().start() <= tall.display().stop() + tall.produced());
    }

    SUBCASE("a vertical solve does not double the capture it is given") {
        // Doubling it is the likeliest bug here: 513 units at VSCALE 660 is
        // 795.9 output lines, not 1591.
        AxisSolution half = AxisVertical.solve(513, Scale(660), 1125);
        CHECK(((half.produced() > 795) && (half.produced() < 797)));
        CHECK(half.memory().start() < 1125);
    }
}

// --- the active window ------------------------------------------------------

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
        CHECK(tiny.scale().reg() == Scale::Min);
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

    // On the display window rather than on where the write ends: the write is
    // fractional and the window closes on its floor, so a picture reaching the
    // porch to within a pixel is emitted blank from the porch.
    SUBCASE("and the picture fills the line up to the front porch, not past it") {
        RasterFit fit = AxisHorizontal.fitToRaster(Capture, Raster, 0, ActiveStop);
        AxisSolution solved = AxisHorizontal.solve(Capture, fit.scale(), Raster,
                                                   0, ActiveStop);
        CHECK(solved.display().start() <= (int32_t)ActiveStop);
        CHECK(solved.display().start() > (int32_t)ActiveStop - 8);
    }

    SUBCASE("an activeStop of 0 keeps the raster edge, so nothing else moves") {
        CHECK_NEAR(AxisHorizontal.maxDisplayWindow(Raster, 0, 0),
                   AxisHorizontal.maxDisplayWindow(Raster), 0.01);
    }
}

TEST_CASE("the scale is not bumped for an overshoot the window already clips")
{
    // The bench 1080p vertical at the default framing: 480 captured lines into
    // the 1080-line active region 41..1121. The ideal scale of 455.11 rounds to
    // 455 and produces 1080.26, a quarter of a line past the far bound, which
    // the display window closes on anyway. One unit of VDS_VSCALE is worth 2.37
    // output lines here, so bumping the scale to clear that quarter line paints
    // two of them black.
    const uint16_t Raster = 1125, ActiveStart = 41, ActiveStop = 1121;
    const uint16_t Capture = 480;

    RasterFit fit = AxisVertical.fitToRaster(Capture, Raster, ActiveStart,
                                             ActiveStop);
    CHECK(fit.produced() > (float)(ActiveStop - ActiveStart) - 1.0f);

    SUBCASE("and the display window still closes by the front porch") {
        AxisSolution solved = AxisVertical.solve(Capture, fit.scale(), Raster,
                                                 ActiveStart, ActiveStop);
        CHECK(solved.display().start() <= (int32_t)ActiveStop);
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

    SUBCASE("omitting the active window places the picture identically") {
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
    // The stop is the room the raster offers over maxMagnification: the
    // smallest capture that still reaches VDS_HSCALE's floor. The numerator
    // moves with the output and the denominator does not, and the default
    // capture is a property of the INPUT line, so the two do not track and
    // widening the output raster eats the zoom travel.
    const uint16_t Raster = 1916;
    const uint16_t DefaultCapture = 890;  // ActiveImage::defaultWidth on this bench

    uint16_t floor = AxisHorizontal.minimumCapture(Raster);

    CHECK(DefaultCapture > floor);

    SUBCASE("and the stop is where the picture stops growing with the crop") {
        // Above it, cropping magnifies and the picture stays full size; below
        // it the scale is pinned and every further unit of crop is a unit of
        // picture lost.
        const float atStop = AxisHorizontal.fitToRaster(floor, Raster).produced();
        const float past =
            AxisHorizontal.fitToRaster((uint16_t)(floor - 20), Raster).produced();
        CHECK(past < atStop - 10.0f);
    }
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
    CHECK(solved.display().start() - solved.display().stop()
          <= (int32_t)fit.produced() + 1);
    CHECK(solved.display().stop() > 100);
    CHECK((int32_t)raster - solved.display().start() > 100);
}

// Measured on the bench at both ends. The NEAR end crept onto the picture's own
// corner at every clock the engine can select shows nothing; the FAR end,
// recovered as `far - produced` across six magnifications from 1.14 to 2.05,
// lands on the modelled write origin to 0.35 px. Nothing needs hiding at either.
// docs/investigations/display-window-opens-early.md
// VDS_DIS_?B_ST is where blanking STARTS, so an aperture that closes after the
// write has finished exposes memory nothing wrote, which the playback stage
// fetches as scratch. Both ends of the write are fractional -- it runs from
// VDS_?B_SP + originOffset() for produced() -- so the far edge has to be the
// floor of that sum rather than a separately rounded corner plus a floored
// length, which can land a whole unit past it.
TEST_CASE("blanking starts no later than the write ends")
{
    // The bench 1080p vertical: 582 captured lines at VDS_VSCALE 533 in a
    // 1125-line raster. The write ends 1120.88 lines in, and a window closing
    // at 1121 leaves the last line of the aperture unwritten.
    const uint16_t Raster = 1125, Capture = 582;
    const Scale scale(533);
    const AxisSolution solved = AxisVertical.solve(Capture, scale, Raster);

    const float writeEnds = (float)solved.memory().stop()
                          + AxisVertical.originOffset(scale.magnification())
                          + solved.produced();
    CHECK((float)solved.display().start() <= writeEnds);
}

TEST_CASE("the horizontal memory window is an odd number of units wide")
{
    // An EVEN VDS_HB_ST - VDS_HB_SP shears the picture and an odd one is clean,
    // measured 38 of 38 calling the mark before it was taken. The width is not a
    // register -- VDS_HB_ST is floor(VDS_HB_SP + originOffset + produced), so
    // VDS_HB_SP cancels and the parity belongs to the produced width.
    // docs/known-issues.md
    const uint16_t Raster = 1919;

    SUBCASE("at the bench framing that shears") {
        // RiscPC 320x256@50 at VDS_HSCALE 496: the solve wants 1822, and 1822
        // is the state photographed sheared.
        CHECK(AxisHorizontal.solve(831, Scale(496), Raster).memory().width() % 2 == 1);
    }

    SUBCASE("across the zoom range, where the parity otherwise alternates") {
        for (uint16_t scale = 280; scale <= 1020; ++scale) {
            const AxisSolution solved = AxisHorizontal.solve(829, Scale(scale), Raster);
            if (!solved.usable())
                continue;
            REQUIRE(solved.memory().width() % 2 == 1);
        }
    }

    SUBCASE("the bias costs no picture, because the reach already reserves one") {
        // The window closes one capture unit short of the write end, so a bias
        // that steps the far edge FORWARD still lands inside written memory --
        // and forward is what keeps the picture filling the screen. Back left a
        // black column at the right on every mode whose width came out even.
        for (uint16_t scale = 280; scale <= 1020; ++scale) {
            const AxisSolution solved = AxisHorizontal.solve(829, Scale(scale), Raster);
            if (!solved.usable())
                continue;
            float reach = floorf((float)solved.memory().stop()
                                 + AxisHorizontal.originOffset(Scale(scale).magnification())
                                 + solved.produced() - Scale(scale).magnification());
            const float bound = (float)AxisHorizontal.farBound(Raster, 0);
            if (reach > bound)
                reach = bound;
            REQUIRE((float)solved.memory().start() >= reach);
        }
    }

    SUBCASE("and the aperture still closes on the last column the capture filled") {
        // The memory window may carry the odd unit; the APERTURE may not. One
        // pixel past the interpolator's reach shows as a column of junk down the
        // right-hand edge, which is what the forward bias costs if both far
        // edges move together. Blanking it loses nothing: that column was never
        // captured.
        for (uint16_t scale = 280; scale <= 1020; ++scale) {
            const AxisSolution solved = AxisHorizontal.solve(829, Scale(scale), Raster);
            if (!solved.usable())
                continue;
            const float reach = floorf((float)solved.memory().stop()
                                       + AxisHorizontal.originOffset(Scale(scale).magnification())
                                       + solved.produced() - Scale(scale).magnification());
            REQUIRE((float)solved.display().start() <= reach);
        }
    }

    SUBCASE("the window never opens past where the write ends") {
        // Biasing the width must give a unit back, never take one: memory past
        // the picture is memory the playback stage still walks.
        const Scale scale(496);
        const AxisSolution solved = AxisHorizontal.solve(831, scale, Raster);
        const float writeEnds = (float)solved.memory().stop()
                              + AxisHorizontal.originOffset(scale.magnification())
                              + solved.produced();
        CHECK((float)solved.memory().start() <= writeEnds);
    }
}

TEST_CASE("the display window is the picture, at both ends")
{
    const uint16_t Raster = 1916, Capture = 973;
    const Scale scale(557);
    const AxisSolution solved = AxisHorizontal.solve(Capture, scale, Raster);
    const PictureOrigin placed = AxisHorizontal.placePicture(
        scale.produced(Capture), Raster, scale.magnification());

    CHECK(solved.display().stop() > placed.corner());
    CHECK((float)solved.display().stop()
          <= (float)placed.corner() + scale.magnification() + 1.0f);

    const int32_t picture = placed.corner() + (int32_t)solved.produced();
    CHECK(solved.display().start() <= picture);
    CHECK((float)(picture - solved.display().start())
          <= scale.magnification() + 1.0f);
}

// The capture stop is what the pan walks toward the end of the line, and past
// VideoSourceLine::lastCapture() the input formatter is writing blanking rather than
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
                                s.display().stop(), s.memory().stop(), s.memory().start(), s.display().stop(),
                                s.display().start());
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


// The scaler interpolates between two capture units, so an output unit landing
// at source position s reads units floor(s) and floor(s) + 1. The last unit the
// capture wrote is capture - 1, so an aperture whose final unit reaches past it
// shows memory nothing wrote -- which the playback stage fetches as whatever the
// previous mode left in it.
//
// Measured at 320x256@50 on vga, capture 582 at VDS_VSCALE 552 in a 1124-line
// raster: the last line of the picture is a static line of stale memory, it
// clears when the capture takes one more line, and it grows to a forty-line
// band that does NOT flash while the source's border does when the capture
// takes forty fewer.
static float lastCaptureUnitRead(const Axis &axis, Scale scale,
                                 const AxisSolution &solved)
{
    const float lastUnit = (float)solved.display().start() - 1.0f;
    const float pos = (lastUnit - (float)solved.memory().stop()
                       - axis.originOffset(scale.magnification()))
                    * (float)scale.reg() / (float)Scale::Unity;
    return floorf(pos) + 1.0f;
}

TEST_CASE("the aperture's last unit is interpolated from captured memory")
{
    SUBCASE("vertically, at the bench 320x256@50 framing") {
        const uint16_t Raster = 1124, Capture = 582;
        const Scale scale(552);
        const AxisSolution solved = AxisVertical.solve(Capture, scale, Raster);
        CHECK(lastCaptureUnitRead(AxisVertical, scale, solved)
              <= (float)Capture - 1.0f);
    }

    SUBCASE("horizontally, at the bench 320x256@50 framing") {
        const uint16_t Raster = 1919, Capture = 998;
        const Scale scale(568);
        const AxisSolution solved = AxisHorizontal.solve(Capture, scale, Raster);
        CHECK(lastCaptureUnitRead(AxisHorizontal, scale, solved)
              <= (float)Capture - 1.0f);
    }

    SUBCASE("across the zoom range on both axes") {
        for (uint16_t reg = Scale::Min; reg <= Scale::Max; ++reg) {
            const Scale scale(reg);

            const AxisSolution v = AxisVertical.solve(582, scale, 1124);
            if (v.usable())
                REQUIRE(lastCaptureUnitRead(AxisVertical, scale, v) <= 581.0f);

            const AxisSolution h = AxisHorizontal.solve(998, scale, 1919);
            if (h.usable())
                REQUIRE(lastCaptureUnitRead(AxisHorizontal, scale, h) <= 997.0f);
        }
    }
}

// The write origin marks where content first APPEARS, which is the first unit
// the capture partly filled -- `docs/investigations/moving-write-origin.md`
// found it by creeping until the picture started. The first unit fully written
// is one capture unit later, so an aperture opening on the origin shows a unit
// carrying whatever the previous mode left in that memory.
//
// Crept on the bench at three magnifications, `VDS_DIS_HB_SP` raised one unit
// at a time until the line down the left edge cleared:
//
//   magnification 1.13, write origin 140.32 -> first clean corner 142
//   magnification 1.17, write origin 140.19 -> first clean corner 142
//   magnification 2.17, write origin 140.12 -> first clean corner 143
//
// The third is what makes it a capture unit rather than an output pixel: a
// fixed one-pixel margin predicts 142 there.
static float firstCaptureUnitRead(const Axis &axis, Scale scale,
                                  const AxisSolution &solved)
{
    return ((float)solved.display().stop() - (float)solved.memory().stop()
            - axis.originOffset(scale.magnification()))
         * (float)scale.reg() / (float)Scale::Unity;
}

// The inset lands exactly on one capture unit where the window stop is on its
// floor, and originOffset() is computed in float, so the equality case loses a
// ulp. A thousandth of a unit is not a framing defect.
static const float UnitSlack = 1e-3f;

TEST_CASE("the aperture's first unit is interpolated from captured memory")
{
    SUBCASE("horizontally, at the three crept magnifications") {
        const AxisSolution a = AxisHorizontal.solve(1232, Scale(904), 1600);
        CHECK(firstCaptureUnitRead(AxisHorizontal, Scale(904), a)
              >= 1.0f - UnitSlack);

        const AxisSolution b = AxisHorizontal.solve(1195, Scale(877), 1600);
        CHECK(firstCaptureUnitRead(AxisHorizontal, Scale(877), b)
              >= 1.0f - UnitSlack);

        const AxisSolution c = AxisHorizontal.solve(917, Scale(473), 1600);
        CHECK(firstCaptureUnitRead(AxisHorizontal, Scale(473), c)
              >= 1.0f - UnitSlack);
    }

    SUBCASE("across the zoom range on both axes") {
        for (uint16_t reg = Scale::Min; reg <= Scale::Max; ++reg) {
            const Scale scale(reg);

            const AxisSolution v = AxisVertical.solve(582, scale, 1124);
            if (v.usable())
                REQUIRE(firstCaptureUnitRead(AxisVertical, scale, v)
                        >= 1.0f - UnitSlack);

            const AxisSolution h = AxisHorizontal.solve(998, scale, 1919);
            if (h.usable())
                REQUIRE(firstCaptureUnitRead(AxisHorizontal, scale, h)
                        >= 1.0f - UnitSlack);
        }
    }
}
