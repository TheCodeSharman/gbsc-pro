// Host-compiled unit tests for src/tv5725/FrameSync.h -- `make -C test
// frame-sync`.
//
// The lock steers the output frame time towards the source's. Which mechanism
// it uses is decided by what drives the display clock: an external generator
// takes a rate correction, the internal PLL takes a raster correction instead,
// because its rate cannot move.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/clock/ClockGen.h"
#include "../GBSC-Pro-Source code/gbs-control/src/si5351mcu.h"

#include "Si5351Stubs.h"
#include "LoggedLines.h"

#include "../GBSC-Pro-Source code/gbs-control/gbs_types.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/DisplayClock.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/FrameSync.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoRoute.h"

using namespace Tv5725;

// The platform seam, driven rather than stubbed: this suite is the one that
// cares where the two pulses sit relative to each other, which is the whole of
// what a phase is.
static const uint32_t TicksPerSecond = 160000000u;

static uint32_t g_inputPeriod;
static uint32_t g_outputPeriod;
static uint32_t g_outputOffset;
static bool g_inputArrives;
static bool g_outputArrives;
static unsigned g_probes;

// vsyncPeriodAndPhase() selects the input bus, samples, selects the output bus
// and samples again, so which answer is due is read off TEST_BUS_SEL.
static bool onOutputBus() { return Wire.field(0, 0x4d, 0, 5) == 0x2; }

uint32_t debugPinTicksPerSecond() { return TicksPerSecond; }

// Added to every second output sample, so a suite can make two readings of the
// output rate disagree with each other.
static uint32_t g_outputWobble;
static unsigned g_outputSamples;

// The same for the input, which is how a source whose period reads differently
// on two consecutive samples behaves. The engine's settled rate is what the
// correction uses; only the phase comes from here.
static uint32_t g_inputWobble;
static unsigned g_inputSamples;

// How many frames the input period reads as. An edge ISR that misses a pulse
// times the one after it, so the pin hands back a whole multiple of the frame.
static uint32_t g_inputFramesPerRead;

bool debugPinPulseEdges(uint32_t *start, uint32_t *stop)
{
    if (onOutputBus()) {
        if (!g_outputArrives)
            return false;
        *start = 1 + g_outputOffset;
        *stop = *start + g_outputPeriod
                + ((g_outputSamples++ & 1) ? g_outputWobble : 0);
        return true;
    }
    if (!g_inputArrives)
        return false;
    *start = 1;
    *stop = 1 + g_inputPeriod * g_inputFramesPerRead
            + ((g_inputSamples++ & 1) ? g_inputWobble : 0);
    return true;
}

uint32_t debugPinPulseTicks()
{
    uint32_t start, stop;
    return debugPinPulseEdges(&start, &stop) ? stop - start : 0;
}

void debugPinProbe() { ++g_probes; }

static uint32_t ticksForHz(float hz) { return (uint32_t)((float)TicksPerSecond / hz); }

namespace {

// A 60 Hz source on a solved raster, with the read pointer a quarter of a frame
// behind the write pointer -- which is where the default target phase puts it.
void aLockedSource()
{
    Wire.reset();
    g_inputPeriod = ticksForHz(60.0f);
    g_outputPeriod = ticksForHz(60.0f);
    g_outputOffset = g_inputPeriod / 4;
    g_inputArrives = true;
    g_outputArrives = true;
    g_outputWobble = 0;
    g_outputSamples = 0;
    g_inputWobble = 0;
    g_inputSamples = 0;
    g_inputFramesPerRead = 1;
    g_probes = 0;
    g_logLines.clear();
    VideoRoute::toScaler();

    GBS::VDS_HSYNC_RST::write(1915);
    GBS::VDS_VSYNC_RST::write(1125);
    GBS::VDS_VS_ST::write(4);
}

// What runFrequency() insists on before it steers anything: the pad enabled,
// the scaler carrying the video, and the display clock taken from PCLKIN.
void theGeneratorDrivesTheDisplay()
{
    GBS::PAD_CKIN_ENZ::write(0);
    GBS::PLL648_CONTROL_01::write(DisplayClock::ExternalPclkIn);
}

}  // namespace

