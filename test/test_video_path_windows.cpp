// Host-compiled unit tests for the registers one solve writes --
// `make -C test geometry-windows`.
//
// The net under doPostPresetLoadSteps()'s geometry writes: what makes them
// deletable is that the engine writes each of those fields afterwards
// regardless.
//
// **THE BENCH CANNOT SETTLE THAT.** The writes sit in eight groups behind
// videoStandardInput 1, 2, 3, 4, 8 or 9, presetIsPalForce60 or
// VPERIOD_IF == 523, and one source reaches exactly one group. So the question
// is asked here instead: poison every bank, seed only what the engine reads, run
// one solve, and ask the fake which registers it wrote.
// docs/chip-initialisation.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "CheckNear.h"
#include "MeasuredSource.h"
#include "SolvedEngine.h"

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Memory.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"

using namespace Tv5725;

// The picture each axis puts on the raster, as an outside observer reads it off
// the registers the solve wrote.
static long producedHorizontal()
{
    const long capture = Wire.field(1, 0x18, 0, 11) - Wire.field(1, 0x1A, 0, 11);
    return capture * Scale::Unity / Wire.field(3, 0x16, 0, 10);
}

static long producedVertical()
{
    const long capture = Wire.field(1, 0x1C, 0, 11) - Wire.field(1, 0x1E, 0, 11);
    return capture * Scale::Unity / Wire.field(3, 0x17, 4, 10);
}

// Every field doPostPresetLoadSteps() writes into that the engine also owns,
// with the bytes each one spans. Named so a failure says which.
struct Owned {
    const char *name;
    uint8_t seg, reg, offset, width, bytes;
};

static const Owned OwnedFields[] = {
    {"IF_HB_ST2",       1, 0x18, 0, 11, 2},
    {"IF_HB_SP2",       1, 0x1A, 0, 11, 2},
    {"IF_VB_ST",        1, 0x1C, 0, 11, 2},
    {"IF_VB_SP",        1, 0x1E, 0, 11, 2},
    {"VDS_VSCALE_BYPS", 3, 0x00, 5,  1, 1},
    {"VDS_DIS_VB_ST",   3, 0x13, 0, 11, 2},
    {"VDS_DIS_VB_SP",   3, 0x14, 4, 11, 2},
    {"VDS_VSCALE",      3, 0x17, 4, 10, 2},
    {"PB_CAP_OFFSET",   4, 0x37, 0, 10, 2},
};

static const size_t OwnedCount = sizeof(OwnedFields) / sizeof(OwnedFields[0]);

TEST_CASE("one solve writes every geometry register the sketch used to poke")
{
    SolvedEngine solved;

    // Asked of the fake rather than inferred from the value: a field whose
    // computed value happened to equal the poison would read correct having
    // never been written at all.
    for (size_t i = 0; i < OwnedCount; ++i) {
        const Owned &f = OwnedFields[i];
        CAPTURE(f.name);
        for (uint8_t b = 0; b < f.bytes; ++b) {
            CAPTURE(b);
            CHECK(Wire.touched[f.seg][static_cast<uint8_t>(f.reg + b)]);
        }
    }
}

TEST_CASE("no field is left holding what was there before the solve")
{
    SolvedEngine solved;

    // The half `touched` cannot see: five of these nine share a byte with
    // another field the engine writes, so the byte is touched whether or not the
    // field itself was and only the value distinguishes.
    for (size_t i = 0; i < OwnedCount; ++i) {
        const Owned &f = OwnedFields[i];
        CAPTURE(f.name);
        uint32_t stale = (static_cast<uint32_t>(Poison) * 0x0101u >> f.offset)
                         & ((1u << f.width) - 1u);
        CHECK(Wire.field(f.seg, f.reg, f.offset, f.width) != stale);
    }
}

TEST_CASE("vertical scaling is switched ON, whatever a preset load asked for")
{
    SolvedEngine solved;

    // doPostPresetLoadSteps() sets VDS_VSCALE_BYPS to 1 for a >650-line source
    // on videoStandardInput 9, and the engine clears it unconditionally because
    // it has computed an explicit scale -- so the two disagree and the engine
    // wins on ordering. Deleting the sketch's write changes nothing.
    CHECK(Wire.field(3, 0x00, 5, 1) == 0);
    CHECK(Wire.field(3, 0x00, 4, 1) == 0);  // VDS_HSCALE_BYPS, the same way
}

