// Host-compiled unit tests for Tv5725::OutputWindow -- `make -C test output-window`.
// Both axes solved from the capture and the raster alone -- nothing is read
// back off the chip. docs/firmware-geometry-engine.md.
//
// Every case goes in through the constructor and asserts on the four registers
// each axis comes out with, because those registers are the behaviour.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// windows this class derives, for inspection by hand.

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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMapping.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputWindow.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

using namespace Tv5725;

// The raster a case cares about. OutputMode::solve() fills the rest of an
// OutputTiming; nothing OutputWindow reads is outside these six.
static Tv5725::OutputTiming rasterOf(uint16_t linePx, uint16_t frameLines,
                                     uint16_t activeStopH = 0, uint16_t activeStopV = 0,
                                     uint16_t activeStartH = 0, uint16_t activeStartV = 0)
{
    Tv5725::OutputTiming raster;
    raster.horizontalTotal = linePx;
    raster.verticalTotal = frameLines;
    raster.activeStop = activeStopH;
    raster.activeLinesStop = activeStopV;
    raster.activeStart = activeStartH;
    raster.activeLinesStart = activeStartV;
    return raster;
}

// --- the bench readings, as this suite's own oracle ---------------------------

// Where the scaler starts writing, measured at the near edge across the
// magnification range, and the lowest VDS_?B_SP that does not corrupt the
// picture. docs/scaler-geometry-model.md.
//
// Stated here rather than asked of the class: the cases below predict the
// registers from these numbers and compare, so a constant that moves in the
// firmware disagrees with the reading it came from. Asking the class would make
// every such case compare the model with itself.
struct WriteStart { float constant, perMagnification; uint16_t floor; };
static const WriteStart BenchHorizontal = {55.0f, 25.0f, 8};
static const WriteStart BenchVertical   = {0.2f, 0.8f, 0};

static const WriteStart &bench(const Axis &axis)
{
    return axis.vertical() ? BenchVertical : BenchHorizontal;
}

static float writeOrigin(const Axis &axis, float magnification)
{
    return bench(axis).constant + bench(axis).perMagnification * magnification;
}

// One past the last pixel the picture may occupy: the raster total less the
// minimum front porch, or the mode's own stop where it states one.
static uint16_t farBound(uint16_t rasterTotal, uint16_t activeStop)
{
    const uint16_t edge = rasterTotal > 2 ? (uint16_t)(rasterTotal - 2) : 0;
    return activeStop > 0 && activeStop < edge ? activeStop : edge;
}

// The room the raster offers a picture: the far bound, less whichever of the
// write floor and the mode's own back porch holds the picture off further. The
// far end owes nothing -- charging it the same reserve leaves a black bar down
// the right that no zoom closes.
static float room(const Axis &axis, uint16_t rasterTotal,
                  uint16_t activeStart = 0, uint16_t activeStop = 0)
{
    const float floor = (float)bench(axis).floor + bench(axis).constant;
    return (float)farBound(rasterTotal, activeStop)
         - ((float)activeStart > floor ? (float)activeStart : floor);
}

static const OutputMapping &on(const OutputWindow &solved, const Axis &axis)
{
    return axis.vertical() ? solved.vertical() : solved.horizontal();
}

// Where the picture's first pixel lands. The memory window opens where the
// write does and the content appears an origin later, so the corner is not a
// register -- it is read back out of the pair that are.
static float cornerOf(const OutputMapping &solved, const Axis &axis)
{
    return (float)solved.memory().stop()
         + writeOrigin(axis, solved.scale().magnification());
}

// Where the write ends: the corner plus the whole picture.
static float writeEndOf(const OutputMapping &solved, const Axis &axis)
{
    return cornerOf(solved, axis) + solved.produced();
}

// --- everything from the capture and the raster alone -------------------------

TEST_CASE("nothing is inherited from the registers")
{
    // The bench state: 798 IF units captured on a 1126-unit line, 513 units
    // of a 312-line frame, onto a 1445 x 1126 output raster.
    OutputWindow s(798, 513, rasterOf(1445, 1126));

    SUBCASE("both scales are computed, not read") {
        CHECK(((s.horizontal().scale() >= Scale::Min) && (s.horizontal().scale() <= Scale::Max)));
        CHECK(((s.vertical().scale() >= Scale::Min) && (s.vertical().scale() <= Scale::Max)));
    }

    SUBCASE("both memory windows clear their floor") {
        CHECK(s.horizontal().memory().stop() >= BenchHorizontal.floor);
        CHECK(s.vertical().memory().stop() >= BenchVertical.floor);
    }

    SUBCASE("neither window reaches the value that wraps") {
        CHECK(((s.horizontal().memory().start() < 1444) && (s.horizontal().display().start() < 1444)));
        CHECK(((s.vertical().memory().start() < 1125) && (s.vertical().display().start() < 1125)));
    }

    SUBCASE("the vertical picture is not doubled") {
        // ~2200 would mean the capture had been doubled on the way through.
        CHECK(((s.vertical().produced() > 900) && (s.vertical().produced() < 1130)));
    }

    SUBCASE("the same capture always gives the same answer") {
        OutputWindow again = OutputWindow(798, 513, rasterOf(1445, 1126));
        CHECK(((again.horizontal().scale() == s.horizontal().scale())
               && (again.vertical().scale() == s.vertical().scale())));
        CHECK(again.horizontal().memory().stop() == s.horizontal().memory().stop());
        CHECK(again.vertical().display().start() == s.vertical().display().start());
    }

    SUBCASE("a capture that reads zero yields no picture rather than a wrong one") {
        // Unusable, not merely empty: VideoPath::solveWindows() refuses the
        // whole solve on this, and a picture of no size that reads usable is
        // written to the chip as a raster of zeroes.
        OutputWindow dropped = OutputWindow(0, 0, rasterOf(1445, 1126));
        CHECK(dropped.horizontal().produced() == 0.0f);
        CHECK(dropped.vertical().produced() == 0.0f);
        CHECK_FALSE(dropped.horizontal().usable());
        CHECK_FALSE(dropped.vertical().usable());
        CHECK_FALSE(dropped.usable());
    }
}

