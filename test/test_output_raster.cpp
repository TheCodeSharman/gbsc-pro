// Host-compiled unit tests for src/tv5725/OutputRaster.h -- `make -C test
// output-raster`.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// solved grid for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputRaster.h"

using namespace Tv5725;

TEST_CASE("htotal is the frame clock budget over the frame height, floored")
{
    // A raster needs horizontalTotal x frameLines x fieldRate hertz, so horizontalTotal floors:
    // rounding up asks the part for a clock above the target.
    CHECK(OutputRaster::horizontalTotalFor(108000000u, 1126, 50.0f) == 1918);
    CHECK(OutputRaster::horizontalTotalFor(108000000u, 1126, 60.0f) == 1598);
    CHECK(OutputRaster::horizontalTotalFor(81000000u, 1126, 50.0f) == 1438);
    CHECK(OutputRaster::horizontalTotalFor(129600000u, 1126, 50.0f) == 2301);
    CHECK(OutputRaster::horizontalTotalFor(162000000u, 1126, 50.0f) == 2877);

    SUBCASE("and a raster that will not fit its register is refused") {
        // VDS_HSYNC_RST is twelve bits. Wrapping would roll the picture.
        CHECK(OutputRaster::horizontalTotalFor(162000000u, 262, 50.0f) == 0);
        CHECK(OutputRaster::horizontalTotalFor(0u, 1126, 50.0f) == 0);
        CHECK(OutputRaster::horizontalTotalFor(108000000u, 0, 50.0f) == 0);
        CHECK(OutputRaster::horizontalTotalFor(108000000u, 1126, 0.0f) == 0);
    }
}

TEST_CASE("the divider is the largest seed at or under the ceiling")
{
    // The seed only has to put the Si5351 in range -- the rate is steered to
    // whatever the raster demands afterwards -- so take the most clock available.
    CHECK(OutputRaster::dividerFor(1126, 50.0f, 129600000u) == 0x95);
    CHECK(OutputRaster::dividerFor(1126, 50.0f, 108000000u) == 0x85);
    CHECK(OutputRaster::dividerFor(1126, 50.0f, 162000000u) == 0xA5);

    SUBCASE("and it skips seeds whose htotal would overflow the register") {
        // A short frame at a high clock cannot be expressed, so the seed below it
        // is the answer rather than a wrapped raster.
        CHECK(OutputRaster::dividerFor(262, 50.0f, 162000000u) != 0xA5);
    }

    SUBCASE("an unmeasurable field rate yields nothing, not a guess") {
        CHECK(OutputRaster::dividerFor(1126, 0.0f, 129600000u) == 0);
    }
}

TEST_CASE("the sync pulse is CEA-861's, converted to the clock the line runs at")
{
    // **THE CONVERSION IS UNAMBIGUOUS BECAUSE THE STANDARD IS A TIME.** 1080p sync
    // is 44 px at 148.5 MHz = 296.3 ns and the back porch 148 px = 996.6 ns, both
    // identical at 50 and 60 Hz -- only the front porch absorbs the rate
    // difference. So the pixel count scales with OUR clock.
    //
    // These are the values the bench actually applied, 2026-08-11/12.
    RasterSolution at108 = Mode1080p.solve(50.0f, 108000000u);
    CHECK(at108.horizontalTotal == 1918);
    CHECK(at108.hsyncStart == 0);
    CHECK(at108.hsyncStop == 32);
    CHECK(at108.activeStart == 32 + 108);

    RasterSolution at1296 = Mode1080p.solve(50.0f, 129600000u);
    CHECK(at1296.horizontalTotal == 2301);
    CHECK(at1296.hsyncStop == 38);

    RasterSolution at162 = Mode1080p.solve(50.0f, 162000000u);
    CHECK(at162.horizontalTotal == 2877);
    CHECK(at162.hsyncStop == 48);

    SUBCASE("all three are the same 296 ns, which is the point") {
        // If they were a fixed pixel count instead, this would fail at two of the
        // three clocks.
        CHECK_MESSAGE(at108.hsyncStop * 1000000000.0 / at108.demandedHz() > 280.0,
                      "108 MHz pulse too short");
        CHECK_MESSAGE(at162.hsyncStop * 1000000000.0 / at162.demandedHz() < 320.0,
                      "162 MHz pulse too long");
    }
}

TEST_CASE("the default ceiling is the highest clock the bench demonstrated")
{
    // Swept 2026-08-11 on the RiscPC at 320x256@50: 108 and 129.6 MHz both
    // sharp, 162 MHz flickers then goes black. A FLOOR on the true limit rather
    // than the limit -- nothing between 129.6 and 162 has been tried. Not the
    // datasheet's 108 MHz CLKOUT figure, which rates a pad PAD_CKOUT_ENZ
    // disables.
    CHECK(OutputRaster::WorkingCeilingHz == 129600000u);

    RasterSolution best = Mode1080p.solve(50.0f);
    CHECK(best.horizontalTotal == 2301);
    CHECK(best.verticalTotal == 1126);
    CHECK(best.divider == 0x95);
    CHECK(best.demandedHz() <= OutputRaster::WorkingCeilingHz);
}