TEST_CASE("the capture window is a window, not a leftover")
{
    SolvedEngine solved;

    // Start beyond stop is the shape a half-written window takes, and it is what
    // the sketch's own arithmetic produced when it ran late: IF_VB_SP 8 with
    // IF_VB_ST 6, seen at gbs-control.ino's needPostAdjust. Reading each pair as
    // an ordered window is the cheapest assertion that catches it.
    CHECK(Wire.field(1, 0x1A, 0, 11) < Wire.field(1, 0x18, 0, 11));  // horizontalStop < horizontalStart
    CHECK(Wire.field(1, 0x1E, 0, 11) < Wire.field(1, 0x1C, 0, 11));  // verticalStop < verticalStart
    CHECK(Wire.field(3, 0x14, 4, 11) < Wire.field(3, 0x13, 0, 11));  // display V

    // And inside the raster it is placed on, which is the other half of being a
    // window. The doubler is in the path on this source, so the vertical capture
    // counts half-lines and wraps at twice the source frame.
    CHECK(Wire.field(1, 0x18, 0, 11) <= 1277);
    CHECK(Wire.field(1, 0x1C, 0, 11) <= 2 * 312u);
    CHECK(Wire.field(3, 0x13, 0, 11) <= 1126);
}

TEST_CASE("the playback stride covers the widest fetch, and holds still while zooming")
{
    SolvedEngine solved;

    // The stride is the per-line allocation and the fetch is what playback
    // reads into it, so a stride below the fetch overlaps lines. The fetch
    // follows the capture, which grows as the picture zooms OUT -- so a stride
    // sized for the framing on screen is short of the one the next press wants,
    // and it arrives as a green band down the right of the picture.
    // The bench line, COMPUTED rather than inherited: the stride follows the
    // wrap the engine solved, and the 1276 seeded in the fixture is what the
    // previous load left behind.
    CHECK(Wire.field(4, 0x37, 0, 10)
          == Memory::offsetFor(Wire.field(1, 0x0E, 0, 11) + 1));
    CHECK(Wire.field(1, 0x0E, 0, 11) != 1276);
    CHECK(Wire.field(4, 0x37, 0, 10) >= Wire.field(4, 0x39, 0, 10));

    SUBCASE("and the zoom that widens the capture does not outgrow it") {
        // Rewriting the stride re-lays the buffer out under a picture being
        // read from it, so it may not move with the framing -- it has to be
        // right for every framing of this line from the start. Sizing it from
        // the widest capture would not do: that subtracts the measured hsync
        // pulse, which moves by a unit between solves.
        const uint32_t stride = Wire.field(4, 0x37, 0, 10);

        solved.engine.zoom(-5000, 0);

        CHECK(Wire.field(4, 0x37, 0, 10) == stride);
        CHECK(Wire.field(4, 0x37, 0, 10) >= Wire.field(4, 0x39, 0, 10));
    }
}

TEST_CASE("the engine uses the divider it was GIVEN, not the one in the register")
{
    // The window between a write and PLLAD_LAT, reproduced. The register reports
    // a divider the ADC is not clocking at, so an engine that reads it back
    // solves the capture window for a line that is not arriving -- PLLAD_MD
    // reading 2210 against a PLL running 2553 is a solid green screen with every
    // register self-consistent.
    SolvedEngine solved;

    const uint32_t startBefore = Wire.field(1, 0x18, 0, 11);   // IF_HB_ST2
    const uint32_t stopBefore  = Wire.field(1, 0x1A, 0, 11);   // IF_HB_SP2

    // Straight into the bank, so this is the chip changing under the engine
    // rather than the engine being told anything.
    seed(5, 0x12, 0, 12, 1276);
    seed(1, 0x0E, 0, 11, 638);

    REQUIRE(solved.engine.resolve());

    // Not vacuous: test_video_path.cpp's checkBenchGeometry() pins both of these
    // to the values measured on the unit, so "unchanged" is anchored to a
    // number rather than to whatever the engine happened to leave.
    CHECK(Wire.field(1, 0x18, 0, 11) == startBefore);
    CHECK(Wire.field(1, 0x1A, 0, 11) == stopBefore);

}

