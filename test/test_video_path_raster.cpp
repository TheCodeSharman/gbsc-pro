// Host-compiled tests for what the engine does while the source is unsettled --
// `make -C test geometry-raster`.
//
// getSourceFieldRate() is the sketch's, and this file supplies it instead, so
// the test DRIVES the source measurement -- the input the whole solve turns on,
// and the one thing a bench test cannot hold still.
//
// What it pins is the difference between waiting and giving up. Getting that
// wrong does not look like a bug: the picture is fine, because the preset
// table's raster is still there. It just means the engine never ran.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Si5351Stubs.h"
#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SamplingClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"
#include "../GBSC-Pro-Source code/gbs-control/src/clock/ClockGen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoPath.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/OutputMode.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"

#include "RegistersWritten.h"
#include "MeasuredSource.h"
#include "DebugPinStub.h"

using namespace Tv5725;


// The source field rate the engine will measure. The sketch defines this for
// real; here it is the test's to set, which is the point.
static float g_fieldRate = 50.08f;
uint32_t debugPinPulseTicks() { return ticksForHz(g_fieldRate); }
void tv5725Log(const char *) {}

// STATUS_SYNC_PROC_VTOTAL, s0_1B[10:0] -- the source's line count.
static void setSourceLines(uint16_t lines)
{
    Wire.bank[0][0x1B] = lines & 0xFF;
    Wire.bank[0][0x1C] = (Wire.bank[0][0x1C] & 0xF8) | ((lines >> 8) & 0x07);
}

// VDS_HSYNC_RST, s3_01[11:0] -- the horizontal total the engine writes.
static uint16_t horizontalTotalWritten() { return Wire.field(3, 0x01, 0, 12) + 1; }

// Neither a preset table's value nor anything the engine writes.
static const uint8_t Poison = 0xC2;

// Poisoned, HPERIOD_IF reads a value that implies a plausible line rate, and
// measureLineRate() prefers it to the injected field rate. A case that wants the
// rate it injects has to say nothing was measured.
static void poisonChip()
{
    Wire.poison(Poison);
    Wire.bank[0][0x06] = 0;                       // HPERIOD_IF low eight
    Wire.bank[0][0x07] &= 0xFE;                   // and its ninth bit

    // A bus that answers, and a quiet interrupt byte: the poison sets every
    // latched bit, including the one that arms a re-measure on every pass.
    Tv5725::Chip::holdPower(true);
    Wire.bank[0][0x0F] = 0;
}

static uint32_t horizontalTotalUnwritten()
{
    return (Poison | (Poison << 8)) & 0x0FFF;
}

// One pass of the whole acquisition path. The engine no longer drives itself:
// its stages are named calls the layer makes in order, with the measurement of
// the source taken between them, so a case that wants a solve drives the layer.
//
// The clock only has to increase. Which pass is a detection pass is the
// cadence's business and no case here is about that.
static uint32_t g_nowMs = 0;
static bool pollOnce(VideoSourceAcquisition &acquisition)
{
    g_nowMs += VideoSourceAcquisition::DetectionIntervalMs;
    return acquisition.poll(g_nowMs);
}

// poll() gates on a line count steady over several passes before it will pay for
// a field rate measurement, so a solve takes more than one call.
static bool pollUntilSolved(VideoSourceAcquisition &acquisition)
{
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i)
        if (pollOnce(acquisition))
            return true;
    return false;
}

struct SettledEngine {
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine;
    VideoSourceAcquisition acquisition;

    SettledEngine()
        : engine(clock, sampling, framings), acquisition(sampling, engine)
    {
        Wire.reset();
        poisonChip();
        Wire.lockSyncProcessor();
        // The chip's route outlives an instance the way it outlives a reset.
        Tv5725::VideoRoute::toScaler();
        // The bench source's pulse, modelled so the count follows the divider
        // the engine measures through. Poisoned, HLOW_LEN is not a duty and
        // the engine waits rather than solving from it.
        Wire.sourceHsync(181, 2553, true);
        g_fieldRate = 50.08f;
        setSourceLines(311);  // the bench RiscPC, settled: PAL-like
    }
};

