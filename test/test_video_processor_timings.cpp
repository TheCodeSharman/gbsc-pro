// Host-compiled unit tests for Tv5725::VideoProcessorTimings -- `make -C test output-windows`.
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
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessorTimings.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessorTimings.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

using namespace Tv5725;

// --- everything from the capture and the raster alone -------------------------

TEST_CASE("nothing is inherited from the registers")
{
    // The bench state: 798 IF units captured on a 1126-unit line, 513 units
    // of a 312-line frame, onto a 1445 x 1126 output raster.
    VideoProcessorTimings s(798, 513, 1445, 1126);

    SUBCASE("both scales are computed, not read") {
        CHECK(((s.horizontalScale() >= AxisHorizontal.scaleMin()) && (s.horizontalScale() <= Scale::Max)));
        CHECK(((s.verticalScale() >= AxisVertical.scaleMin()) && (s.verticalScale() <= Scale::Max)));
    }

    SUBCASE("both memory windows clear their floor") {
        CHECK(s.memory().horizontal().stop() >= AxisHorizontal.windowStopMin());
        CHECK(s.memory().vertical().stop() >= AxisVertical.windowStopMin());
    }

    SUBCASE("neither window reaches the value that wraps") {
        CHECK(((s.memory().horizontal().start() < 1444) && (s.display().horizontal().start() < 1444)));
        CHECK(((s.memory().vertical().start() < 1125) && (s.display().vertical().start() < 1125)));
    }

    SUBCASE("the vertical picture is not doubled") {
        // ~2200 would mean the capture had been doubled on the way through.
        CHECK(((s.producedVertical() > 900) && (s.producedVertical() < 1130)));
    }

    SUBCASE("the same capture always gives the same answer") {
        VideoProcessorTimings again = VideoProcessorTimings(798, 513, 1445, 1126);
        CHECK(((again.horizontalScale() == s.horizontalScale())
               && (again.verticalScale() == s.verticalScale())));
        CHECK(again.memory().horizontal().stop() == s.memory().horizontal().stop());
        CHECK(again.display().vertical().start() == s.display().vertical().start());
    }

    SUBCASE("a capture that reads zero yields no picture rather than a wrong one") {
        VideoProcessorTimings dropped = VideoProcessorTimings(0, 0, 1445, 1126);
        CHECK(dropped.producedHorizontal() == 0.0f);
        CHECK(dropped.producedVertical() == 0.0f);
    }
}

TEST_CASE("the solution carries the front porch to both axes")
{
    const uint16_t Raster = 1916, Frame = 1126;
    const uint16_t StopH = 1852, StopV = 1121;

    VideoProcessorTimings solved(1008, 532, Raster, Frame, StopH, StopV);
    CHECK(solved.display().horizontal().start() <= (int32_t)StopH);
    CHECK(solved.display().vertical().start() <= (int32_t)StopV);

    SUBCASE("and without one the raster edge still bounds it") {
        VideoProcessorTimings plain(1008, 532, Raster, Frame);
        CHECK(plain.display().horizontal().start() > (int32_t)StopH);
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
        VideoProcessorTimings solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        CHECK(solved.memory().horizontal().start() == solved.display().horizontal().start());
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
        VideoProcessorTimings solved(capture, 512, 1445, 1126);
        REQUIRE(solved.usable());
        AxisSolution plain = AxisHorizontal.solve(capture, solved.horizontalScale(), 1445);
        CHECK(solved.memory().horizontal().stop() == plain.memory().stop());
        CHECK(solved.memory().horizontal().start() == plain.memory().start());
        CHECK(solved.display().horizontal().stop() == plain.display().stop());
        CHECK(solved.display().horizontal().start() == plain.display().start());
        CHECK(solved.horizontalScale().reg() >= previous);
        previous = solved.horizontalScale().reg();
    }
}

TEST_CASE("both axes allocate only the memory the picture occupies")
{
    // A property of the memory, not of one axis, so Axis::solve applies it and
    // both axes get it. The artefact was seen horizontally, but a rule holding
    // on one axis only would be a special case nobody measured.
    VideoProcessorTimings solved(749, 512, 1445, 1126);
    CHECK(solved.memory().horizontal().start() == solved.display().horizontal().start());
    CHECK(solved.memory().vertical().start() == solved.display().vertical().start());

    SUBCASE("and neither reaches the value that wraps") {
        // VDS_VB_ST at VDS_VSYNC_RST rolls the frame; VDS_HB_ST at
        // VDS_HSYNC_RST wraps.
        CHECK(solved.memory().horizontal().start() <= 1445 - 2);
        CHECK(solved.memory().vertical().start() <= 1126 - 2);
    }
}


// `--dump` prints the whole-solution grid for inspection by hand.
static void dumpGrid()
{
    for (uint16_t raster : {1445, 1716, 858})
        for (unsigned ch = 100; ch <= 1100; ch += 83)
            for (unsigned cv = 100; cv <= 600; cv += 71) {
                VideoProcessorTimings s(ch, cv, raster, 1126);
                std::printf("whole %u %u %u %u %u %d %d %d %d %d %d %d %d\n",
                            raster, ch, cv, s.horizontalScale().reg(), s.verticalScale().reg(),
                            s.display().horizontal().stop(), s.memory().horizontal().stop(), s.display().horizontal().stop(), s.display().horizontal().start(),
                            s.display().vertical().stop(), s.memory().vertical().stop(), s.display().vertical().stop(), s.display().vertical().start());
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