TEST_CASE("a preset load computes the divider it uses")
{
    // There is nothing to inherit, so the divider is COMPUTED from the line
    // rate the source is running at. The registers are outputs of that, never
    // inputs to it.
    SolvedEngine solved;

    // 311 lines at 50 Hz, which is what the seeds above describe.
    g_fieldRate = 50.08f;
    solved.engine.setOutputMode(&Tv5725::Mode1080p);
    solved.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(solved.acquisition));

    // Against the rate the engine measured rather than a literal: the divider
    // is a function of that rate, so a literal here would assert the fake's
    // arithmetic instead of the engine's and would move whenever either did.
    const uint16_t wanted =
        SamplingClock::recommendedDivider(solved.sampling.lineRateHz(), 4, true);
    CHECK(wanted != 2553);   // or this test proves nothing about computing it

    CHECK(Wire.field(5, 0x12, 0, 12) == wanted);
    CHECK(Wire.field(1, 0x0E, 0, 11) == InputFormatter::lineCounterFor(wanted, true));
    CHECK(Wire.field(5, 0x4B, 0, 12) == SyncProcessor::retimeStopFor(wanted));

    SUBCASE("and the solve that follows uses it") {
        // The seeded IF_HSYNC_RST was 1276 for a 2553 divider. If the engine
        // were still reading rasters back it would mix the new divider with the
        // old wrap; it takes both from the same held value.
        REQUIRE(solved.engine.resolve());
        CHECK(Wire.field(1, 0x0E, 0, 11)
              == InputFormatter::lineCounterFor(
                     (uint16_t)Wire.field(5, 0x12, 0, 12), true));
    }
}

TEST_CASE("an unmeasurable source keeps the divider, rather than being given one")
{
    // getSourceFieldRate() reports 0 with no lock, and a divider computed from a
    // measurement that did not happen is the green screen -- so a pass that
    // measured nothing writes nothing, and what stays in force is the last
    // divider a real measurement chose. Leaving it there is also what lets the
    // state measure its way out: the count and the duty are both read through
    // whatever is latched.
    SolvedEngine solved;
    const uint16_t installed = (uint16_t)Wire.field(5, 0x12, 0, 12);

    g_fieldRate = 0.0f;
    solved.engine.setOutputMode(&Tv5725::Mode1080p);
    solved.engine.inputTimingsChanged(4);
    CHECK_FALSE(pollUntilSolved(solved.acquisition));
    CHECK(Wire.field(5, 0x12, 0, 12) == installed);

    SUBCASE("and a later refusal keeps the one that was solved") {
        g_fieldRate = 50.08f;
        solved.engine.setOutputMode(&Tv5725::Mode1080p);
        solved.engine.inputTimingsChanged(4);
        REQUIRE(pollUntilSolved(solved.acquisition));
        const uint16_t solvedDivider = (uint16_t)Wire.field(5, 0x12, 0, 12);

        g_fieldRate = 0.0f;
        solved.engine.setOutputMode(&Tv5725::Mode1080p);
        solved.engine.inputTimingsChanged(4);
        CHECK_FALSE(pollUntilSolved(solved.acquisition));
        CHECK(Wire.field(5, 0x12, 0, 12) == solvedDivider);
    }
}

// A source measurement that has not settled is not a mode, and the vertical
// axis is the one that can be fooled by it: the horizontal line comes from the
// held divider, while the vertical is entirely 2 x (STATUS_SYNC_PROC_VTOTAL + 1).
//
// Measured on the bench, sampled through a preset load: VTOTAL passes through
// 506, 251, 269, 259 and 511 before settling. Every one of those is inside the
// 200..1300 bounds the solve accepts, so a solve landing on one writes a
// vertical window sized for a frame the source is not sending -- and, having
// succeeded, never revisits it.
//
// What still refuses one is the 200..1300 bound and SourceMeasurement's
// steadiness run. **A transient count inside the bounds that holds still long
// enough is no longer caught**, because telling it from a real source at those
// timings needs an assumed field rate, and that assumption is what refused
// 640x480@75 outright. docs/firmware-geometry-engine.md
TEST_CASE("a vertical total outside what any source runs defers the solve")
{
    Wire.reset();
    poisonChip();

    seed(3, 0x01, 0, 12, 1914);   // VDS_HSYNC_RST
    seed(3, 0x02, 4, 11, 1125);   // VDS_VSYNC_RST
    seed(1, 0x0E, 0, 11, 1276);   // IF_HSYNC_RST
    seed(0, 0x19, 0, 12, 181);    // STATUS_SYNC_PROC_HLOW_LEN
    seed(5, 0x12, 0, 12, 2553);   // PLLAD_MD

    // The 97 a preset load leaves behind, below SourceVerticalTotalMin.
    seed(0, 0x1B, 0, 11, 97);     // STATUS_SYNC_PROC_VTOTAL, mid-load
    g_fieldRate = 50.08f;

    DisplayClock clock;

    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);
    VideoSourceAcquisition acquisition(sampling, engine);

    engine.setOutputMode(&Tv5725::Mode1080p);
    engine.inputTimingsChanged(4);
    CHECK_FALSE(pollUntilSolved(acquisition));

    // The window is parked at the reference, not solved for 97 lines: the
    // input formatter emits nothing to measure while its vertical blank lies
    // outside the frame, so leaving it alone would defer for ever.
    CHECK(Tv5725::InputFormatter::IF_VB_ST::read() == 0);
    CHECK(Tv5725::InputFormatter::IF_VB_SP::read() == 2);

    g_fieldRate = 50.08f;
}