TEST_CASE("a settled source gets the computed raster, not the table's")
{
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    CHECK(horizontalTotalWritten() == 1916);

    // The twelve tables ship 1445 (PAL) and 1602 (NTSC) at this frame height.
    // Landing on either would mean the table won, which is the whole failure
    // this file exists to catch.
    CHECK(horizontalTotalWritten() != 1445);
    CHECK(horizontalTotalWritten() != 1602);
}

TEST_CASE("an unsettled line count is waited out, not solved against")
{
    SettledEngine settled;

    // 97 is the documented mid-preset-load reading -- see CaptureWindow's
    // comment and CLAUDE.md -- and it is perfectly steady, so steadiness alone
    // would call it settled.
    setSourceLines(97);
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    CHECK_FALSE(pollUntilSolved(settled.acquisition));

    // A half-written raster is worse than none: the totals go in before the
    // sync positions, so bailing between them would leave the two disagreeing.
    CHECK(Wire.field(3, 0x01, 0, 12) == horizontalTotalUnwritten());

    SUBCASE("and the poll after it settles lands the whole raster") {
        // Giving up instead leaves the previous raster standing for the
        // session, invisibly -- the picture is fine and FrameSync steers the
        // Si5351 to whatever raster it finds. Measured 2026-08-15: stuck at the
        // table's 1445 x 1126 where the engine wants its own, with the Si5351
        // on 81.48 MHz to match the wrong one.
        setSourceLines(311);
        REQUIRE(pollUntilSolved(settled.acquisition));
        CHECK(horizontalTotalWritten() == 1916);

        // VDS_VSYN_SIZE1/2 are the vertical totals the frame-rate selector picks
        // between, and VDS_FR_SELECT never alternates, so both are the frame.
        // One quantity in three registers has one owner, so they arrive with the
        // raster rather than from whatever ran at load time.
        uint32_t verticalTotal = Wire.field(3, 0x02, 4, 11) + 1;
        CHECK(Wire.field(3, 0x20, 0, 11) == verticalTotal + 1);
        CHECK(Wire.field(3, 0x22, 0, 11) == verticalTotal + 1);
    }
}

TEST_CASE("a field rate that moves without the line count is waited out too")
{
    SettledEngine settled;

    g_fieldRate = 50.08f;
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    REQUIRE(horizontalTotalWritten() == 1916);

    // The same 311-line source now reading 60 Hz is the transient this guard
    // exists for. A raster solved at the wrong rate is out by the ratio of the
    // rates, so the previous answer has to stand.
    g_fieldRate = 60.0f;
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    CHECK_FALSE(pollUntilSolved(settled.acquisition));
    CHECK(horizontalTotalWritten() == 1916);

    SUBCASE("and the poll after the rate returns lands it") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(settled.acquisition));
        CHECK(horizontalTotalWritten() == 1916);
    }
}

TEST_CASE("entering bypass drops the outstanding solve")
{
    SettledEngine settled;

    // Outstanding from the previous mode, which is the common state now that an
    // unsettled source waits rather than giving up.
    setSourceLines(97);
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE_FALSE(pollUntilSolved(settled.acquisition));

    // Past 535 lines the unit drops to RGBHV bypass, where video routes around
    // the VDS and there is no raster to solve. bypassModeSwitch_RGBHV() returns
    // before doPostPresetLoadSteps(), so nothing else clears the mode change,
    // and a solve landing afterwards writes a scaled raster and a recomputed
    // divider straight over the bypass setup.
    settled.engine.setOutputMode(&ModeBypass);
    Tv5725::VideoRoute::toHdBypassChannel();
    Wire.reset();
    poisonChip();
    setSourceLines(311);

    CHECK_FALSE(pollUntilSolved(settled.acquisition));
    CHECK(registersWritten() == 0);
}