TEST_CASE("the solution carries the front porch to both axes")
{
    const uint16_t Raster = 1916, Frame = 1126;
    const uint16_t StopH = 1852, StopV = 1121;

    OutputWindow solved(1008, 532, rasterOf(Raster, Frame, StopH, StopV));
    CHECK(solved.horizontal().display().start() <= (int32_t)StopH);
    CHECK(solved.vertical().display().start() <= (int32_t)StopV);

    SUBCASE("and without one the raster edge still bounds it") {
        // Compared against the solution that HAS a porch rather than against
        // the porch itself: the window gives back Axis::margin at the far edge,
        // so it sits inside either bound and the porch is the tighter one.
        OutputWindow plain(1008, 532, rasterOf(Raster, Frame));
        CHECK(plain.horizontal().display().start()
              > solved.horizontal().display().start());
        CHECK(plain.horizontal().display().start() < (int32_t)Raster);
    }
}

TEST_CASE("the horizontal window goes where the geometry puts it")
{
    // Asserting an absence. MemoryWindow::fetchFor sizes PB_FETCH_NUM from the capture
    // width, which makes the beat independent of HSCALE, so there is no tearing
    // band left for the window to dodge and no table to consult.
    for (uint16_t capture = 400; capture <= 1009; capture += 3) {
        OutputWindow solved(capture, 512, rasterOf(1445, 1126));
        REQUIRE(solved.usable());
        // The far edges part by the parity unit and no more, and the MEMORY one
        // is the wider: the fetch covers every column the aperture shows.
        const int32_t spare = solved.horizontal().memory().start()
                            - solved.horizontal().display().start();
        CHECK(spare >= 0);
        CHECK(spare <= 1);
    }
}

TEST_CASE("the scale moves with the zoom rather than jumping")
{
    // The scale must never fall back to a different HSCALE -- it has to move
    // fluidly with the zoom pad rather than jump. A shrink search moves it by up
    // to 56 counts and steps backwards 71 times across this range while the pad
    // goes one way.
    uint16_t previous = 0;
    for (uint16_t capture = 400; capture <= 1009; ++capture) {
        OutputWindow solved(capture, 512, rasterOf(1445, 1126));
        REQUIRE(solved.usable());
        CHECK(solved.horizontal().scale().reg() >= previous);
        previous = solved.horizontal().scale().reg();
    }
}

TEST_CASE("both axes allocate only the memory the picture occupies")
{
    // A property of the memory, not of one axis, so the solve applies it and
    // both axes get it. The artefact was seen horizontally, but a rule holding
    // on one axis only would be a special case nobody measured.
    //
    // Horizontally the far edges may part by the parity unit; vertically there
    // is no bias, so they still meet.
    OutputWindow solved(749, 512, rasterOf(1445, 1126));
    const int32_t spare = solved.horizontal().memory().start()
                        - solved.horizontal().display().start();
    CHECK(spare >= 0);
    CHECK(spare <= 1);
    CHECK(solved.vertical().memory().start() == solved.vertical().display().start());

    SUBCASE("and neither reaches the value that wraps") {
        // VDS_VB_ST at VDS_VSYNC_RST rolls the frame; VDS_HB_ST at
        // VDS_HSYNC_RST wraps.
        CHECK(solved.horizontal().memory().start() <= 1445 - 2);
        CHECK(solved.vertical().memory().start() <= 1126 - 2);
    }
}

// --- where the scaler starts writing -----------------------------------------

// The gap between the memory window opening and the aperture opening IS the
// write origin, so the two registers together measure it. Horizontally the
// aperture opens a further capture unit in, on the first unit fully written.
TEST_CASE("the write start is not a constant")
{
    SUBCASE("the horizontal write start matches every reading") {
        for (uint16_t capture = 500; capture <= 1400; capture += 7) {
            const OutputWindow solved(capture, 512, rasterOf(1916, 1126));
            const OutputMapping &h = solved.horizontal();
            if (!h.usable() || h.memory().stop() != BenchHorizontal.floor)
                continue;
            const float m = h.scale().magnification();
            const float gap = (float)h.display().stop() - (float)h.memory().stop() - m;
            REQUIRE(gap >= writeOrigin(AxisHorizontal, m));
            REQUIRE(gap < writeOrigin(AxisHorizontal, m) + 1.0f);
        }
    }

    SUBCASE("the vertical write start matches every reading") {
        // Nearly flat: a line buffer, with no interpolator to feed. Which is
        // why it needs a whole sweep -- 0.8 per magnification against 0.9 is a
        // tenth of a line, and only some of the magnifications round across it.
        for (uint16_t capture = 200; capture <= 1100; capture += 3) {
            const OutputWindow solved(800, capture, rasterOf(1916, 1126));
            const OutputMapping &v = solved.vertical();
            if (!v.usable() || v.memory().stop() != BenchVertical.floor)
                continue;
            const float m = v.scale().magnification();
            REQUIRE((float)v.display().stop()
                    == doctest::Approx(lrintf(writeOrigin(AxisVertical, m))));
        }
    }
}