// The framing is held as a proportion, but every bench instrument, the console
// line and docs/scaler-geometry-model.md speak input units. The engine holds the
// denominator, so it is the only thing that can convert -- and what it reports
// has to be the window it wrote, or the report is a second model of the picture.
TEST_CASE("the framing reported in units is the capture window on the chip")
{
    SolvedEngine solved;

    CHECK(solved.engine.extentUnitsOn(AxisHorizontal)
          == Wire.field(1, 0x18, 0, 11) - Wire.field(1, 0x1A, 0, 11));
    CHECK(solved.engine.extentUnitsOn(AxisVertical)
          == Wire.field(1, 0x1C, 0, 11) - Wire.field(1, 0x1E, 0, 11));

    SUBCASE("and it follows a press") {
        REQUIRE(solved.engine.zoom(400, 0));
        CHECK(solved.engine.extentUnitsOn(AxisHorizontal)
              == Wire.field(1, 0x18, 0, 11) - Wire.field(1, 0x1A, 0, 11));
    }
}

// One press step is one pixel of the output screen, so the engine sizes a press
// from the scale its own solve produced. Reading VDS_?SCALE back to size it
// instead makes a register an input to the calculation.
TEST_CASE("a press is asked for in output pixels, not input units")
{
    SolvedEngine solved;

    // As an outside observer reads it, from the register the solve wrote.
    const float magnification = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
    REQUIRE(magnification > 1.5f);   // or pixels and units are indistinguishable

    const int16_t wanted = AxisHorizontal.stepUnits(16, magnification);
    REQUIRE(wanted != 16);

    SUBCASE("zooming") {
        const long before = solved.engine.extentUnitsOn(AxisHorizontal);
        REQUIRE(solved.engine.zoom(16, 0));
        CHECK(before - solved.engine.extentUnitsOn(AxisHorizontal) == wanted);
    }

    SUBCASE("and panning, once a zoom has left room to pan into") {
        REQUIRE(solved.engine.zoom(400, 0));

        // Sized from the scale the zoom LEFT, not the one it started from: a
        // press is converted at the magnification in force when it is made.
        const float zoomed = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
        const int16_t step = AxisHorizontal.stepUnits(16, zoomed);
        const long before = solved.engine.originUnitsOn(AxisHorizontal);
        REQUIRE(solved.engine.pan(16, 0));
        CHECK(solved.engine.originUnitsOn(AxisHorizontal) - before == step);
    }
}

TEST_CASE("a press on one axis leaves the other where it was")
{
    // stepUnits() floors at one granule, so an axis the press did not name
    // drifts a unit per press unless a press of nothing is skipped outright.
    SolvedEngine solved;
    const long verticalExtent = solved.engine.extentUnitsOn(AxisVertical);

    REQUIRE(solved.engine.zoom(16, 0));
    CHECK(solved.engine.extentUnitsOn(AxisVertical) == verticalExtent);
}

// The output raster is one the engine SOLVED, so it is held rather than read
// back off VDS_?SYNC_RST. Reading it back makes a register an input to the
// calculation that produced it.
TEST_CASE("the solve uses the raster the engine holds, not the one on the chip")
{
    SolvedEngine solved;

    // Wiped after the solve that wrote them. A re-solve that reaches for these
    // sees no raster at all, reads it as bypass, and declines.
    const uint32_t scaleBefore = Wire.field(3, 0x17, 4, 10);  // VDS_VSCALE
    seed(3, 0x01, 0, 12, 0);   // VDS_HSYNC_RST
    seed(3, 0x02, 4, 11, 0);   // VDS_VSYNC_RST

    // A framing the engine has not solved before, so a solve that ran shows as
    // a moved SCALE and one that declined shows as no change at all. Neither
    // display edge is the witness: the near one is pinned to the output mode's
    // back porch and the far one to its front porch, and the fit fills the
    // region between them at every framing.
    solved.engine.zoom(0, 400);

    CHECK(Wire.field(3, 0x17, 4, 10) != scaleBefore);
}

