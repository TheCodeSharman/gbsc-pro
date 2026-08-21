// Host-compiled unit tests for src/tv5725/OutputRaster.h -- `make -C test
// output-raster`.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// solved grid for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "SketchSeam.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

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
    // The standard is a TIME, so the conversion is unambiguous: 1080p sync is
    // 44 px at 148.5 MHz = 296.3 ns and the back porch 148 px = 996.6 ns, both
    // identical at 50 and 60 Hz because only the front porch absorbs the rate
    // difference. The pixel count then scales with OUR clock. The bench figures
    // 1918/2301/2877 were taken at 1126 lines; CEA's 1125 raises each by 2-3,
    // since a shorter frame buys a longer line.
    RasterSolution at108 = Mode1080p.solve(50.0f, 108000000u);
    CHECK(at108.horizontalTotal == 1920);
    CHECK(at108.hsyncStart == 0);
    CHECK(at108.hsyncStop == 32);
    CHECK(at108.activeStart == 32 + 108);

    RasterSolution at1296 = Mode1080p.solve(50.0f, 129600000u);
    CHECK(at1296.horizontalTotal == 2304);
    CHECK(at1296.hsyncStop == 38);

    RasterSolution at162 = Mode1080p.solve(50.0f, 162000000u);
    CHECK(at162.horizontalTotal == 2880);
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

// A minimum RESERVE, not the standard's actual front porch. CEA absorbs the field
// rate difference in the front porch -- 1080p50 runs 528 px against 1080p60's 88 --
// so the standard's own value is not a target, and what is left above the minimum
// stays leftover for the picture. 1080p60's 88 px at 148.5 MHz is the shortest
// front porch in the standard, so it is the floor: 592.6 ns.
//
// The board's own requirement is a floor of about 16 px, which the standard's
// 64 px at 108 MHz clears. docs/scaler-geometry-model.md "The output front
// porch"
TEST_CASE("the front porch reserves CEA-861's minimum at our own clock")
{
    RasterSolution at108 = Mode1080p.solve(50.0f, 108000000u);
    CHECK(at108.horizontalTotal == 1920);
    CHECK(at108.activeStop == 1920 - 64);

    RasterSolution at1296 = Mode1080p.solve(50.0f, 129600000u);
    CHECK(at1296.horizontalTotal == 2304);
    CHECK(at1296.activeStop == 2304 - 77);

    SUBCASE("which is the same TIME at both clocks, not the same pixel count") {
        // A fixed pixel count would fail one of these two.
        double lo = (at108.horizontalTotal - at108.activeStop) * 1e9
                    / at108.demandedHz();
        double hi = (at1296.horizontalTotal - at1296.activeStop) * 1e9
                    / at1296.demandedHz();
        CHECK(lo > 580.0);
        CHECK(lo < 605.0);
        CHECK(hi > 580.0);
        CHECK(hi < 605.0);
    }

    SUBCASE("and the active window is what lies between the two porches") {
        CHECK(at108.activeWidth() == at108.activeStop - at108.activeStart);
    }

    SUBCASE("vertically the porch is the standard's lines, needing no conversion") {
        // 1080p CEA-861 is 1080 active + 4 front + 5 sync + 36 back = 1125, so the
        // active window is 41..1121 -- which is the encoder window measured on the
        // bench, 1121 - 41 = 1080 exactly.
        CHECK(at108.activeLinesStart == 41);
        CHECK(at108.activeLinesStop == 1121);
    }
}

TEST_CASE("the default ceiling is the highest clock the bench demonstrated")
{
    // 108 and 129.6 MHz are both sharp and 162 MHz flickers then goes black,
    // so this is a FLOOR on the true limit rather than the limit -- nothing
    // between 129.6 and 162 has been tried. Not the datasheet's 108 MHz CLKOUT
    // figure, which rates a pad PAD_CKOUT_ENZ disables.
    CHECK(OutputRaster::WorkingCeilingHz == 129600000u);

    RasterSolution best = Mode1080p.solve(50.0f);
    CHECK(best.horizontalTotal == 2304);
    CHECK(best.verticalTotal == 1125);
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
    CHECK(solved.horizontalTotal == 1920);
    CHECK(solved.verticalTotal == 1125);
    CHECK(solved.demandedHz() <= OutputRaster::EngineCeilingHz);

    SUBCASE("which is a third more line than the tables shipped") {
        // pal_1920x1080 ships 1445. The point of computing the raster at all.
        CHECK(solved.horizontalTotal > 1445);
    }
}