// Produced width, floored, expressed against where the scaler actually starts
// writing rather than against a fixed corner. docs/scaler-geometry-model.md
//
// These check the MODEL above against the readings it was fitted to. The cases
// either side hold the class to that model, so the two together are the chain
// from a bench measurement to a register.
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
    double writeStart = winSp + writeOrigin(axis, (float)m);
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
    // A capture too small to fill the raster: the scale pins at its floor and
    // the picture that is left has to be placed rather than stretched.
    const uint16_t Raster = 1445;
    const OutputWindow solved(200, 512, rasterOf(Raster, 1126));
    const OutputMapping &h = solved.horizontal();
    REQUIRE(h.scale().reg() == Scale::Min);
    REQUIRE(h.produced() < room(AxisHorizontal, Raster));

    SUBCASE("the picture is centred on the raster, not pinned to a panel edge") {
        // PANEL_VISIBLE_LEFT is 90 on the bench TV.
        CHECK_NEAR(cornerOf(h, AxisHorizontal), (Raster - h.produced()) / 2.0f, 1.0);
        CHECK_NEAR(Raster - writeEndOf(h, AxisHorizontal),
                   cornerOf(h, AxisHorizontal), 1.0);
    }

    SUBCASE("a picture too wide to centre is pinned as far over as it goes") {
        // A capture that fills the raster cannot be centred: the near edge is
        // already as far over as the write floor allows.
        const OutputWindow wide(1009, 512, rasterOf(Raster, 1126));
        CHECK(wide.horizontal().memory().stop() == BenchHorizontal.floor);
    }

    SUBCASE("the memory window is never placed where the picture corrupts") {
        // Below 8 the display corrupts.
        for (uint16_t capture = 60; capture <= 1400; capture += 13) {
            const OutputWindow any(capture, 512, rasterOf(Raster, 1126));
            REQUIRE(any.horizontal().memory().stop() >= BenchHorizontal.floor);
        }
    }
}

// --- the picture is computed, never inherited ---------------------------------