TEST_CASE("the engine writes the scan mode its own measurement implies")
{
    // The four bits are the input formatter's, and the sketch has been choosing
    // them from rto->videoStandardInput and telling the engine afterwards. A
    // source that qualifies for scaling RGBHV is filed under standard 3, whose
    // branch bypasses the line doubler -- so a 15 kHz RGBHV source is captured
    // progressive and the divider that follows is right for a wrong premise.
    //
    // The engine measures the line rate. Nothing else has to be asked, and no
    // caller can disagree with it.
    SolvedEngine solved;   // 311 lines at 50.08 Hz, so 15.6 kHz

    CHECK(Wire.field(1, 0x0B, 4, 2) == 1);  // IF_HS_DEC_FACTOR
    CHECK(Wire.field(1, 0x0B, 7, 1) == 0);  // IF_LD_SEL_PROV
    CHECK(Wire.field(1, 0x0C, 0, 1) == 0);  // IF_LD_RAM_BYPS
    CHECK(Wire.field(1, 0x00, 6, 1) == 0);  // IF_PRGRSV_CNTRL

    // And the divider that follows from it.
    CHECK(Wire.field(5, Tv5725::Adc::PLLAD_MD::byteOffset,
                     Tv5725::Adc::PLLAD_MD::bitOffset,
                     Tv5725::Adc::PLLAD_MD::bitWidth) == 2200);
}

TEST_CASE("the engine realigns luma for the input the ADC was told to select")
{
    // Which connector is live is not a measurement -- nothing on the chip
    // reports it -- so it is held by the class that selected it, and the engine
    // asks that rather than reading a register or being handed a callback.
    Tv5725::Adc::selectInput(1);
    SolvedEngine solved;   // 311 lines, so line doubled

    CHECK(Wire.field(1, 0x02, 5, 2) == 3);  // IF_HS_Y_PDELAY
    CHECK(Wire.field(3, 0x24, 4, 2) == 2);  // VDS_Y_DELAY

    Tv5725::Adc::selectInput(0);
    solved.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(solved.acquisition));

    CHECK(Wire.field(1, 0x02, 5, 2) == 2);
    CHECK(Wire.field(3, 0x24, 4, 2) == 3);

    Tv5725::Adc::selectInput(1);
}

TEST_CASE("the engine realigns the 422/444 conversion with the scan mode")
{
    // Three blocks share the alignment and each has been taking its value from
    // the standard byte. The doubler is the fact behind all three, and the
    // engine is the only thing that measures it.
    SolvedEngine solved;   // 311 lines at 50.08 Hz, so line doubled

    CHECK(Wire.field(1, 0x02, 0, 1) == 0);  // IF_SEL_WEN
    CHECK(Wire.field(1, 0x02, 1, 1) == 1);  // IF_HS_SEL_LPF
    CHECK(Wire.field(3, 0x24, 2, 1) == 0);  // VDS_V_DELAY
    CHECK(Wire.field(2, 0x17, 0, 4) == 0);  // MADPT_Y_DELAY
}

// Nothing on the chip can measure where active video starts, so an unrecognised
// source is placed from an assumption. A source running a raster the standards
// state is placed from the standard instead.
// docs/investigations/vesa-modes-are-clipped-by-default.md
TEST_CASE("a VESA source is captured where its published raster puts picture")
{
    // 640x480@60: 96 of its 800 pixels on sync, and a 525-line frame the sync
    // processor counts from zero and reports as 524 -- which is what the bench
    // reads on a source running this mode.
    //
    // Seeded against the divider the engine solves for this source, because
    // that is the one in force when the layer that measures reads the pulse.
    // The register counts ADC samples, so on the part it scales with the
    // divider and the duty is the same either side; the fake holds whatever was
    // seeded. HsyncPulse.h
    // Into a raster with room for the whole published window. At 1080p this
    // mode's 80% of the line is wider than a 1600 raster can show at unity, and
    // what the capture does THERE is the bound's own case below.
    const uint16_t Divider = 2046;
    const uint16_t HsyncLow = (uint16_t)(Divider * 96 / 800);
    SolvedEngine solved(524, 59.94f, HsyncLow, &Tv5725::Mode720p, false);

    const long line = Wire.field(1, 0x0E, 0, 11) + 1;
    const long stop = Wire.field(1, 0x1A, 0, 11);
    const long start = Wire.field(1, 0x18, 0, 11);

    // 18.0% is where the published raster puts picture in ITS line, counted
    // from the hsync leading edge. This mode's pulse is inverted, so the line
    // is counted from the trailing edge with the pulse already behind it.
    const long sync = (long)std::ceil((double)line * (double)HsyncLow / (double)Divider);

    CHECK_NEAR(stop, 0.180 * line - sync, 2);
    CHECK_NEAR(start - stop, 0.800 * line, 2);
}


