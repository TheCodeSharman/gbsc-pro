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

// --- everything from the capture and the raster alone -------------------------

TEST_CASE("nothing is inherited from the registers")
{
    // The bench state: 798 IF units captured on a 1126-unit line, 513 units
    // of a 312-line frame, onto a 1445 x 1126 output raster.
    OutputWindow s(798, 513, 1445, 1126);

    SUBCASE("both scales are computed, not read") {
        CHECK(((s.scaleOn(AxisHorizontal) >= Scale::Min) && (s.scaleOn(AxisHorizontal) <= Scale::Max)));
        CHECK(((s.scaleOn(AxisVertical) >= Scale::Min) && (s.scaleOn(AxisVertical) <= Scale::Max)));
    }

    SUBCASE("both memory windows clear their floor") {
        CHECK(s.on(AxisHorizontal).memory().stop() >= AxisHorizontal.windowStopMin());
        CHECK(s.on(AxisVertical).memory().stop() >= AxisVertical.windowStopMin());
    }

    SUBCASE("neither window reaches the value that wraps") {
        CHECK(((s.on(AxisHorizontal).memory().start() < 1444) && (s.on(AxisHorizontal).display().start() < 1444)));
        CHECK(((s.on(AxisVertical).memory().start() < 1125) && (s.on(AxisVertical).display().start() < 1125)));
    }

    SUBCASE("the vertical picture is not doubled") {
        // ~2200 would mean the capture had been doubled on the way through.
        CHECK(((s.on(AxisVertical).produced() > 900) && (s.on(AxisVertical).produced() < 1130)));
    }

    SUBCASE("the same capture always gives the same answer") {
        OutputWindow again = OutputWindow(798, 513, 1445, 1126);
        CHECK(((again.scaleOn(AxisHorizontal) == s.scaleOn(AxisHorizontal))
               && (again.scaleOn(AxisVertical) == s.scaleOn(AxisVertical))));
        CHECK(again.on(AxisHorizontal).memory().stop() == s.on(AxisHorizontal).memory().stop());
        CHECK(again.on(AxisVertical).display().start() == s.on(AxisVertical).display().start());
    }

    SUBCASE("a capture that reads zero yields no picture rather than a wrong one") {
        OutputWindow dropped = OutputWindow(0, 0, 1445, 1126);
        CHECK(dropped.on(AxisHorizontal).produced() == 0.0f);
        CHECK(dropped.on(AxisVertical).produced() == 0.0f);
    }
}

TEST_CASE("the solution carries the front porch to both axes")
{
    const uint16_t Raster = 1916, Frame = 1126;
    const uint16_t StopH = 1852, StopV = 1121;

    OutputWindow solved(1008, 532, Raster, Frame, StopH, StopV);
    CHECK(solved.on(AxisHorizontal).display().start() <= (int32_t)StopH);
    CHECK(solved.on(AxisVertical).display().start() <= (int32_t)StopV);

    SUBCASE("and without one the raster edge still bounds it") {
        // Compared against the solution that HAS a porch rather than against
        // the porch itself: the window gives back Axis::margin at the far edge,
        // so it sits inside either bound and the porch is the tighter one.
        OutputWindow plain(1008, 532, Raster, Frame);
        CHECK(plain.on(AxisHorizontal).display().start()
              > solved.on(AxisHorizontal).display().start());
        CHECK(plain.on(AxisHorizontal).display().start() < (int32_t)Raster);
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
        OutputWindow solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        // The far edges part by the parity unit and no more, and the MEMORY one
        // is the wider: the fetch covers every column the aperture shows.
        const int32_t spare = solved.on(AxisHorizontal).memory().start()
                            - solved.on(AxisHorizontal).display().start();
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
        OutputWindow solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        AxisSolution plain = AxisHorizontal.solve(capture, solved.scaleOn(AxisHorizontal), 1445);
        CHECK(solved.on(AxisHorizontal).memory().stop() == plain.memory().stop());
        CHECK(solved.on(AxisHorizontal).memory().start() == plain.memory().start());
        CHECK(solved.on(AxisHorizontal).display().stop() == plain.display().stop());
        CHECK(solved.on(AxisHorizontal).display().start() == plain.display().start());
        CHECK(solved.scaleOn(AxisHorizontal).reg() >= previous);
        previous = solved.scaleOn(AxisHorizontal).reg();
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
    OutputWindow solved(749, 512, 1445, 1126);
    const int32_t spare = solved.on(AxisHorizontal).memory().start()
                        - solved.on(AxisHorizontal).display().start();
    CHECK(spare >= 0);
    CHECK(spare <= 1);
    CHECK(solved.on(AxisVertical).memory().start() == solved.on(AxisVertical).display().start());

    SUBCASE("and neither reaches the value that wraps") {
        // VDS_VB_ST at VDS_VSYNC_RST rolls the frame; VDS_HB_ST at
        // VDS_HSYNC_RST wraps.
        CHECK(solved.on(AxisHorizontal).memory().start() <= 1445 - 2);
        CHECK(solved.on(AxisVertical).memory().start() <= 1126 - 2);
    }
}


// `--dump` prints the whole-solution grid for inspection by hand.
static void dumpGrid()
{
    for (uint16_t raster : {1445, 1716, 858})
        for (unsigned ch = 100; ch <= 1100; ch += 83)
            for (unsigned cv = 100; cv <= 600; cv += 71) {
                OutputWindow s(ch, cv, raster, 1126);
                std::printf("whole %u %u %u %u %u %d %d %d %d %d %d %d %d\n",
                            raster, ch, cv, s.scaleOn(AxisHorizontal).reg(), s.scaleOn(AxisVertical).reg(),
                            s.on(AxisHorizontal).display().stop(), s.on(AxisHorizontal).memory().stop(), s.on(AxisHorizontal).display().stop(), s.on(AxisHorizontal).display().start(),
                            s.on(AxisVertical).display().stop(), s.on(AxisVertical).memory().stop(), s.on(AxisVertical).display().stop(), s.on(AxisVertical).display().start());
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