TEST_CASE("the picture is made as big as the raster allows")
{
    const uint16_t Raster = 1445;
    const OutputWindow solved(798, 513, rasterOf(Raster, 1126));
    const OutputMapping &h = solved.horizontal();

    SUBCASE("the picture is made as big as the raster allows") {
        // Not "as big as the room": the write offset costs perMagnification x
        // magnification. The observable is that nothing more could be claimed --
        // the memory window lands hard against its floor.
        CHECK(((h.scale().reg() >= Scale::Min) && (h.scale().reg() <= Scale::Max)));
        CHECK(h.memory().stop() >= BenchHorizontal.floor);
        CHECK(h.memory().stop() <= BenchHorizontal.floor + 4);
    }

    SUBCASE("the picture gives up exactly the write offset and nothing more") {
        // Once, not twice: the offset is paid before the first write and there
        // is nothing after the last one.
        CHECK_NEAR(h.produced(),
                   room(AxisHorizontal, Raster)
                       - BenchHorizontal.perMagnification * h.scale().magnification(),
                   2.0);
    }

    SUBCASE("a smaller capture still fills the raster") {
        // 800, not 400. Below the narrowest capture the magnification runs out
        // and the picture CANNOT fill the raster, so a capture under it is
        // testing the ceiling rather than the fit.
        const OutputWindow small(800, 513, rasterOf(Raster, 1126));
        CHECK(small.horizontal().memory().stop() <= BenchHorizontal.floor + 4);
    }

    SUBCASE("the scale register bounds how big the picture can get") {
        // A capture too small to fill the raster is bounded by the axis's
        // scale floor. That is a limit, not a failure.
        const OutputWindow tiny(60, 513, rasterOf(Raster, 1126));
        CHECK(tiny.horizontal().scale().reg() == Scale::Min);
        CHECK(tiny.horizontal().produced() < room(AxisHorizontal, Raster));
    }

    SUBCASE("the placed picture always clears the write floor") {
        // The scale is given a step back rather than allowed to overflow:
        // rounding it down makes the picture a shade larger than solved for,
        // which can push the near edge below VDS_?B_SP's floor.
        for (uint16_t capture = 100; capture <= 1200; capture += 50) {
            const OutputWindow f(capture, 513, rasterOf(Raster, 1126));
            REQUIRE(f.horizontal().memory().stop() >= BenchHorizontal.floor);
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
    const OutputWindow solved(1008, 512, rasterOf(2300, 1126));
    const OutputMapping &h = solved.horizontal();

    // One capture unit past the modelled corner: the origin marks where content
    // first appears, and that unit is only partly written.
    CHECK((float)h.display().stop() > cornerOf(h, AxisHorizontal));
    CHECK((float)h.display().stop()
          <= cornerOf(h, AxisHorizontal) + h.scale().magnification() + 1.0f);

    SUBCASE("the memory window still opens where the write does") {
        // It is the DISPLAY that must not show the gap. Opening the memory
        // window late moves the picture instead, which widens the band.
        CHECK(h.memory().stop() == BenchHorizontal.floor);
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
    const Tv5725::OutputTiming raster = rasterOf(1445, Raster, 0, ActiveStop);
    const float allowed = room(AxisVertical, Raster, 0, ActiveStop);

    const uint16_t most = OutputWindow::widestCapture(AxisVertical, raster);
    CHECK(OutputWindow(400, most, raster).vertical().produced() <= allowed);

    SUBCASE("and the capture it bounds does not fit") {
        // 622 half-lines: what the line doubler offers on a 311-line source,
        // and what a framing free to take the whole capturable region asks for.
        CHECK(OutputWindow(400, 622, raster).vertical().produced() > allowed);
    }

    SUBCASE("a raster with room for everything bounds nothing the input can hold") {
        // 1080p against the same source: the doubled frame fits with 1.8x to
        // spare, so the ceiling is above anything the input line can offer.
        CHECK(OutputWindow::widestCapture(AxisVertical, rasterOf(1445, 1126, 0, 1117))
              > 622);
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
            const OutputWindow solved(capture, capture, rasterOf(1445, 1126));
            REQUIRE(solved.horizontal().scale() >= Scale::Min);
            REQUIRE(solved.vertical().scale() >= Scale::Min);
        }
    }

    SUBCASE("and the zoom stops there rather than shrinking the picture") {
        // VideoPath::zoom() stops the capture where the magnification runs out,
        // so the picture stays full size and the control simply stops.
        CHECK(OutputWindow::narrowestCapture(AxisHorizontal, rasterOf(1445, 1126)) == 436);
        CHECK(OutputWindow::narrowestCapture(AxisVertical, rasterOf(1445, 1126)) == 375);
    }
}

// --- one axis's four output registers -----------------------------------------

TEST_CASE("the solver places every output register")
{
    // Bench reference: a capture of 851 on a 1445 px line fits to HSCALE 650
    // and produces 1340.65 px, so centred puts the corner at 52. It cannot go
    // there -- at x1.575 the write start is 94.4 px after VDS_HB_SP, needing
    // the register below its floor of 8 -- so the picture is pushed right.
    const uint16_t Raster = 1445;
    const OutputWindow solved(851, 513, rasterOf(Raster, 1126));
    const OutputMapping &h = solved.horizontal();
    REQUIRE(h.scale().reg() == 650);

    SUBCASE("the solver centres the picture as far as the hardware allows") {
        // The MEMORY window opens where the write does; the display window
        // opens one capture unit later, on the first unit fully written.
        CHECK(h.memory().stop() == 8);
        CHECK(h.display().stop() == 104);
    }

    SUBCASE("the memory window is exactly the display window") {
        // Allocate only what is displayed. Taking the whole raster is not free:
        // memory past the picture is memory the playback stage still walks, and
        // on the bench it showed as artefacts down the LEFT edge. EQUAL to the
        // display window, not merely under the last usable value.
        CHECK(h.memory().start() == h.display().start());
        CHECK(h.memory().start() <= 1443);
    }

    SUBCASE("the display window hugs the picture") {
        // A window sized for a different picture blanks where tearing shows,
        // so a headroom measurement taken through one is worthless. It gives
        // back a margin at each end -- the write origin's at the near one, and
        // at the far one the capture unit the scaler interpolates past the last
        // one written, plus the unit flooring costs.
        const float picture = writeEndOf(h, AxisHorizontal);
        CHECK((float)h.display().start() <= picture);
        CHECK(picture - (float)h.display().start()
              <= h.scale().magnification() + 1.0f);
    }

    SUBCASE("the solver corrects the thirteen pixel offset seen on the bench") {
        // VDS_DIS_HB_SP 129 against an origin of 116.
        CHECK(h.display().stop() != 129);
    }

    SUBCASE("no returned register reaches the value that wraps") {
        // Tested at the boundary itself: off-by-one is the risk.
        const OutputWindow edge(1009, 1124, rasterOf(1445, 1126));
        CHECK(edge.horizontal().memory().start() < 1444);
        CHECK(edge.horizontal().display().start() < 1444);
        CHECK(edge.vertical().memory().start() < 1125);
        CHECK(edge.vertical().display().start() < 1125);
    }

    SUBCASE("the display window never runs past the last written pixel") {
        // VDS_DIS_?B_ST is where blanking STARTS, so it may equal origin +
        // produced but never exceed it. Rounding up shows scratch.
        const OutputMapping &v = solved.vertical();
        CHECK((float)v.display().start() <= writeEndOf(v, AxisVertical));
    }

    SUBCASE("a vertical solve does not double the capture it is given") {
        // Doubling it is the likeliest bug here: 723 units at VSCALE 660 is
        // 1121.7 output lines, not 2243.
        const OutputWindow tall(800, 723, rasterOf(1445, 1125));
        REQUIRE(tall.vertical().scale().reg() == 660);
        CHECK(((tall.vertical().produced() > 1121) && (tall.vertical().produced() < 1123)));
        CHECK(tall.vertical().memory().start() < 1125);
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
    const OutputWindow behind(1008, 512, rasterOf(Raster, 1126, 0, 0, 140, 0));
    const OutputWindow whole(1008, 512, rasterOf(Raster, 1126));

    CHECK_NEAR(cornerOf(behind.horizontal(), AxisHorizontal), 140.0f, 1.0);
    CHECK(behind.horizontal().produced() < whole.horizontal().produced());
    CHECK_NEAR(behind.horizontal().produced(),
               room(AxisHorizontal, Raster, 140), 2.0);

    SUBCASE("an active start below the write floor changes nothing") {
        // The write floor is physical and a back porch cannot argue with it, so
        // the room is bounded by whichever is LARGER.
        const OutputWindow under(1008, 512, rasterOf(Raster, 1126, 0, 0, 40, 0));
        CHECK(under.horizontal().memory().stop() == whole.horizontal().memory().stop());
        CHECK(under.horizontal().memory().start() == whole.horizontal().memory().start());
        CHECK(under.horizontal().scale() == whole.horizontal().scale());
    }
}

TEST_CASE("only the near end pays the write floor")
{
    const uint16_t Raster = 1901;   // the bench raster at 108 MHz, 1125 lines
    const uint16_t Capture = 1055;
    const float FarEdge = Raster - 2;
    const OutputWindow solved(Capture, 512, rasterOf(Raster, 1125));
    const OutputMapping &h = solved.horizontal();

    SUBCASE("the room gives up the floor once, not twice") {
        CHECK_NEAR(room(AxisHorizontal, Raster),
                   FarEdge - (BenchHorizontal.floor + BenchHorizontal.constant), 0.01);
    }

    SUBCASE("the picture reaches the end of the line") {
        const float end = writeEndOf(h, AxisHorizontal);
        CHECK(end <= FarEdge);
        CHECK(end > FarEdge - 8);
    }

    SUBCASE("the near edge still sits on the write floor") {
        // The left edge is already as far over as physics allows, and must not
        // move: below the floor the picture corrupts.
        CHECK(h.memory().stop() >= BenchHorizontal.floor);
        CHECK(h.memory().stop() <= BenchHorizontal.floor + 2);
    }

    SUBCASE("a capture too small to fill the raster is still bounded") {
        const OutputWindow tiny(60, 512, rasterOf(Raster, 1125));
        CHECK(tiny.horizontal().scale().reg() == Scale::Min);
        CHECK(tiny.horizontal().produced() < room(AxisHorizontal, Raster));
    }
}

TEST_CASE("the picture stops at the front porch, not at the raster edge")
{
    // The raster's edge is the wrong far bound: a display window taken up to
    // VDS_HSYNC_RST leaves too little front porch and the colours come out
    // wrong. Measured on the bench, RiscPC 320x256@50 into a 1916 px raster,
    // the window is good at 1900 and bad at 1910 -- a floor of about 16 px.
    const uint16_t Raster = 1916;
    const uint16_t ActiveStop = 1852;   // 1916 less a 64 px front porch
    const uint16_t Capture = 1008;
    const OutputWindow solved(Capture, 512, rasterOf(Raster, 1126, ActiveStop, 0));

    SUBCASE("the room gives up the front porch as well as the write floor") {
        CHECK_NEAR(room(AxisHorizontal, Raster, 0, ActiveStop),
                   ActiveStop - (BenchHorizontal.floor + BenchHorizontal.constant), 0.01);
    }

    // On the display window rather than on where the write ends: the write is
    // fractional and the window closes on its floor, so a picture reaching the
    // porch to within a pixel is emitted blank from the porch.
    SUBCASE("and the picture fills the line up to the front porch, not past it") {
        CHECK(solved.horizontal().display().start() <= (int32_t)ActiveStop);
        CHECK(solved.horizontal().display().start() > (int32_t)ActiveStop - 8);
    }

    SUBCASE("an activeStop of 0 keeps the raster edge, so nothing else moves") {
        CHECK_NEAR(room(AxisHorizontal, Raster, 0, 0),
                   room(AxisHorizontal, Raster), 0.01);
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
    const OutputWindow solved(890, 480, rasterOf(1916, Raster, 1852, ActiveStop,
                                                 0, ActiveStart));
    REQUIRE(solved.vertical().scale().reg() == 455);
    CHECK(solved.vertical().produced() > (float)(ActiveStop - ActiveStart) - 1.0f);

    SUBCASE("and the display window still closes by the front porch") {
        CHECK(solved.vertical().display().start() <= (int32_t)ActiveStop);
    }
}

TEST_CASE("the picture starts no earlier than the back porch")
{
    const uint16_t Raster = 1918, ActiveStart = 140;

    SUBCASE("a picture with room to spare is centred, clear of the porch") {
        // The scale pins at its floor here, so the picture is smaller than the
        // room and has somewhere to be centred.
        const OutputWindow solved(400, 512, rasterOf(Raster, 1126, 0, 0, ActiveStart, 0));
        const OutputMapping &h = solved.horizontal();
        REQUIRE(h.scale().reg() == Scale::Min);
        CHECK(cornerOf(h, AxisHorizontal) >= (float)ActiveStart);

        // Symmetrically, so what is reserved near is reserved far.
        CHECK_NEAR(Raster - writeEndOf(h, AxisHorizontal),
                   cornerOf(h, AxisHorizontal), 1.5);
    }

    SUBCASE("and one too big to centre starts AT the back porch") {
        // Overscan off the far end rather than begin inside the blanking.
        const OutputWindow solved(1008, 512, rasterOf(Raster, 1126, 0, 0, ActiveStart, 0));
        const OutputMapping &h = solved.horizontal();
        CHECK_NEAR(cornerOf(h, AxisHorizontal), (float)ActiveStart, 1.0);
        CHECK(h.memory().stop() >= (int32_t)BenchHorizontal.floor);
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
    const uint16_t DefaultCapture = 890;  // CaptureWindow::defaultWidth on this bench
    const uint16_t floor = OutputWindow::narrowestCapture(AxisHorizontal,
                                                          rasterOf(Raster, 1126));

    CHECK(DefaultCapture > floor);

    SUBCASE("and the stop is where the picture stops growing with the crop") {
        // Above it, cropping magnifies and the picture stays full size; below
        // it the scale is pinned and every further unit of crop is a unit of
        // picture lost.
        const float atStop = OutputWindow(floor, 512, rasterOf(Raster, 1126))
                                 .horizontal().produced();
        const float past = OutputWindow((uint16_t)(floor - 20), 512,
                                        rasterOf(Raster, 1126)).horizontal().produced();
        CHECK(past < atStop - 10.0f);
    }
}

TEST_CASE("a picture too small for the raster is blanked, not left open")
{
    // Below the narrowest capture the magnification runs out and `produced`
    // falls short of the raster. The room left over is not empty: playback
    // keeps fetching past the end of the written data, so an open window there
    // shows stale buffer. The display window has to stop where the picture does.
    const uint16_t Raster = 1445;
    const OutputWindow solved(200, 512, rasterOf(Raster, 1126));
    const OutputMapping &h = solved.horizontal();
    REQUIRE(h.produced() < (float)Raster);

    // the window IS the picture, and the room left over is black at both ends
    CHECK(h.display().start() - h.display().stop() <= (int32_t)h.produced() + 1);
    CHECK(h.display().stop() > 100);
    CHECK((int32_t)Raster - h.display().start() > 100);
}

// --- where the aperture closes ------------------------------------------------

// Measured on the bench at both ends. The NEAR end crept onto the picture's own
// corner at every clock the engine can select shows nothing; the FAR end,
// recovered as `far - produced` across six magnifications from 1.14 to 2.05,
// lands on the modelled write origin to 0.35 px. Nothing needs hiding at either.
// docs/investigations/display-window-opens-early.md
//
// VDS_DIS_?B_ST is where blanking STARTS, so an aperture that closes after the
// write has finished exposes memory nothing wrote, which the playback stage
// fetches as scratch. Both ends of the write are fractional, so the far edge
// has to be the floor of that sum rather than a separately rounded corner plus
// a floored length, which can land a whole unit past it.
TEST_CASE("blanking starts no later than the write ends")
{
    // The bench 1080p vertical: 584 captured lines fitting to VDS_VSCALE 533 in
    // a 1125-line raster. The write ends 1121.0 lines in, and a window closing
    // at 1122 leaves the last line of the aperture unwritten.
    const OutputWindow solved(800, 584, rasterOf(1916, 1125));
    const OutputMapping &v = solved.vertical();
    REQUIRE(v.scale().reg() == 533);
    CHECK((float)v.display().start() <= writeEndOf(v, AxisVertical));
}

// Vertically it closes on the floor of that sum LESS the trailing capture
// margin, none of which is picture: `produced` is the whole window scaled, and
// the path drops one of those units while the other is the source's own
// blanking. Measured at 800x600@60 -- the aperture solved without that term
// carries two rows of stale memory under the source's last line.
//
// The interpolator gives nothing back on this axis, which is a separate
// question and still measured: crept into Mode960p with the source's last
// picture line as the capture's last unit, the picture extends a row a step
// out to the bound with the falloff keeping its shape.
TEST_CASE("the vertical aperture closes where the write ends")
{
    SUBCASE("at the bench 800x600@60 framing, where the far bound is what stops it") {
        const OutputWindow solved(600, 384, rasterOf(2156, 1000, 1957, 999, 424, 39));
        REQUIRE(solved.vertical().scale().reg() == 410);
        CHECK(solved.vertical().display().start() == 993);
    }

    SUBCASE("and where it is the write rather than the bound") {
        const OutputWindow solved(800, 584, rasterOf(1916, 1125));
        const OutputMapping &v = solved.vertical();
        const float ends = writeEndOf(v, AxisVertical)
                         - AxisVertical.captureMargin() * v.scale().magnification();
        CHECK(v.display().start() == (int32_t)floorf(ends));
    }
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
        const OutputWindow solved(872, 512, rasterOf(Raster, 1126));
        REQUIRE(solved.horizontal().scale().reg() == 496);
        CHECK(solved.horizontal().memory().width() % 2 == 1);
    }

    SUBCASE("across the zoom range, where the parity otherwise alternates") {
        for (uint16_t capture = 600; capture <= 1500; ++capture) {
            const OutputWindow solved(capture, 512, rasterOf(Raster, 1126));
            if (!solved.usable())
                continue;
            REQUIRE(solved.horizontal().memory().width() % 2 == 1);
        }
    }

    SUBCASE("the bias costs no picture, because the reach already reserves one") {
        // The window closes one capture unit short of the write end, so a bias
        // that steps the far edge FORWARD still lands inside written memory --
        // and forward is what keeps the picture filling the screen. Back left a
        // black column at the right on every mode whose width came out even.
        for (uint16_t capture = 600; capture <= 1500; ++capture) {
            const OutputWindow solved(capture, 512, rasterOf(Raster, 1126));
            const OutputMapping &h = solved.horizontal();
            if (!h.usable())
                continue;
            float reach = floorf(writeEndOf(h, AxisHorizontal)
                                 - h.scale().magnification());
            const float bound = (float)farBound(Raster, 0);
            if (reach > bound)
                reach = bound;
            REQUIRE((float)h.memory().start() >= reach);
        }
    }

    SUBCASE("and the aperture still closes on the last column the capture filled") {
        // The memory window may carry the odd unit; the APERTURE may not. One
        // pixel past the interpolator's reach shows as a column of junk down the
        // right-hand edge, which is what the forward bias costs if both far
        // edges move together. Blanking it loses nothing: that column was never
        // captured.
        for (uint16_t capture = 600; capture <= 1500; ++capture) {
            const OutputWindow solved(capture, 512, rasterOf(Raster, 1126));
            const OutputMapping &h = solved.horizontal();
            if (!h.usable())
                continue;
            const float reach = floorf(writeEndOf(h, AxisHorizontal)
                                       - h.scale().magnification());
            REQUIRE((float)h.display().start() <= reach);
        }
    }

    SUBCASE("the window never opens past where the write ends") {
        // Biasing the width must give a unit back, never take one: memory past
        // the picture is memory the playback stage still walks.
        const OutputWindow solved(872, 512, rasterOf(Raster, 1126));
        const OutputMapping &h = solved.horizontal();
        CHECK((float)h.memory().start() <= writeEndOf(h, AxisHorizontal));
    }
}

TEST_CASE("the display window is the picture, at both ends")
{
    const OutputWindow solved(981, 512, rasterOf(1916, 1126));
    const OutputMapping &h = solved.horizontal();
    REQUIRE(h.scale().reg() == 557);

    CHECK((float)h.display().stop() > cornerOf(h, AxisHorizontal));
    CHECK((float)h.display().stop()
          <= cornerOf(h, AxisHorizontal) + h.scale().magnification() + 1.0f);

    CHECK((float)h.display().start() <= writeEndOf(h, AxisHorizontal));
    CHECK(writeEndOf(h, AxisHorizontal) - (float)h.display().start()
          <= h.scale().magnification() + 1.0f);
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
static float lastCaptureUnitRead(const Axis &axis, const OutputMapping &solved)
{
    const float lastUnit = (float)solved.display().start() - 1.0f;
    const float pos = (lastUnit - cornerOf(solved, axis))
                    * (float)solved.scale().reg() / (float)Scale::Unity;
    return floorf(pos) + 1.0f;
}

TEST_CASE("the aperture's last unit is interpolated from captured memory")
{
    // Vertically it reads the last unit the capture filled and no further --
    // one unit later than the horizontal bound, because nothing on that axis
    // has ever shown the row past it. See "the vertical aperture closes where
    // the write ends".
    SUBCASE("vertically, at the bench 320x256@50 framing") {
        const OutputWindow solved(800, 604, rasterOf(1916, 1124));
        REQUIRE(solved.vertical().scale().reg() == 552);
        CHECK(lastCaptureUnitRead(AxisVertical, solved.vertical()) <= 604.0f);
    }

    SUBCASE("horizontally, at the bench 320x256@50 framing") {
        const OutputWindow solved(1003, 512, rasterOf(1919, 1126));
        REQUIRE(solved.horizontal().scale().reg() == 568);
        CHECK(lastCaptureUnitRead(AxisHorizontal, solved.horizontal())
              <= 1003.0f - 1.0f);
    }

    SUBCASE("across the zoom range on both axes") {
        for (uint16_t capture = 400; capture <= 1500; capture += 3) {
            const OutputWindow v(800, capture, rasterOf(1916, 1124));
            if (v.vertical().usable())
                REQUIRE(lastCaptureUnitRead(AxisVertical, v.vertical())
                        <= (float)capture);

            const OutputWindow h(capture, 512, rasterOf(1919, 1126));
            if (h.horizontal().usable())
                REQUIRE(lastCaptureUnitRead(AxisHorizontal, h.horizontal())
                        <= (float)capture - 1.0f);
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
//
// Horizontal only. The vertical near end reads past the start of the frame,
// which comes back as nothing, so the unit buys no picture there and costs a
// black bar across the top -- the test below holds that end.
static float firstCaptureUnitRead(const Axis &axis, const OutputMapping &solved)
{
    return ((float)solved.display().stop() - cornerOf(solved, axis))
         * (float)solved.scale().reg() / (float)Scale::Unity;
}

// The inset lands exactly on one capture unit where the window stop is on its
// floor, and the origin is computed in float, so the equality case loses a ulp.
// A thousandth of a unit is not a framing defect.
static const float UnitSlack = 1e-3f;

TEST_CASE("the aperture's first unit is interpolated from captured memory")
{
    SUBCASE("horizontally, at the three crept magnifications") {
        const uint16_t captures[] = {1330, 1289, 683};
        const uint16_t scales[] = {904, 877, 473};
        for (int i = 0; i < 3; ++i) {
            const OutputWindow solved(captures[i], 512, rasterOf(1600, 1126));
            REQUIRE(solved.horizontal().scale().reg() == scales[i]);
            CHECK(firstCaptureUnitRead(AxisHorizontal, solved.horizontal())
                  >= 1.0f - UnitSlack);
        }
    }

    SUBCASE("across the zoom range") {
        for (uint16_t capture = 600; capture <= 1500; capture += 3) {
            const OutputWindow solved(capture, 512, rasterOf(1919, 1126));
            if (!solved.horizontal().usable())
                continue;
            REQUIRE(firstCaptureUnitRead(AxisHorizontal, solved.horizontal())
                    >= 1.0f - UnitSlack);
        }
    }
}

TEST_CASE("the vertical aperture opens on the picture, not a capture unit later")
{
    // The picture is placed on the output mode's first active line, which is
    // the first line the panel paints, so an inset there is a black bar across
    // the top of the screen rather than overscan. Reading before the first
    // written LINE reaches past the start of the frame and comes back as
    // nothing: measured clean with the vertical aperture opened eleven rows
    // before the write starts.
    // docs/investigations/the-aperture-is-inset-one-capture-unit-at-each-end.md
    SUBCASE("at 1080p") {
        const uint16_t ActiveStart = 41;
        const OutputWindow solved(800, 512, rasterOf(1916, 1125, 1852, 1121,
                                                     0, ActiveStart));
        REQUIRE(solved.vertical().scale().reg() == 486);
        CHECK(solved.vertical().display().stop() == ActiveStart);
    }

    SUBCASE("at 576p") {
        const uint16_t ActiveStart = 44;
        const OutputWindow solved(800, 256, rasterOf(2070, 625, 1943, 620,
                                                     0, ActiveStart));
        REQUIRE(solved.vertical().scale().reg() == 455);
        CHECK(solved.vertical().display().stop() == ActiveStart);
    }

    SUBCASE("across the zoom range") {
        // The aperture opens where the write does, plus the origin and nothing
        // else. Horizontally a whole capture unit is added on top; here that
        // would be a black bar across the top of the screen.
        const uint16_t ActiveStart = 41;
        for (uint16_t capture = 380; capture <= 1100; ++capture) {
            const OutputWindow solved(800, capture,
                                      rasterOf(1916, 1125, 1852, 1121, 0, ActiveStart));
            const OutputMapping &v = solved.vertical();
            if (!v.usable())
                continue;
            const float gap = (float)v.display().stop() - (float)v.memory().stop();
            REQUIRE(gap <= writeOrigin(AxisVertical, v.scale().magnification()) + 0.5f);
        }
    }
}

// The bound above is taken with the window opening at 0, which no real output
// mode does: the picture starts after the mode's sync and back porch, and that
// is room the capture cannot use. Measured on 800x600@60 into Mode960p, where
// the porch is a quarter of the line -- a capture free to take the whole
// capturable region is allowed 1322 units against a display window of 1269, so
// VDS_HSCALE pins at its 1023 floor, the surplus is cropped off the far end,
// and the source's own right-hand blanking can never be shown.
TEST_CASE("the capture bound accounts for the output mode's back porch")
{
    const uint16_t Raster = 1790;       // Mode960p solved at 60 Hz
    const uint16_t ActiveStart = 426;   // after the sync pulse and back porch
    const uint16_t ActiveStop = 1695;

    const Tv5725::OutputTiming behind = rasterOf(Raster, 1000, ActiveStop, 999,
                                                 ActiveStart, 0);
    const float allowed = room(AxisHorizontal, Raster, ActiveStart, ActiveStop);
    const uint16_t most = OutputWindow::widestCapture(AxisHorizontal, behind);

    // Within the rounding of the scale, which is to nearest rather than up.
    CHECK(OutputWindow(most, 512, behind).horizontal().produced() <= allowed + 1.0f);

    SUBCASE("and the porch is room the capture cannot have") {
        // Taken with the window opening at 0, the bound is the whole raster's
        // and lets through a capture a quarter wider than the window can show.
        CHECK(most < OutputWindow::widestCapture(
                         AxisHorizontal, rasterOf(Raster, 1000, ActiveStop, 999, 0, 0)));
    }
}

// THE WRITE FLOOR BINDS AT THE PORCH IT EXACTLY REACHES, and the boundary is
// inclusive. Below it the picture starts at the write floor and the origin is
// charged out of the room; above it the mode's own back porch is what holds the
// picture off, and charging the origin again would leave a bar no zoom closes.
//
// Nothing else in this suite sits on the value, so a `<` here for a `<=` moves
// the capture bound by the whole origin charge and no other case notices --
// which is how it reached a commit once.
TEST_CASE("the write floor binds at the porch it exactly reaches")
{
    const uint16_t Boundary = (uint16_t)(BenchHorizontal.floor + BenchHorizontal.constant);
    const uint16_t Raster = 1916, Frame = 1126;

    const uint16_t at = OutputWindow::narrowestCapture(
        AxisHorizontal, rasterOf(Raster, Frame, 0, 0, Boundary, 0));
    const uint16_t past = OutputWindow::narrowestCapture(
        AxisHorizontal, rasterOf(Raster, Frame, 0, 0, Boundary + 1, 0));

    // Charged at the boundary and not one unit past it, so the smallest capture
    // that still fills the raster jumps by the charge.
    REQUIRE(at > 0);
    CHECK(past > at);
    CHECK(past - at >= 20);
}

// `--dump` prints the whole-solution grid for inspection by hand.
static void dumpGrid()
{
    for (uint16_t raster : {1445, 1716, 858})
        for (unsigned ch = 100; ch <= 1100; ch += 83)
            for (unsigned cv = 100; cv <= 600; cv += 71) {
                const OutputWindow s(ch, cv, rasterOf(raster, 1126));
                std::printf("whole %u %u %u %u %u %.4f %.4f %d %d %d %d %d %d %d %d\n",
                            raster, ch, cv,
                            s.horizontal().scale().reg(), s.vertical().scale().reg(),
                            s.horizontal().produced(), s.vertical().produced(),
                            s.horizontal().memory().stop(), s.horizontal().memory().start(),
                            s.horizontal().display().stop(), s.horizontal().display().start(),
                            s.vertical().memory().stop(), s.vertical().memory().start(),
                            s.vertical().display().stop(), s.vertical().display().start());
            }

    // The active window, which the grid above cannot see because it never varies
    // activeStart -- so a divergence in that parameter would go unnoticed.
    for (uint16_t raster : {1918, 2301, 2877})
        for (uint16_t activeStart : {0, 40, 140, 167, 209, 400})
            for (unsigned capture = 200; capture <= 1200; capture += 143) {
                const OutputWindow s(capture, 512,
                                     rasterOf(raster, 1126, 0, 0, activeStart, activeStart));
                std::printf("active %u %u %u %u %u %.4f %.4f %d %d %d %d %d %d %d %d\n",
                            raster, activeStart, capture,
                            s.horizontal().scale().reg(), s.vertical().scale().reg(),
                            s.horizontal().produced(), s.vertical().produced(),
                            s.horizontal().memory().stop(), s.horizontal().memory().start(),
                            s.horizontal().display().stop(), s.horizontal().display().start(),
                            s.vertical().memory().stop(), s.vertical().memory().start(),
                            s.vertical().display().stop(), s.vertical().display().start());
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