// The part cannot minify: VDS_?SCALE divides 1024 and tops out at Scale::Max,
// so a capture bigger than the room produces a picture bigger than the room and
// its far end is never drawn. Nothing in a register dump says so -- the scale is
// simply clamped -- and the control reads as dead in both directions, because
// zoom-out is at its bound and zoom-in only trims what was already off-screen.
TEST_CASE("a capture the output cannot show is bounded, not cropped")
{
    // The bench source into a 480p raster: the line doubler makes the vertical
    // axis count 622 half-lines against a frame with room for about 515.
    SolvedEngine solved(311, 50.08f, 181, &Mode480p);

    const long capture = Wire.field(1, 0x1C, 0, 11) - Wire.field(1, 0x1E, 0, 11);
    const long window = Wire.field(3, 0x13, 0, 11) - Wire.field(3, 0x14, 4, 11);
    const long produced = capture * Scale::Unity / Wire.field(3, 0x17, 4, 10);

    // The aperture is inset one capture unit at each end, so it is that much
    // narrower than the picture produced.
    const long reach = 1 + Scale::Unity / Wire.field(3, 0x17, 4, 10);

    REQUIRE(window > 0);
    CHECK(produced <= window + 3 + 2 * reach);

    SUBCASE("and the control has somewhere to go in both directions") {
        const long before = solved.engine.extentUnitsOn(AxisVertical);
        REQUIRE(solved.engine.zoom(0, 16));
        CHECK(solved.engine.extentUnitsOn(AxisVertical) < before);
        REQUIRE(solved.engine.zoom(0, -16));
        CHECK(solved.engine.extentUnitsOn(AxisVertical) > 0);
    }
}


// The picture breaks up when the capture outgrows the raster, and the framing
// can ask for that: VDS_HSCALE divides 1024 and is ten bits, so the scaler
// magnifies and CANNOT MINIFY. Past unity fitToRaster() pins the scale at
// Scale::Max and recomputes produced from it, so every further unit of capture
// is a unit of picture with nowhere to go. Measured on the bench at four
// rasters: a capture of 1827 is clean at 2400 and shredded at 1600, source and
// divider unchanged.
// docs/investigations/the-capture-may-not-outgrow-the-raster.md
TEST_CASE("zooming out stops where the raster stops, on the horizontal axis too")
{
    SolvedEngine solved(523, 60.0f, 181, &Mode1080p);

    // Far enough to reach the bound from any starting framing, one press at a
    // time: the clamp has to hold against a control that keeps asking.
    for (int press = 0; press < 40; ++press)
        solved.engine.zoom(-40, 0);

    const long capture = Wire.field(1, 0x18, 0, 11) - Wire.field(1, 0x1A, 0, 11);
    const long window = Wire.field(3, 0x10, 0, 12) - Wire.field(3, 0x11, 4, 12);
    const long produced = capture * Scale::Unity / Wire.field(3, 0x16, 0, 10);

    // The aperture is inset one capture unit at each end, so it is that much
    // narrower than the picture produced.
    const long reach = 1 + Scale::Unity / Wire.field(3, 0x16, 0, 10);

    REQUIRE(window > 0);
    CHECK(produced <= window + 3 + 2 * reach);

    SUBCASE("and the control has somewhere to go back to") {
        const long before = solved.engine.extentUnitsOn(AxisHorizontal);
        REQUIRE(solved.engine.zoom(40, 0));
        CHECK(solved.engine.extentUnitsOn(AxisHorizontal) < before);
    }
}


