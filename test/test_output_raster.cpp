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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/DisplayClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"

using namespace Tv5725;

TEST_CASE("htotal is the frame clock budget over the frame height, floored")
{
    // A raster needs horizontalTotal x frameLines x fieldRate hertz, so horizontalTotal floors:
    // rounding up asks the part for a clock above the target.
    CHECK(OutputMode::horizontalTotalFor(108000000u, 1126, 50.0f) == 1918);
    CHECK(OutputMode::horizontalTotalFor(108000000u, 1126, 60.0f) == 1598);
    CHECK(OutputMode::horizontalTotalFor(81000000u, 1126, 50.0f) == 1438);
    CHECK(OutputMode::horizontalTotalFor(129600000u, 1126, 50.0f) == 2301);
    CHECK(OutputMode::horizontalTotalFor(162000000u, 1126, 50.0f) == 2877);

    SUBCASE("and a raster that will not fit its register is refused") {
        // VDS_HSYNC_RST is twelve bits. Wrapping would roll the picture.
        CHECK(OutputMode::horizontalTotalFor(162000000u, 262, 50.0f) == 0);
        CHECK(OutputMode::horizontalTotalFor(0u, 1126, 50.0f) == 0);
        CHECK(OutputMode::horizontalTotalFor(108000000u, 0, 50.0f) == 0);
        CHECK(OutputMode::horizontalTotalFor(108000000u, 1126, 0.0f) == 0);
    }
}

TEST_CASE("the divider is the largest seed at or under the ceiling")
{
    // The seed only has to put the Si5351 in range -- the rate is steered to
    // whatever the raster demands afterwards -- so take the most clock available.
    CHECK(OutputMode::clockDividerFor(1126, 50.0f, 129600000u) == 0x95);
    CHECK(OutputMode::clockDividerFor(1126, 50.0f, 108000000u) == 0x85);
    // 162 MHz is not reachable however high the ceiling: a 1126-line frame at
    // that clock wants a 2877-pixel line, past what the part can produce.
    CHECK(OutputMode::clockDividerFor(1126, 50.0f, 162000000u) == 0x95);

    SUBCASE("and it skips seeds whose htotal would overflow the register") {
        // A short frame at a high clock cannot be expressed, so the seed below it
        // is the answer rather than a wrapped raster.
        CHECK(OutputMode::clockDividerFor(262, 50.0f, 162000000u) != 0xA5);
    }

    SUBCASE("an unmeasurable field rate yields nothing, not a guess") {
        CHECK(OutputMode::clockDividerFor(1126, 0.0f, 129600000u) == 0);
    }
}

TEST_CASE("the divider keeps the line inside what the scaler can produce")
{
    // The part wraps the line past about 2240 produced pixels, and the raster
    // bound follows from that and the active fraction. A short frame at the
    // engine ceiling asks for far more -- 750 lines at 50 Hz wants 2880 -- so
    // the seed below is the answer. docs/investigations/720p-edge-corruption.md
    const uint16_t frames[] = { 625, 750, 1000, 1066, 1125 };
    for (uint8_t i = 0; i < 5; ++i) {
        uint8_t seed = OutputMode::clockDividerFor(frames[i], 50.0f,
                                                   OutputMode::EngineCeilingHz);
        CHECK(seed != 0);
        CHECK(OutputMode::horizontalTotalFor(DisplayClock::hzFor(seed), frames[i], 50.0f)
              <= OutputMode::MaxHorizontalTotal);
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
    OutputTimings at108 = Mode1080p.solve(50.0f, 108000000u);
    CHECK(at108.horizontalTotal == 1920);
    CHECK(at108.hsyncStart == 0);
    CHECK(at108.hsyncStop == 32);
    CHECK(at108.activeStart == 32 + 108);

    OutputTimings at1296 = Mode1080p.solve(50.0f, 129600000u);
    CHECK(at1296.horizontalTotal == 2304);
    CHECK(at1296.hsyncStop == 38);

    OutputTimings at648 = Mode1080p.solve(50.0f, 64800000u);
    CHECK(at648.horizontalTotal == 1152);

    SUBCASE("all three are the same 296 ns, which is the point") {
        // If they were a fixed pixel count instead, this would fail at two of the
        // three clocks.
        CHECK_MESSAGE(at108.hsyncStop * 1000000000.0 / at108.demandedHz() > 280.0,
                      "108 MHz pulse too short");
        CHECK_MESSAGE(at1296.hsyncStop * 1000000000.0 / at1296.demandedHz() < 320.0,
                      "129.6 MHz pulse too long");
        CHECK_MESSAGE(at648.hsyncStop * 1000000000.0 / at648.demandedHz() > 280.0,
                      "64.8 MHz pulse too short");
    }
}

// The far end of the line reserves what the BOARD needs, which is a property of
// this part and not of any standard. CEA's own minimum front porch is an order of
// magnitude above it -- 64 px at 108 MHz, 77 at 129.6 -- and the difference is
// picture, because the reserve bounds where the produced picture may end.
//
// The encoder generates its own HDMI blanking from what it samples and never sees
// ours, so conforming to CEA at this end buys nothing.
// docs/scaler-geometry-model.md "The output front porch"
TEST_CASE("the far-end reserve is the board's floor, not the standard's porch")
{
    OutputTimings at108 = Mode1080p.solve(50.0f, 108000000u);
    CHECK(at108.horizontalTotal == 1920);
    CHECK(at108.activeStop == 1920 - 16);

    OutputTimings at1296 = Mode1080p.solve(50.0f, 129600000u);
    CHECK(at1296.horizontalTotal == 2304);
    CHECK(at1296.activeStop == 2304 - 16);

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
    CHECK(OutputMode::WorkingCeilingHz == 129600000u);

    OutputTimings best = Mode1080p.solve(50.0f);
    CHECK(best.horizontalTotal == 2304);
    CHECK(best.verticalTotal == 1125);
    CHECK(best.divider == 0x95);
    CHECK(best.demandedHz() <= OutputMode::WorkingCeilingHz);
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
    // It sits BELOW WorkingCeilingHz: the part runs 129.6 MHz clean and sharp,
    // but the wider raster it buys puts the pipeline's run-up on picture at
    // 1080p, and no framing avoids it because the run-up cannot be blanked
    // without clipping image nor moved without costing the right edge.
    // docs/investigations/display-window-opens-early.md
    CHECK(OutputMode::EngineCeilingHz < OutputMode::WorkingCeilingHz);

    OutputTimings solved = Mode1080p.solve(50.0f, OutputMode::EngineCeilingHz);
    CHECK(solved.horizontalTotal == 1920);
    CHECK(solved.verticalTotal == 1125);
    CHECK(solved.demandedHz() <= OutputMode::EngineCeilingHz);

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
    CHECK(OutputMode::forFrameHeight(1125) == &Mode1080p);
    CHECK(OutputMode::forFrameHeight(1066) == &Mode1024p);
    CHECK(OutputMode::forFrameHeight(1000) == &Mode960p);
    CHECK(OutputMode::forFrameHeight(750) == &Mode720p);
    CHECK(OutputMode::forFrameHeight(625) == &Mode576p);
    CHECK(OutputMode::forFrameHeight(525) == &Mode480p);

    SUBCASE("and an unknown height selects nothing") {
        // NOT a fallback to 1080p: a height nobody has swept keeps whatever
        // raster it had, the same rule DisplayClock follows. 264 and 314 are the
        // dropped downscale tables'; 1126 and 751 are the one-line-long totals
        // every shipped table carried, and a build still producing those should
        // find no mode rather than the nearest one.
        CHECK_FALSE(OutputMode::forFrameHeight(264));
        CHECK_FALSE(OutputMode::forFrameHeight(314));
        CHECK_FALSE(OutputMode::forFrameHeight(1126));
        CHECK_FALSE(OutputMode::forFrameHeight(751));
        CHECK_FALSE(OutputMode::forFrameHeight(0));
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
            OutputTimings solved = Mode1080p.solve(rates[r], ceilings[c]);
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
                            (unsigned)OutputMode::horizontalTotalFor(
                                ceilings[c], heights[h], rates[r]));
            }
        }
    }
}