TEST_CASE("arming needs a raster and both vsync periods")
{
    aLockedSource();
    DisplayClock clock;
    FrameSync lock(clock);

    CHECK_FALSE(lock.ready());
    CHECK(lock.init());
    CHECK(lock.ready());
}

TEST_CASE("a raster that has not been solved yet arms nothing")
{
    aLockedSource();
    GBS::VDS_HSYNC_RST::write(0);

    DisplayClock clock;
    FrameSync lock(clock);

    CHECK_FALSE(lock.init());
    CHECK_FALSE(lock.ready());
}

TEST_CASE("a source whose vsync never arrives arms nothing, and says which side")
{
    aLockedSource();
    g_inputArrives = false;

    DisplayClock clock;
    FrameSync lock(clock);

    CHECK_FALSE(lock.init());

    SUBCASE("and the input is named rather than inferred") {
        CHECK(loggedContaining("no INPUT vsync"));
        CHECK_FALSE(loggedContaining("no OUTPUT vsync"));
    }

    SUBCASE("and the pin is probed, because a failure cannot say which side it is") {
        CHECK(g_probes == 1u);
    }
}

TEST_CASE("an output vsync that never arrives is named as the output")
{
    aLockedSource();
    g_outputArrives = false;

    DisplayClock clock;
    FrameSync lock(clock);

    CHECK_FALSE(lock.init());
    CHECK(loggedContaining("no OUTPUT vsync"));
    CHECK_FALSE(loggedContaining("no INPUT vsync"));
}

TEST_CASE("a disturbed lock is not quiet again until the interval has passed")
{
    aLockedSource();
    DisplayClock clock;
    FrameSync lock(clock);

    lock.defer(10000);

    CHECK_FALSE(lock.quietFor(500, 10400));
    CHECK(lock.quietFor(500, 10501));
}

TEST_CASE("an unarmed lock corrects nothing")
{
    aLockedSource();
    DisplayClock clock;
    FrameSync lock(clock);

    CHECK_FALSE(lock.runVsync(0, 60.0f));
    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u);
}

TEST_CASE("the first two passes after arming settle rather than correct")
{
    // The raster has just been written, so a phase measured across it is not
    // the steady one.
    aLockedSource();
    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();

    CHECK(lock.runVsync(0, 60.0f));
    CHECK(lock.runVsync(0, 60.0f));
    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u);
    CHECK(lock.lastCorrection() == 0);
}

TEST_CASE("a read pointer ahead of target is held back by stretching the raster")
{
    // Phase under target: the crossover is drifting towards live video, so the
    // output frame is made longer until it falls back.
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;   // well under the 90 degree target

    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);

    CHECK(lock.runVsync(0, 60.0f));

    CHECK(lock.lastCorrection() == FrameSync::Correction);
    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u + FrameSync::Correction);

    SUBCASE("and method 0 carries the vsync pulse with it") {
        CHECK(GBS::VDS_VS_ST::read() == 4u + FrameSync::Correction);
    }
}

TEST_CASE("method 1 stretches the raster and leaves the vsync pulse where it is")
{
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;

    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();
    lock.runVsync(1, 60.0f);
    lock.runVsync(1, 60.0f);
    lock.runVsync(1, 60.0f);

    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u + FrameSync::Correction);
    CHECK(GBS::VDS_VS_ST::read() == 4u);
}

TEST_CASE("a read pointer already past target is left alone")
{
    aLockedSource();
    g_outputOffset = (g_inputPeriod * 3) / 4;   // well past the 90 degree target

    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);

    CHECK(lock.lastCorrection() == 0);
    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u);
}

TEST_CASE("resetting takes the correction back out of the raster")
{
    // Both directions have to land somewhere defined, or a correction measured
    // against the state before a change stays in the raster for ever.
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;

    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);
    REQUIRE(GBS::VDS_VSYNC_RST::read() == 1125u + FrameSync::Correction);

    lock.reset(0);

    CHECK(GBS::VDS_VSYNC_RST::read() == 1125u);
    CHECK(GBS::VDS_VS_ST::read() == 4u);
    CHECK(lock.lastCorrection() == 0);
    CHECK_FALSE(lock.ready());
}

