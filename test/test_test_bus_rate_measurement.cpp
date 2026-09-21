// Host-compiled unit tests for Tv5725::TestBusRateMeasurement --
// `make -C test test-bus-rate-measurement`.
//
// The chip routes one signal onto the debug pin and the ESP counts its edges.
// The count is stubbed here, so what is under test is the selection around it
// and the arithmetic after it.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake/Wire.h"

FakeTwoWire Wire;

#include "DebugPinStub.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/TestBusRateMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/TestBus.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncProcessor.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SyncMeasurement.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Chip.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

void tv5725Log(const char *) {}

using namespace Tv5725;

static float g_sourceRate = 0.0f;
static unsigned g_samples = 0;

uint32_t debugPinPulseTicks()
{
    ++g_samples;
    return ticksForHz(g_sourceRate);
}

// A pin pulsing at this rate, and nothing sampled yet. The sync type is held
// in a static that outlives a case, so it is set here rather than inherited
// from whichever case ran last.
static void given(float hz)
{
    Wire.reset();
    g_sourceRate = hz;
    g_samples = 0;
    SyncMeasurement::set(false);
}

TEST_CASE("the rate is what the pin's pulse says")
{
    given(50.08f);

    CHECK(TestBusRateMeasurement::sourceFieldRateHz(false) ==
          doctest::Approx(50.08f).epsilon(0.001));
}

TEST_CASE("one measurement is one sample")
{
    // Each sample spins for an edge on the board, up to FS_SAMPLE_TIMEOUT_MS,
    // so a second one is a quarter of a second the caller did not ask for.
    given(50.08f);

    TestBusRateMeasurement::sourceFieldRateHz(false);

    CHECK(g_samples == 1);
}

TEST_CASE("a rate the board is not expecting costs no more than any other")
{
    // 100 Hz is outside the 47..86 band upstream retried on. Retrying bought
    // nothing -- the second reading was taken as it stood -- and cost a sample
    // on every measurement of any source outside it.
    given(100.0f);

    CHECK(TestBusRateMeasurement::sourceFieldRateHz(false) ==
          doctest::Approx(100.0f).epsilon(0.001));
    CHECK(g_samples == 1);
}

TEST_CASE("no pulse is no rate")
{
    // A timed-out sample reports no ticks, and 0 Hz is how the caller sees a
    // source that is not there.
    given(0.0f);

    CHECK(TestBusRateMeasurement::sourceFieldRateHz(false) == 0.0f);
}

TEST_CASE("the pin is left carrying what was measured")
{
    // Nothing is put back. Every reader of the pin selects its own signal
    // before reading, so restoring bought an invariant nobody depended on.
    given(50.08f);
    TestBus::select(0x1F);

    TestBusRateMeasurement::sourceFieldRateHz(false);

    CHECK(TestBus::selected() == 0);
}

TEST_CASE("a composite-sync PLL is timed off the sync separator")
{
    given(50.08f);
    SyncMeasurement::set(true);

    TestBusRateMeasurement::pllRateHz();

    CHECK(SyncProcessor::SP_TEST_MODULE::read() ==
          (uint8_t)SyncProcessor::TestModuleCsSep);
    CHECK(SyncProcessor::SP_TEST_EN::read() == 1);
}

TEST_CASE("a separate-sync PLL is timed off vertical sync activity")
{
    // The separator has nothing to separate on a source that sends its own V.
    given(50.08f);
    SyncMeasurement::set(false);

    TestBusRateMeasurement::pllRateHz();

    CHECK(SyncProcessor::SP_TEST_MODULE::read() ==
          (uint8_t)SyncProcessor::TestModuleVsActDet);
}

TEST_CASE("the reserved bit of the stage selector survives a measurement")
{
    // s5_63 bit 7 is RESERVED in RD-5725-1.1's own table. A byte write across
    // the register clears it; the three named fields are tied instead, so one
    // transaction sets them and leaves what nothing documents alone.
    given(50.08f);
    Wire.bank[5][0x63] = 0x80;

    TestBusRateMeasurement::pllRateHz();

    CHECK((Wire.bank[5][0x63] & 0x80) == 0x80);
}

TEST_CASE("a measurement leaves the pin's pad driven")
{
    // The pad is the other half of a driven selection, and a rate measured
    // with it clear reads 0 Hz with every register naming the right signal.
    given(50.08f);
    Chip::PAD_BOUT_EN::write(0);

    TestBusRateMeasurement::sourceFieldRateHz(false);

    CHECK(Chip::PAD_BOUT_EN::read() == 1);
}

TEST_CASE("the formatter carrying the pulse is enabled by the measurement")
{
    // Nothing on the measuring path turned the formatter's test output on. It
    // was left to resetDebugPort(), which runs from a preset load and an input
    // detection -- so a solve armed before either measured through whatever
    // the chip retained across the ESP reset, and the fault this guards was
    // measured only during acquisition.
    given(50.08f);
    InputFormatter::IF_TEST_EN::write(0);

    TestBusRateMeasurement::sourceFieldRateHz(false);

    CHECK(InputFormatter::IF_TEST_EN::read() == 1);
}

TEST_CASE("the VDS carrying the output pulse is enabled by the measurement")
{
    given(50.08f);
    VideoProcessor::VDS_TEST_EN::write(0);

    TestBusRateMeasurement::outputFrameRateHz();

    CHECK(VideoProcessor::VDS_TEST_EN::read() == 1);
}