TEST_CASE("an unmeasurable line rate is retried, not settled for")
{
    SettledEngine settled;

    // A cold boot reads `271 lines x 49.22 Hz -> line rate 0` 3.6 s in, so
    // lineRateFrom() refuses -- and a pass that measured nothing writes
    // nothing, so 1856 stands until one can. The retry is the point: nothing
    // solves a raster from a rate that was never measured.
    Adc::applyDivider(1856);    // as the bypass path leaves it, in force
    g_fieldRate = 0.0f;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE_FALSE(pollUntilSolved(settled.acquisition));
    CHECK(Wire.field(5, 0x12, 0, 12) == 1856);

    SUBCASE("and the poll that can measure it computes one") {
        g_fieldRate = 50.08f;
        REQUIRE(pollUntilSolved(settled.acquisition));

        // 311 lines at 50.08 Hz is 15574 Hz. 2508 samples on that line is
        // 39.1 MHz, inside the crossover row that installs 4x oversampling,
        // and the divider sits hard against that row's ceiling.
        CHECK(Wire.field(5, 0x12, 0, 12) == 2200);
        CHECK(Wire.field(1, 0x0E, 0, 11) == 1100);   // IF_HSYNC_RST, divider / 2
        CHECK(Wire.field(5, 0x4B, 0, 12) == 2046);   // SP_RT_HS_SP, 93% of it
    }
}

TEST_CASE("the raster follows the key, not the reading behind it")
{
    // Measured on the bench across four mode changes of one unchanged 800x600
    // source: it settles at 60.38 Hz after one and 60.72 after the next, and
    // the raster moves 11 px with it. The source did not move, the reading did.
    // The frame time lock closes on frame TIME continuously, so a raster in the
    // right ballpark is steered exact -- one that jumps between solves is not.
    SettledEngine settled;
    setSourceLines(627);
    g_fieldRate = 60.38f;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    const uint16_t first = horizontalTotalWritten();

    g_fieldRate = 60.72f;
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    CHECK(horizontalTotalWritten() == first);

    // And it is this source's raster, not the one the engine came in holding.
    // Asserting only that two solves agree is passed by two stale values.
    CHECK(first != 1920);
    CHECK(first == 1590);
}

TEST_CASE("a solve points the part at the clock source that can serve the raster")
{
    // s0_41 is a source selection. The seed names a target FREQUENCY; writing it
    // to the register puts the display on the internal PLL, which frame time
    // lock cannot steer, so the output drifts against the source while every
    // register reads correct. docs/tv5725-chip.md
    SettledEngine settled;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    settled.clock.driveWith(generator);

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    CHECK(Wire.bank[0][0x41] == DisplayClock::ExternalPclkIn);

    SUBCASE("and the seed it holds is still the frequency to steer to") {
        CHECK(settled.clock.hz() == OutputMode::EngineCeilingHz);
    }
}

TEST_CASE("a board with no generator gets the seed's own internal divider")
{
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    CHECK(Wire.bank[0][0x41] == 0x85);
}

// VDS_VSYNC_RST, s3_02[14:4] -- the output frame the engine writes.
static uint16_t frameLinesWritten() { return Wire.field(3, 0x02, 4, 11) + 1; }


static void forgetWrites()
{
    for (uint8_t s = 0; s < FakeTwoWire::Segments; ++s)
        for (int r = 0; r < 256; ++r)
            Wire.touched[s][r] = false;
    Wire.trace.clear();
}

TEST_CASE("an output change re-solves the raster without re-measuring the source")
{
    // Picking a different resolution is not a source event. The rate and the
    // line count the last solve measured still describe the source, so the
    // engine re-solves raster, clock and windows from what it holds and nothing
    // is frozen, reset or measured again.
    //
    // 720p keeps the line doubler, which 480p does not -- see below.
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    REQUIRE(frameLinesWritten() == 1125);

    forgetWrites();
    REQUIRE(settled.engine.setOutputMode(&Mode720p));

    CHECK(frameLinesWritten() == 750);
    CHECK(Wire.touched[3][0x16]);          // VDS_HSCALE -- the windows followed

    // PLLAD_MD. The divider is the source's, and rewriting it where the scan
    // mode has not moved costs an ADC PLL relock for nothing.
    CHECK_FALSE(Wire.touched[5][0x12]);
}

// The proportion as /geometry states it: ten-thousandths, which is the
// precision a framing is compared at.
static int reported(float proportion)
{
    return (int)(proportion * 10000.0f + 0.5f);
}

// One capture unit out of the thousand or so capturable is about ten
// ten-thousandths, so this permits the re-quantisation and nothing else.
static const int FramingQuantisation = 20;

static bool within(int got, int wanted)
{
    const int off = got > wanted ? got - wanted : wanted - got;
    return off <= FramingQuantisation;
}