// The magnification runs out before the capture does. Once VDS_?SCALE is at
// Scale::Min the picture cannot grow with the crop any more, so every
// further press of zoom-in SHRINKS it and the solve re-centres what is left.
// Measured holding the key on the bench: the scale pinned at 342 while the
// capture fell 574 -> 16 units and the window marched to the corner of the
// source, leaving a 48 px patch on a black screen.
TEST_CASE("zooming in stops where the magnification stops, not where the capture does")
{
    SolvedEngine solved;

    const long filledH = producedHorizontal();
    const long filledV = producedVertical();
    REQUIRE(filledH > 0);
    REQUIRE(filledV > 0);

    // Far enough to reach the stop from any starting framing, one press at a
    // time: the bound has to hold against a control that keeps asking.
    for (int press = 0; press < 60; ++press)
        solved.engine.zoom(40, 40);

    SUBCASE("the picture is still the size it was") {
        CHECK(producedHorizontal() >= filledH - 2);
        CHECK(producedVertical() >= filledV - 2);
    }

    SUBCASE("and the control has somewhere to go back to") {
        const long before = solved.engine.extentUnitsOn(AxisHorizontal);
        REQUIRE(solved.engine.zoom(-40, 0));
        CHECK(solved.engine.extentUnitsOn(AxisHorizontal) > before);
    }
}


// Neither stop is visible in VDS_?SCALE, so the press itself has to report it.
// Measured on the bench: zoom-in stops with VDS_HSCALE 343 and VDS_VSCALE 342 --
// one either side of Scale::Min, because the scale is lrintf(Unity x capture /
// room) and the rounding is the mode's -- and zoom-out stops at 579 and 554,
// nowhere near Scale::Max, because the capture reaches the end of the line long
// before the scaler is asked to minify. The OSD draws its "limit" off this.
TEST_CASE("a press that moves nothing says so")
{
    SolvedEngine solved;

    CHECK(solved.engine.zoom(40, 40));

    SUBCASE("at the zoom-in stop") {
        for (int press = 0; press < 60; ++press)
            solved.engine.zoom(40, 40);
        CHECK_FALSE(solved.engine.zoom(40, 40));
    }

    SUBCASE("and at the zoom-out stop") {
        for (int press = 0; press < 60; ++press)
            solved.engine.zoom(-40, -40);
        CHECK_FALSE(solved.engine.zoom(-40, -40));
    }
}


// The divider is chosen against the OUTPUT, not only against the ADC's rating.
// A line the raster cannot show costs ADC clock and reaches the screen cropped:
// 640x480@75 into a 1080p raster solved to a capture of 1448 against a window of
// 1176 and arrived corrupt with nothing touched.
TEST_CASE("the divider is bounded by the line the output raster can show")
{
    SolvedEngine solved(500, 75.0f, 181, &Mode1080p, false);

    const uint16_t raster = (uint16_t)(Wire.field(3, 0x01, 0, 12) + 1);
    const uint16_t showable = AxisHorizontal.maximumCapture(raster, 0);

    REQUIRE(showable > 0);
    CHECK(solved.engine.capturableOn(AxisHorizontal) <= showable);

    SUBCASE("so zooming all the way out never asks the scaler to minify") {
        for (int press = 0; press < 40; ++press)
            solved.engine.zoom(-40, 0);
        // Scale::Max is 1024/1023, a magnification of 1.001 -- the unity clamp
        // rather than a minification, so landing ON it is the bound holding.
        CHECK(Wire.field(3, 0x16, 0, 10) <= Scale::Max);
    }
}


// The bench rule the framing override exists to check: whatever the output
// resolution and whatever the source, a 100% framing captures the whole
// capturable region -- blanking and border included -- and the scaler is never
// asked to minify to show it.
TEST_CASE("a forced full framing captures everything the source offers")
{
    SolvedEngine solved(523, 60.0f, 181, &Mode1080p, false);

    // Framed somewhere else first, so the override has something to override.
    REQUIRE(solved.engine.zoom(80, 40));
    REQUIRE(solved.engine.extentUnitsOn(AxisHorizontal)
            < solved.engine.capturableOn(AxisHorizontal));

    solved.engine.forceFullFraming(true);
    REQUIRE(resolveUntilSolved(solved.acquisition));

    CHECK(solved.engine.extentUnitsOn(AxisHorizontal)
          == solved.engine.capturableOn(AxisHorizontal));
    CHECK(solved.engine.extentUnitsOn(AxisVertical)
          == solved.engine.capturableOn(AxisVertical));

    SUBCASE("and the scaler still magnifies rather than clamping at unity") {
        CHECK(Wire.field(3, 0x16, 0, 10) <= Scale::Max);
    }

    SUBCASE("a press cannot move it while it is forced") {
        const uint16_t before = solved.engine.extentUnitsOn(AxisHorizontal);
        solved.engine.zoom(80, 0);
        CHECK(solved.engine.extentUnitsOn(AxisHorizontal) == before);
    }

    SUBCASE("and releasing it gives the framing back") {
        solved.engine.forceFullFraming(false);
        REQUIRE(solved.engine.zoom(80, 0));
        CHECK(solved.engine.extentUnitsOn(AxisHorizontal)
              < solved.engine.capturableOn(AxisHorizontal));
    }
}