TEST_CASE("a frame height selects the output mode that owns it")
{
    // The frame height is the one part of the raster that is a CHOICE rather
    // than a calculation. Everything else -- the line total, the divider, both
    // sync pulses, the active window -- is derived from it and the measured
    // field rate.
    CHECK(OutputRaster::modeFor(1125) == &Mode1080p);
    CHECK(OutputRaster::modeFor(1066) == &Mode1024p);
    CHECK(OutputRaster::modeFor(1000) == &Mode960p);
    CHECK(OutputRaster::modeFor(750) == &Mode720p);
    CHECK(OutputRaster::modeFor(625) == &Mode576p);
    CHECK(OutputRaster::modeFor(525) == &Mode480p);

    SUBCASE("and an unknown height selects nothing") {
        // NOT a fallback to 1080p: a height nobody has swept keeps whatever
        // raster it had, the same rule DisplayClock follows. 264 and 314 are the
        // dropped downscale tables'; 1126 and 751 are the one-line-long totals
        // every shipped table carried, and a build still producing those should
        // find no mode rather than the nearest one.
        CHECK_FALSE(OutputRaster::modeFor(264));
        CHECK_FALSE(OutputRaster::modeFor(314));
        CHECK_FALSE(OutputRaster::modeFor(1126));
        CHECK_FALSE(OutputRaster::modeFor(751));
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

TEST_CASE("the output mode comes from the user's preference, not from the chip")
{
    // The mode arrives explicitly rather than being read back from
    // GBS::VDS_VSYNC_RST, whose only writer is the preset table it replaces.
    CHECK((OutputRaster::modeForPreference(Output1080P, 50.0f) == &Mode1080p));
    CHECK((OutputRaster::modeForPreference(Output1024P, 50.0f) == &Mode1024p));
    CHECK((OutputRaster::modeForPreference(Output960P, 50.0f) == &Mode960p));
    CHECK((OutputRaster::modeForPreference(Output720P, 50.0f) == &Mode720p));
}

TEST_CASE("the four resolutions that are one height resolve to it at either rate")
{
    // 1080p, 1024p, 960p and 720p ship ONE frame height across their PAL and
    // NTSC tables -- verified in test_output_raster.py against the archive --
    // so the field rate cannot change which mode they select. Only the clock
    // and horizontalTotal move with the rate, inside solve().
    CHECK((OutputRaster::modeForPreference(Output1080P, 50.0f)
           == OutputRaster::modeForPreference(Output1080P, 59.94f)));
    CHECK((OutputRaster::modeForPreference(Output1024P, 50.0f)
           == OutputRaster::modeForPreference(Output1024P, 59.94f)));
    CHECK((OutputRaster::modeForPreference(Output960P, 50.0f)
           == OutputRaster::modeForPreference(Output960P, 59.94f)));
    CHECK((OutputRaster::modeForPreference(Output720P, 50.0f)
           == OutputRaster::modeForPreference(Output720P, 59.94f)));
}

TEST_CASE("the SD preference is two different resolutions, and the rate picks one")
{
    // Output480P is not one mode at two rates: 480p and 576p are different
    // active line counts sharing a PresetPreference value, which is why their
    // tables ship 526 and 626 where every other mode's pair agrees. So it is the
    // one preference the field rate disambiguates, and it picks a RESOLUTION
    // rather than adjusting a timing. docs/vesa-gtf.md settled the split.
    CHECK((OutputRaster::modeForPreference(Output480P, 59.94f) == &Mode480p));
    CHECK((OutputRaster::modeForPreference(Output480P, 50.0f) == &Mode576p));

    CHECK(Mode480p.activeLines() == 480);
    CHECK(Mode480p.frameLines() == 525);    // 480 + 9 front + 6 sync + 30 back
    CHECK(Mode576p.activeLines() == 576);
    CHECK(Mode576p.frameLines() == 625);    // 576 + 5 front + 5 sync + 39 back
}

TEST_CASE("a preference that is not a resolution resolves to no mode")
{
    // 0 leaves the caller to fall back rather than silently solving the wrong
    // raster. 6 was OutputDownscale, which went with the tables on 2026-08-14 --
    // reachable from the OLED alone, never from the web UI, and its one
    // assignment site in the sketch was already commented out -- so it is cast
    // rather than named: the enumerator no longer exists.
    CHECK((OutputRaster::modeForPreference((PresetPreference)6, 50.0f) == 0));
    CHECK((OutputRaster::modeForPreference(OutputBypass, 50.0f) == 0));
}

TEST_CASE("a custom preset resolves to no mode, because its bytes are the mode")
{
    // OutputCustomized is not a resolution -- it means "load the one I saved",
    // and that file's own frame height is the answer. The caller reads it back,
    // which is the last place that inherits on purpose; it goes when a saved
    // slot records the inputs to the calculation instead of a register dump.
    CHECK((OutputRaster::modeForPreference(OutputCustomized, 50.0f) == 0));
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

TEST_CASE("a mode's frame total is its active lines plus the standard blanking")
{
    // The shipped tables run one line long, all six of them, against the
    // standards:
    //
    //     1080p 1126 / CEA 1125    1024p 1067 / VESA 1066    480p 526 / CEA 525
    //      720p  751 / CEA  750     960p 1001 / VESA 1000    576p 626 / CEA 625
    //
    // RD-5725-1.1 on VDS_VSYNC_RST: "This field contains vertical total value
    // minus 1", so writing the standard's total into it costs a line. The cause
    // is a missing vertical FRONT porch term: active runs to the end of frame
    // and CEA's 4 front-porch lines become one extra line.
    CHECK(Mode1080p.activeLines() == 1080);
    CHECK(Mode1080p.frameLines() == 1125);   // 1080 + 4 front + 5 sync + 36 back
    CHECK(Mode720p.activeLines() == 720);
    CHECK(Mode720p.frameLines() == 750);     // 720 + 5 front + 5 sync + 20 back
}