TEST_CASE("the framing is a proportion, so it reproduces at every output size")
{
    // THE FRAMING IS STORED AS PROPORTIONS, SO IT MUST NEVER CLAMP. Scale::Min
    // is raster / maxMagnification, which makes the reachable range
    // raster-independent by construction: a smaller output shrinks what the
    // scaler produces with it, lowering the magnification and moving AWAY from
    // the floor. A clamp anywhere is a defect in the arithmetic, and the way it
    // shows is a picture that will not fill a smaller output.
    //
    // ONE SOURCE THROUGHOUT. SourceKey is the line count and the field rate, so
    // changing the input mode is a different key and a different framing, which
    // compares nothing.
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    // At the precision /geometry reports, which is the proportion in
    // ten-thousandths. Exact float equality would be testing the quantisation
    // instead: calculateInputFormatterRegisters() hands the framing to the
    // capture window and takes back what integer units could express, so a
    // re-solve moves it by a ten-thousandth or two either way.
    const int originH = reported(settled.engine.framing().originOn(AxisHorizontal));
    const int extentH = reported(settled.engine.framing().extentOn(AxisHorizontal));
    const int originV = reported(settled.engine.framing().originOn(AxisVertical));
    const int extentV = reported(settled.engine.framing().extentOn(AxisVertical));

    // 480p and 576p are left out. The arithmetic reproduces there too, but the
    // PICTURE is shredded at both, so no bench run can corroborate this leg and
    // a green assertion would be the only evidence for it. Put them back when
    // the shredding is fixed.
    // docs/investigations/a-short-raster-and-a-short-source-shred-the-picture.md
    const OutputMode *modes[] = {&Mode1024p, &Mode960p, &Mode720p, &Mode1080p};
    for (uint8_t i = 0; i < 4; ++i) {
        REQUIRE(settled.engine.setOutputMode(modes[i]));

        // The framing survives the output change. Not to the last
        // ten-thousandth: the capture window can only express whole units, so
        // each solve hands the framing back re-quantised and it moves a unit or
        // two. What this is looking for is a CLAMP, which moves it by hundreds.
        CHECK(within(reported(settled.engine.framing().originOn(AxisHorizontal)), originH));
        CHECK(within(reported(settled.engine.framing().extentOn(AxisHorizontal)), extentH));
        CHECK(within(reported(settled.engine.framing().originOn(AxisVertical)), originV));
        CHECK(within(reported(settled.engine.framing().extentOn(AxisVertical)), extentV));

        // And what it produces is the same fraction of whatever raster it
        // landed on. produced is capture x 1024 / scale, both axes, no loss
        // term. docs/scaler-geometry-model.md
        const float producedH = (float)GBS::IF_HB_ST2::read() - (float)GBS::IF_HB_SP2::read();
        const float rasterH = (float)GBS::VDS_HSYNC_RST::read() + 1.0f;
        const float fraction = producedH * 1024.0f
                               / (float)GBS::VDS_HSCALE::read() / rasterH;
        CHECK(fraction > 0.30f);
        CHECK(fraction < 1.10f);

        // Nothing clamped: the scale stayed off its floor, which is what a
        // raster-independent range means.
        CHECK(GBS::VDS_HSCALE::read() > Scale::Min);
    }
}

TEST_CASE("an output too short for the doubled frame turns the line doubler off")
{
    // The doubler is a property of the OUTPUT as much as of the source: 311
    // lines doubled is 624, which fits a 1125-line frame and does not fit a
    // 525-line one. So an output change can move the scan mode, and the divider
    // with it: an IF unit is two ADC samples on a doubled line and one on an
    // undoubled one, so the same capture width costs twice the divider.
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    REQUIRE(Wire.field(5, 0x12, 0, 12) == 2200);

    REQUIRE(settled.engine.setOutputMode(&Mode480p));

    CHECK(frameLinesWritten() == 525);
    // Not half of 2508: undoubled, one IF unit is one ADC sample. What binds
    // is the 480p raster -- Axis::maximumCapture of it, rounded even -- and
    // not a constant of the part.
    CHECK(Wire.field(5, 0x12, 0, 12) == 1804);
    CHECK(Wire.field(1, 0x0E, 0, 11) <= InputFormatter::LineCounterMax);
}