TEST_CASE("the target phase is a whole turn, whichever way it is given")
{
    aLockedSource();
    DisplayClock clock;
    FrameSync lock(clock);

    CHECK(lock.targetPhase() == FrameSync::DefaultTargetPhase);

    lock.setTargetPhase(-90);
    CHECK(lock.targetPhase() == 270);

    lock.setTargetPhase(450);
    CHECK(lock.targetPhase() == 90);
}

TEST_CASE("the rate correction steers nothing until a ratio is established")
{
    aLockedSource();
    theGeneratorDrivesTheDisplay();

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);
    clock.hold(0x85);

    FrameSync lock(clock);
    lock.init();

    // True rather than false: no ratio is an external state, not a failed
    // measurement, so it is not a lock failure for the caller to reset on.
    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() == 108000000u);
}

TEST_CASE("an output crossing early is slowed by lowering the display clock")
{
    // Phase is how far the output vsync sits after the input's. Under target
    // the crossover is drifting towards live video, so the output frame is
    // made longer -- here by slowing the clock that clocks it out.
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    g_outputOffset = g_inputPeriod / 100;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);
    clock.hold(0x85);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() < 108000000u);

    SUBCASE("by no more than the 0.06% a sink will follow") {
        // A 0.1% step is what switching between 59.94 and 60 costs, and some
        // displays drop sync on it.
        CHECK(clock.hzNow() >= (uint32_t)(108000000.0 * 0.9994));
    }
}

TEST_CASE("an output crossing late is hurried by raising the display clock")
{
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    g_outputOffset = (g_inputPeriod * 3) / 4;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);
    clock.hold(0x85);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() > 108000000u);
    CHECK(clock.hzNow() <= (uint32_t)(108000000.0 * 1.0006));
}

TEST_CASE("the rate correction leaves the display alone while the bypass carries it")
{
    // Pass-through drives the encoder from the source's own timing, so there is
    // nothing here to steer.
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    g_outputOffset = g_inputPeriod / 100;
    VideoRoute::toHdBypassChannel();

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() == 108000000u);
}

TEST_CASE("the rate correction leaves the display alone on the internal PLL")
{
    // Any mapped byte but PCLKIN runs the display off PLL648, whose rate
    // nothing can slew.
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    GBS::PLL648_CONTROL_01::write(0x85);
    g_outputOffset = g_inputPeriod / 100;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() == 108000000u);
}

TEST_CASE("cleanup forgets the ratio, so nothing is steered against a stale raster")
{
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    g_outputOffset = g_inputPeriod / 100;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());
    lock.cleanup();
    lock.init();

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() == 108000000u);
}

TEST_CASE("a reset keeps the ratio, because the callers that reset do not re-establish it")
{
    aLockedSource();
    theGeneratorDrivesTheDisplay();
    g_outputOffset = g_inputPeriod / 100;

    Si5351mcu part;
    Clock::ClockGen generator(part);
    DisplayClock clock;
    clock.attach(generator, 108000000u);
    clock.hold(0x85);

    FrameSync lock(clock);
    lock.init();
    lock.initFrequency(60.0f, clock.hzNow());
    lock.reset(0);
    lock.init();

    CHECK(lock.runFrequency(60.0f));
    CHECK(clock.hzNow() < 108000000u);
}

namespace {

// A board whose generator drives the display, with the part on PCLKIN.
struct SteerableClock {
    Si5351mcu part;
    Clock::ClockGen generator;
    DisplayClock clock;

    SteerableClock() : generator(part)
    {
        theGeneratorDrivesTheDisplay();
        clock.attach(generator, 108000000u);
        clock.hold(0x85);
    }
};

}  // namespace