TEST_CASE("the output mode comes from the user's preference, not from the chip")
{
    // The mode arrives explicitly rather than being read back from
    // GBS::VDS_VSYNC_RST, whose only writer is the preset table it replaces.
    CHECK((OutputMode::forPreference(Output1080P) == &Mode1080p));
    CHECK((OutputMode::forPreference(Output1024P) == &Mode1024p));
    CHECK((OutputMode::forPreference(Output960P) == &Mode960p));
    CHECK((OutputMode::forPreference(Output720P) == &Mode720p));
}

TEST_CASE("480p and 576p are separate preferences, neither of them a rate")
{
    // Output480P used to mean 480 active lines at 60 Hz and 576 at 50, so on a
    // 50 Hz source there was no way to ask for 480p at all: the preference was
    // itself the switch. They are two preferences now, and choosing between
    // them by field rate is Tv5725::OutputChoice's -- the same shape as 960
    // against 1024, and as escapable.
    CHECK((OutputMode::forPreference(Output480P) == &Mode480p));
    CHECK((OutputMode::forPreference(Output576P) == &Mode576p));

    CHECK(Mode480p.activeLines() == 480);
    CHECK(Mode480p.frameLines() == 525);    // 480 + 9 front + 6 sync + 30 back
    CHECK(Mode576p.activeLines() == 576);
    CHECK(Mode576p.frameLines() == 625);    // 576 + 5 front + 5 sync + 39 back
}

TEST_CASE("bypass has no raster to solve, and says so twice")
{
    // The hazard: OutputMode is a raster-geometry value, so solving one for a
    // mode with no raster would write zeros with every register self-consistent.
    // isBypass() is what callers ask; solve() failing usable() is what catches
    // a caller that did not.
    CHECK(ModeBypass.isBypass());
    CHECK(ModeBypass.activeLines() == 0);
    CHECK_FALSE(ModeBypass.solve(50.0f).usable());
    CHECK_FALSE(ModeBypass.solve(59.94f).usable());
}

TEST_CASE("forFrameHeight never answers bypass")
{
    // It resolves a raster that is ON the chip, and bypass has none. Answering
    // ModeBypass for a frame height of zero would make a chip with no raster
    // indistinguishable from one deliberately in bypass.
    CHECK((OutputMode::forFrameHeight(0) == 0));
}

TEST_CASE("a preference that is not a resolution resolves to no mode")
{
    // 0 leaves the caller to fall back rather than silently solving the wrong
    // raster. 6 is cast rather than named because the enumerator no longer
    // exists: OutputDownscale went with the preset tables. Bypass is NOT one of
    // these -- it names a mode, and having its own is the point.
    CHECK((OutputMode::forPreference((PresetPreference)6) == 0));
    CHECK((OutputMode::forPreference(OutputBypass) == &ModeBypass));
}

TEST_CASE("a custom preset resolves to no mode, because its bytes are the mode")
{
    // OutputCustomized is not a resolution -- it means "load the one I saved",
    // and that file's own frame height is the answer. The caller reads it back,
    // which is the last place that inherits on purpose; it goes when a saved
    // slot records the inputs to the calculation instead of a register dump.
    CHECK((OutputMode::forPreference(OutputCustomized) == 0));
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