TEST_CASE("an output change while a mode change is in flight waits for it")
{
    // The pending change has not measured the new source yet, so solving a
    // raster here would size it from the rate of the source being left. The
    // choice is kept and the poll that lands resolves it.
    SettledEngine settled;

    setSourceLines(97);                    // unsettled: no poll can complete
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    CHECK_FALSE(settled.engine.setOutputMode(&Mode480p));

    setSourceLines(311);
    REQUIRE(pollUntilSolved(settled.acquisition));
    CHECK(frameLinesWritten() == 525);
}

TEST_CASE("the engine always holds what the output is doing")
{
    // rasterMode_ == 0 used to mean two things -- in bypass, or nothing solved
    // yet -- so no caller could ask "is the output bypassed" without also
    // catching a chip that had never solved.
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    REQUIRE((settled.engine.outputMode() == &Mode1080p));
    CHECK_FALSE(settled.engine.outputMode()->isBypass());

    settled.engine.setOutputMode(&ModeBypass);
    REQUIRE((settled.engine.outputMode() != 0));
    CHECK(settled.engine.outputMode()->isBypass());
}

TEST_CASE("nothing solved yet is not bypass")
{
    DisplayClock clock;
    SourceMeasurement sampling;
    FramingTable framings;
    VideoPath engine(clock, sampling, framings);

    CHECK((engine.outputMode() == 0));
}

TEST_CASE("a raster is never solved for bypass")
{
    // OutputMode is a raster-geometry value and ModeBypass has no raster, so a
    // solve here would write zeros with every register self-consistent.
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));
    const uint16_t solved = frameLinesWritten();

    // 10 cast rather than named: pass-through is not a resolution and
    // setOutputMode() can no longer be asked for it. A choice naming no
    // resolution keeps the raster it had rather than solving a wrong one.
    forgetWrites();
    CHECK_FALSE(settled.engine.setOutputMode(0));

    CHECK_FALSE(Wire.touched[3][0x02]);      // VDS_VSYNC_RST, the frame total
    CHECK_FALSE(Wire.touched[3][0x01]);      // VDS_HSYNC_RST, the line total
    CHECK(frameLinesWritten() == solved);
}

// STATUS_SYNC_PROC_HLOW_LEN, s0_19[11:0] -- the hsync low time in ADC samples.
static void setHsyncLow(uint16_t samples)
{
    Wire.bank[0][0x19] = samples & 0xFF;
    Wire.bank[0][0x1A] = (Wire.bank[0][0x1A] & 0xF0) | ((samples >> 8) & 0x0F);
}

static bool g_passedThrough = false;
static void notePassThrough() { g_passedThrough = true; }

TEST_CASE("a source passed through still names where its active video starts")
{
    // The raster a source runs is the MEASUREMENT's to establish, not the
    // solve's: pass-through never solves, and the bypass channel's vertical
    // blanking is the one thing that needs it. Measured on the bench, a Wii at
    // 480p reached the bypass switch with this still zero and the channel
    // blanked 64 lines where the raster puts active video at 36.
    //
    // 720x480p is 525 lines at 59.94 Hz with 62 sync of 858, and the duty is
    // taken against the divider the engine solves for this source.
    SettledEngine settled;
    setSourceLines(524);
    setHsyncLow(113);   // 62 of 858, in the 1566 samples that divider counts
    g_fieldRate = 59.94f;
    g_passedThrough = false;

    settled.acquisition.usePassThroughSwitch(notePassThrough);
    settled.acquisition.allowPassThrough(true);
    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    pollUntilSolved(settled.acquisition);

    REQUIRE(g_passedThrough);
    CHECK(settled.engine.sourceActiveStartLine() == 36);
}

