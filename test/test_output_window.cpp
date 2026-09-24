// Host-compiled unit tests for Tv5725::OutputWindow -- `make -C test output-window`.
// Both axes solved from the capture and the raster alone -- nothing is read
// back off the chip. docs/firmware-geometry-engine.md.
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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
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
        CHECK(s.horizontal().memory().stop() >= 8);
        CHECK(s.vertical().memory().stop() >= 0);
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
        OutputWindow dropped = OutputWindow(0, 0, rasterOf(1445, 1126));
        CHECK(dropped.horizontal().produced() == 0.0f);
        CHECK(dropped.vertical().produced() == 0.0f);
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

// The raster's edge is the wrong far bound: a display window taken up to
// VDS_HSYNC_RST leaves too little front porch and the colours come out wrong.
// Measured on the bench, RiscPC 320x256@50 into a 1916 px raster, the window is
// good at 1900 and bad at 1910 -- a floor of about 16 px, which CEA-861's minimum

TEST_CASE("the horizontal window goes where the geometry puts it")
{
    // Asserting an absence. Memory::fetchFor sizes PB_FETCH_NUM from the capture
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

TEST_CASE("the scale is exactly what fitToRaster produced")
{
    // The scale must never fall back to a different HSCALE -- it has to move
    // fluidly with the zoom pad rather than jump. A shrink search moves it by up
    // to 56 counts and steps backwards 71 times across this range while the pad
    // goes one way.
    uint16_t previous = 0;
    for (uint16_t capture = 400; capture <= 1009; ++capture) {
        OutputWindow solved(capture, 512, rasterOf(1445, 1126));
        REQUIRE(solved.usable());
        OutputMapping plain = OutputWindow::solve(AxisHorizontal, capture, solved.horizontal().scale(), 1445);
        CHECK(solved.horizontal().memory().stop() == plain.memory().stop());
        CHECK(solved.horizontal().memory().start() == plain.memory().start());
        CHECK(solved.horizontal().display().stop() == plain.display().stop());
        CHECK(solved.horizontal().display().start() == plain.display().start());
        CHECK(solved.horizontal().scale().reg() >= previous);
        previous = solved.horizontal().scale().reg();
    }
}

TEST_CASE("both axes allocate only the memory the picture occupies")
{
    // A property of the memory, not of one axis, so Axis::solve applies it and
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


// `--dump` prints the whole-solution grid for inspection by hand.
static void dumpGrid()
{
    for (uint16_t raster : {1445, 1716, 858})
        for (unsigned ch = 100; ch <= 1100; ch += 83)
            for (unsigned cv = 100; cv <= 600; cv += 71) {
                OutputWindow s(ch, cv, rasterOf(raster, 1126));
                std::printf("whole %u %u %u %u %u %d %d %d %d %d %d %d %d\n",
                            raster, ch, cv, s.horizontal().scale().reg(), s.vertical().scale().reg(),
                            s.horizontal().display().stop(), s.horizontal().memory().stop(), s.horizontal().display().stop(), s.horizontal().display().start(),
                            s.vertical().display().stop(), s.vertical().memory().stop(), s.vertical().display().stop(), s.vertical().display().start());
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
    // AxisHorizontal's floor is 8 and its write origin's constant is 55.
    const uint16_t Boundary = 63;
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