TEST_CASE("the rate match puts the output's field rate on the source's")
{
    // The clock is set to the RATIO, so an output measured fast is answered by
    // a proportionally slower clock.
    aLockedSource();
    g_outputPeriod = ticksForHz(60.3f);

    SteerableClock board;
    FrameSync lock(board.clock);

    CHECK(lock.matchRate(60.0f));

    const double wanted = 108000000.0 * (60.0 / 60.3);
    CHECK(board.clock.hzNow() == doctest::Approx((double)wanted).epsilon(0.0005));
}

TEST_CASE("a source rate the engine has not settled on steers nothing")
{
    // Asked of the engine rather than measured again here: a reading taken off
    // the test bus just after the divider latches is repeatably wrong, and two
    // such samples agree with each other.
    aLockedSource();

    SteerableClock board;
    FrameSync lock(board.clock);

    CHECK_FALSE(lock.matchRate(0.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("an output rate no two samples agree on steers nothing")
{
    // The clock is set to a ratio involving this rate and nothing re-measures
    // until the next solve, so a pair that agrees and is wrong beats against
    // the source for the life of the boot. Declining is the better outcome.
    aLockedSource();
    g_outputWobble = g_outputPeriod / 10;

    SteerableClock board;
    FrameSync lock(board.clock);

    CHECK_FALSE(lock.matchRate(60.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("the rate match establishes the ratio the per-frame correction needs")
{
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();

    REQUIRE(lock.matchRate(60.0f));
    const uint32_t matched = board.clock.hzNow();

    CHECK(lock.runFrequency(60.0f));
    CHECK(board.clock.hzNow() != matched);
}

TEST_CASE("the rate match leaves the display alone while the bypass carries it")
{
    aLockedSource();
    g_outputPeriod = ticksForHz(60.3f);

    SteerableClock board;
    VideoRoute::toHdBypassChannel();
    FrameSync lock(board.clock);

    CHECK_FALSE(lock.matchRate(60.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("the rate match leaves the display alone on the internal PLL")
{
    aLockedSource();
    g_outputPeriod = ticksForHz(60.3f);

    SteerableClock board;
    GBS::PLL648_CONTROL_01::write(0x85);
    FrameSync lock(board.clock);

    CHECK_FALSE(lock.matchRate(60.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("a board with no generator has no rate to match")
{
    aLockedSource();
    theGeneratorDrivesTheDisplay();

    DisplayClock clock;
    clock.assumeHz(108000000u);
    FrameSync lock(clock);

    CHECK_FALSE(lock.matchRate(60.0f));
    CHECK(clock.hzNow() == 108000000u);
}

TEST_CASE("the correction takes the source's rate from the engine, not the pin")
{
    // Two readings of the input period taken here spread by more than the
    // agreement tolerance on a source the engine holds steady, so a correction
    // that insisted the pair agreed refused about nine times in ten.
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;
    g_inputWobble = g_inputPeriod / 60;   // well over RateAgreement's 0.05%

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    CHECK(lock.runFrequency(60.0f));
    CHECK(board.clock.hzNow() != 108000000u);
}

TEST_CASE("a source the engine has not settled on corrects nothing")
{
    // Not a lock failure: the gate lets a pass through on held state, and the
    // rate is the one thing here that comes from outside it.
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    CHECK(lock.runFrequency(0.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("a phase that cannot be measured is a lock failure")
{
    // The distinction that matters to the caller: the rate coming from the
    // engine does not make the phase measurable, and a pin carrying nothing is
    // what the forgiveness count exists for.
    aLockedSource();
    g_inputArrives = false;

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    CHECK_FALSE(lock.runFrequency(60.0f));
    CHECK(board.clock.hzNow() == 108000000u);
}

TEST_CASE("a phase that has wrapped past zero is corrected the short way")
{
    // `phase` is a position in [0, period), so a read pointer drifting down
    // through zero reappears at the top and the raw difference against a 90
    // degree target jumps from just under it to three quarters of a frame over
    // it. Answering that jump slams the clock the other way, and the loop then
    // hunts across the wrap instead of settling: measured on the bench as two
    // boots in six holding a limit cycle at the 0.06% clamp for three minutes.
    aLockedSource();
    g_outputOffset = g_inputPeriod - g_inputPeriod / 50;   // just below zero

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    REQUIRE(lock.runFrequency(60.0f));

    // The short way round is DOWN -- the pointer is a little behind the
    // target, not three quarters of a frame ahead of it.
    CHECK(board.clock.hzNow() < 108000000u);
}

TEST_CASE("the raster correction reads the wrap the same way")
{
    // Unwrapped, a pointer just below zero reads as far past the target and the
    // raster is left alone, so it drifts further and wraps again.
    aLockedSource();
    g_outputOffset = g_inputPeriod - g_inputPeriod / 50;

    DisplayClock clock;
    FrameSync lock(clock);
    lock.init();
    lock.runVsync(0, 60.0f);
    lock.runVsync(0, 60.0f);

    CHECK(lock.runVsync(0, 60.0f));
    CHECK(lock.lastCorrection() == FrameSync::Correction);
}

TEST_CASE("a phase at the target is left alone, whichever side it approaches from")
{
    aLockedSource();
    g_outputOffset = g_inputPeriod / 4;   // exactly the 90 degree target

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    REQUIRE(lock.runFrequency(60.0f));

    CHECK(board.clock.hzNow() == doctest::Approx(108000000.0).epsilon(0.00001));
}

TEST_CASE("the correction prints the phase it worked from, and where it was aimed")
{
    // The controlled variable. Inferred from the display clock instead, a noisy
    // phase and an oscillating one look alike, because the clock sits two steps
    // downstream -- phase, correction, clock.
    // ../docs/investigations/the-frame-time-lock-saturates.md
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;   // 26666 ticks of a 2666666 frame

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    REQUIRE(lock.runFrequency(60.0f));

    // A quarter of a frame is 666666 ticks, and the pointer is just past zero,
    // so it is 640000 behind the target rather than most of a frame ahead.
    CHECK(loggedContaining("phase 26666/2666666"));
    CHECK(loggedContaining("target 666666"));
    CHECK(loggedContaining("err -640000"));
}

TEST_CASE("a period the pin reads as three frames does not reach the correction")
{
    // A missed edge times the pulse after it, so the pin hands back a whole
    // multiple of the frame -- 2x, 3x and 6x measured on the bench, on exactly
    // the boots whose clock then saturates. The period scales the target and
    // folds the offset, so it moves the error by most of a frame while the
    // phase itself is healthy. The engine holds the source's rate to the
    // milli-hertz, so the period is arithmetic rather than a measurement.
    // ../docs/investigations/the-frame-time-lock-saturates.md
    aLockedSource();
    g_outputOffset = g_inputPeriod / 2;   // half a frame, past the 90 degree target

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());

    g_inputFramesPerRead = 3;

    REQUIRE(lock.runFrequency(60.0f));

    // Past the target, so the output is hurried. Read through a tripled period
    // the target lands past the same phase and the clock goes the other way.
    CHECK(board.clock.hzNow() > 108000000u);

    SUBCASE("and what the pin made of it is still printed, so a capture can see it") {
        CHECK(loggedContaining("pin 7999998/"));
    }
}

TEST_CASE("observing measures and prints without steering the display clock")
{
    // The loop is proportional feedback on an integrator, so at the clamp it
    // moves the phase by as much as the noise does and the clock cannot say
    // which it is watching. Parked, the phase must move in a straight line and
    // anything else is the measurement.
    // ../docs/investigations/the-frame-time-lock-saturates.md
    aLockedSource();
    g_outputOffset = g_inputPeriod / 100;

    SteerableClock board;
    FrameSync lock(board.clock);
    lock.init();
    lock.initFrequency(60.0f, board.clock.hzNow());
    lock.setObserveOnly(true);

    CHECK(lock.runFrequency(60.0f));
    CHECK(board.clock.hzNow() == 108000000u);

    SUBCASE("and the measurement is still printed, which is the whole point") {
        CHECK(loggedContaining("frame time lock: phase 26666/2666666"));
    }
}