// The display window is what the encoder samples, and the encoder takes analog
// video with HS_OUT/VS_OUT -- so what reaches it is the TIME between edges, not
// the pixels the VDS counts in. `Mode1080p` states CEA-861 1080p60 to the
// nanosecond, and solve() derives a near bound from it on both axes, but only
// the far bounds were kept: the window opened where the picture happened to
// fall, 528 ns after sync horizontally against the 997 ns stated, and
// vertically at line 3 with the vsync pulse still asserted to line 5.
//
// docs/investigations/the-picture-position-is-latched-not-re-rolled.md
TEST_CASE("the display window opens at the porch the output mode states")
{
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    const OutputTimings raster = Mode1080p.solve(g_fieldRate, OutputMode::EngineCeilingHz);

    SUBCASE("vertically, clear of the sync pulse it would otherwise sit inside") {
        CHECK(Wire.field(3, 0x14, 4, 11) >= raster.activeLinesStart);
        CHECK(Wire.field(3, 0x14, 4, 11) > Wire.field(3, 0x0E, 4, 11));
    }

    SUBCASE("horizontally, after the back porch the mode asks for") {
        CHECK(Wire.field(3, 0x11, 4, 12) >= raster.activeStart);
    }
}

// The write origin is pipeline latency before the first written pixel, and the
// back porch is where it belongs: the memory window opens inside the porch and
// the picture still starts at activeStart. Charged against the active region
// instead, the picture stops short of the far bound and the gap paints black --
// measured on the bench as 30 px of a 1396 px window, a 2% band down the right.
//
// Asserted against the raster the ENGINE solved rather than one re-solved here:
// the measured field rate quantises a little differently and lands a 1920 px
// line against this file's 1916, which moves both porches by a pixel.
TEST_CASE("the picture fills the active region, the porch carrying the write origin")
{
    SettledEngine settled;

    settled.engine.setOutputMode(&Mode1080p);
    settled.engine.inputTimingsChanged(4);
    REQUIRE(pollUntilSolved(settled.acquisition));

    const OutputTimings raster = Mode1080p.solve(g_fieldRate, OutputMode::EngineCeilingHz);
    const long total = Wire.field(3, 0x01, 0, 12) + 1;     // VDS_HSYNC_RST
    const long start = Wire.field(3, 0x11, 4, 12);         // VDS_DIS_HB_SP
    const long stop = Wire.field(3, 0x10, 0, 12);          // VDS_DIS_HB_ST

    // The back porch is a time, so it is the same count at either raster. The
    // aperture opens one capture unit past it, on the first unit fully written.
    const long reach = 1 + Scale::Unity / Wire.field(3, 0x16, 0, 10);
    CHECK(start >= raster.activeStart);
    CHECK(start <= raster.activeStart + reach);

    SUBCASE("and the far edge sits on the mode's front porch, not short of it") {
        // Within ONE SCALE STEP, not one pixel: the window's far edge is a
        // whole unit and one unit of VDS_HSCALE is produced / scale of picture,
        // three output pixels at this magnification. A tolerance of a pixel
        // pins the framing rather than the rule.
        //
        // The porch left is wider than the mode's by the interpolator's reach:
        // the aperture closes one capture unit's worth of output short of the
        // picture, because the unit it would otherwise show reads past the last
        // unit captured.
        const long wanted = raster.horizontalTotal - raster.activeStop;
        const long step = 1 + (stop - start) / Wire.field(3, 0x16, 0, 10);  // VDS_HSCALE
        const long reach = 1 + Scale::Unity / Wire.field(3, 0x16, 0, 10);
        // And by the parity give-back: an even memory window shears, so the
        // aperture hands one unit back rather than taking one.
        CHECK(total - stop >= wanted - step);
        CHECK(total - stop <= wanted + step + reach + 1);
    }

    // One unit of scale is several output lines at any magnification worth the
    // name -- 2.37 at 2.25x on the bench -- so refusing a rounding overshoot of
    // a fifth of a line costs two whole lines of picture. The far edge is
    // clamped in Axis::solve, so an overshoot below a unit is clipped there and
    // shows as nothing.
    SUBCASE("vertically too, within the line the scale can actually resolve") {
        const long lines = Wire.field(3, 0x13, 0, 11) - Wire.field(3, 0x14, 4, 11);
        const long wanted = raster.activeLinesStop - raster.activeLinesStart;
        // Two reaches: the aperture is inset one capture unit at EACH end.
        const long reach = 1 + Scale::Unity / Wire.field(3, 0x17, 4, 10);
        CHECK(lines >= wanted - 1 - 2 * reach);
        CHECK(lines <= wanted);
    }
}