TEST_CASE("a solved raster reports whether it is usable")
{
    CHECK(Mode1080p.solve(50.0f).usable());

    SUBCASE("and an unmeasured field rate is not") {
        // A raster written from a measurement that did not happen is how the
        // screen goes dark with every register looking correct.
        CHECK_FALSE(Mode1080p.solve(0.0f).usable());
    }
}

TEST_CASE("the engine's ceiling leaves the zoom control somewhere to go")
{
    // NOT the same question as WorkingCeilingHz, which is what the part
    // demonstrably does. This is a usability limit: a wider raster costs zoom
    // travel, because the zoom floor is raster / maxMagnification while the
    // default capture depends on the INPUT line alone.
    //
    // **RAISING IT IS AN UNTRIED BENCH EXPERIMENT.** The argument for 108 over
    // 129.6 rests on a fixed scale floor of 500 putting the zoom floor on the
    // default framing, and Axis::scaleMin() is derived -- so the argument does
    // not stand on its own terms. 129.6 MHz has been swept by hand and never run
    // through the engine, which is what would settle it.
    CHECK(OutputRaster::EngineCeilingHz == 108000000u);

    RasterSolution solved = Mode1080p.solve(50.0f, OutputRaster::EngineCeilingHz);
    CHECK(solved.horizontalTotal == 1918);
    CHECK(solved.verticalTotal == 1126);
    CHECK(solved.demandedHz() <= OutputRaster::EngineCeilingHz);

    SUBCASE("which is a third more line than the tables shipped") {
        // pal_1920x1080 ships 1445. The point of computing the raster at all.
        CHECK(solved.horizontalTotal > 1445);
    }

    SUBCASE("and leaves real travel where 129.6 MHz left none") {
        // ceil(raster * scaleMin / Unity) is Axis::smallestCapture's arithmetic.
        // The bench's usable capture tops out near 1187 -- the source line is
        // 1277 IF units and the window starts at 90 to clear the hsync pulse.
        const uint32_t scaleMin = 500, unity = 1024, usable = 1187;
        uint32_t at108 = (1918u * scaleMin + unity - 1) / unity;
        uint32_t at129 = (2298u * scaleMin + unity - 1) / unity;
        CHECK(at129 == 1123);              // measured clamp, exactly
        CHECK(usable - at129 < 100);       // ~64 units, and none at the default
        CHECK(usable - at108 > 200);       // ~250 units
    }
}

TEST_CASE("a frame height selects the output mode that owns it")
{
    // The frame height is the one part of the raster that is a CHOICE rather
    // than a calculation. Everything else -- the line total, the divider, both
    // sync pulses, the active window -- is derived from it and the measured
    // field rate.
    CHECK(OutputRaster::modeFor(1126) == &Mode1080p);
    CHECK(OutputRaster::modeFor(751) == &Mode720p);

    SUBCASE("and an unknown height selects nothing") {
        // NOT a fallback to 1080p. A height nobody has swept gets the preset
        // table's own raster left alone, which is the same rule Memory used to
        // have and DisplayClock still has: better an untouched value than one
        // computed for a mode this is not. 625 is PAL 576p, 525 NTSC 480p --
        // both real output modes with no OutputMode entry yet.
        CHECK_FALSE(OutputRaster::modeFor(625));
        CHECK_FALSE(OutputRaster::modeFor(525));
        CHECK_FALSE(OutputRaster::modeFor(0));
    }
}

// --- the oracle grid ---------------------------------------------------------

// `--dump` prints the solved grid for inspection by hand.
static void dumpGrid()
{
    // 64.8 MHz and 78.3 Hz are the only states where deriving the sync pulse
    // from the seed's nominal clock and from the raster's real clock give
    // different pixel counts, so the grid is blind to that difference without
    // them. 78.3 Hz is no source's field rate, but it is inside the firmware's
    // own 47..86 Hz guard.
    static const uint32_t ceilings[] = {64800000u, 81000000u, 108000000u,
                                        129600000u, 162000000u};
    static const float rates[] = {50.0f, 59.94f, 60.0f, 78.3f};

    for (unsigned c = 0; c < sizeof(ceilings) / sizeof(ceilings[0]); ++c) {
        for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
            RasterSolution solved = Mode1080p.solve(rates[r], ceilings[c]);
            std::printf("1080p %u %.2f %u %u %u %u %u %u %u\n",
                        (unsigned)ceilings[c], (double)rates[r],
                        (unsigned)solved.horizontalTotal, (unsigned)solved.verticalTotal,
                        (unsigned)solved.divider, (unsigned)solved.hsyncStart,
                        (unsigned)solved.hsyncStop, (unsigned)solved.activeStart,
                        (unsigned)solved.activeWidth());
        }
    }

    // horizontalTotalFor over a grid of frame heights, which is where an off-by-one in the
    // floor or the register bound would hide.
    static const uint16_t heights[] = {262, 526, 626, 751, 1001, 1067, 1126, 1250};
    for (unsigned c = 0; c < sizeof(ceilings) / sizeof(ceilings[0]); ++c) {
        for (unsigned h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
            for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
                std::printf("htotal %u %u %.2f %u\n",
                            (unsigned)ceilings[c], (unsigned)heights[h],
                            (double)rates[r],
                            (unsigned)OutputRaster::horizontalTotalFor(
                                ceilings[c], heights[h], rates[r]));
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dumpGrid();
        return 0;
    }
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