// The clamp is a window in ADC samples, so it moves with the divider: the same
// fraction of a 1900-sample line and of a 1566-sample one are 112 and 92. Left
// where a previous divider put it, it reaches past the back porch and takes the
// black level off picture -- measured on the bench, a stop 18 samples late
// costs 20 grey levels and one 78 late costs 76.
TEST_CASE("the clamp window follows the divider the solve applied")
{
    SolvedEngine solved(523, 60.0f, 181, &Mode1080p, false);

    const uint16_t divider = (uint16_t)Wire.field(5, 0x12, 0, 12);
    REQUIRE(divider > 0);

    // Separate sync, RGB: the line the solve just applied is the line the sync
    // processor counts, so no measurement is needed to place the window.
    CHECK(Wire.field(5, 0x41, 0, 12)
          == SyncProcessor::clampStartFor(divider, false, false));
    CHECK(Wire.field(5, 0x43, 0, 12) == SyncProcessor::clampStopFor(divider, false, false));
}


// A divider held as an ENGINE INPUT, so every register downstream of it is
// solved rather than left from the last solve. Writing the sampling group by
// hand and comparing pictures measures the mismatch against the previous
// solve's window instead of the divider, which is what produced a count limit
// at 2005 that does not exist.
// docs/investigations/a-hand-set-divider-cannot-be-judged-against-a-solved-window.md
TEST_CASE("a held divider is what the whole solve runs off")
{
    SolvedEngine solved;
    g_fieldRate = 50.08f;
    solved.engine.setOutputMode(&Tv5725::Mode1080p);
    solved.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(solved.acquisition));

    const uint16_t recommended = (uint16_t)Wire.field(5, 0x12, 0, 12);
    const uint16_t held = 2000;
    REQUIRE(held != recommended);

    solved.engine.holdDivider(held);
    REQUIRE(solved.engine.resolve());

    // One quantity in three registers, so all three have to follow it.
    CHECK(Wire.field(5, 0x12, 0, 12) == held);
    CHECK(Wire.field(1, 0x0E, 0, 11) == InputFormatter::lineCounterFor(held, true));
    CHECK(Wire.field(5, 0x4B, 0, 12) == SyncProcessor::retimeStopFor(held));

    SUBCASE("and the capture window is solved inside the line it describes") {
        CHECK(Wire.field(1, 0x18, 0, 11)
              <= InputFormatter::lineCounterFor(held, true));
    }

    SUBCASE("and releasing it returns the engine to its own recommendation") {
        solved.engine.holdDivider(0);
        REQUIRE(solved.engine.resolve());
        CHECK(Wire.field(5, 0x12, 0, 12) == recommended);
    }
}


// The rung the ladder was missing. The sync processor counts in ADC clocks, so
// an unlocked ADC PLL leaves every count at zero and the engine with nothing to
// measure -- and the other rungs all write sync-processor registers, which
// cannot reach it. docs/investigations/the-ladder-never-restarts-the-adc-pll.md
TEST_CASE("the sampling clock can be restarted without re-solving anything")
{
    SolvedEngine solved;
    g_fieldRate = 50.08f;
    solved.engine.setOutputMode(&Tv5725::Mode1080p);
    solved.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(solved.acquisition));

    const uint16_t divider = (uint16_t)Wire.field(5, 0x12, 0, 12);
    REQUIRE(divider != 0);

    // As a PLL held in reset reads, which is the state this exists to leave.
    seed(5, 0x11, Tv5725::Adc::PLLAD_VCORST::bitOffset, 1, 1);

    solved.engine.restartSamplingClock();

    CHECK(Tv5725::Adc::PLLAD_VCORST::read() == 0);
    CHECK(Wire.field(5, 0x12, 0, 12) == divider);
}
