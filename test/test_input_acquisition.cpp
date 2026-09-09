// Host tests for InputAcquisition -- `make -C test input-acquisition`.
//
// It owns the tick and calls down for each piece, so what is asserted here is
// the OUTCOME: a source driven through this class alone reaches the same solved
// registers that driving Tv5725::VideoPath directly reaches. A test that only
// checked the call was forwarded would pass against a class that forwarded it
// to nothing useful. docs/input-acquisition.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "SolvedEngine.h"
#include "RegistersWritten.h"

#include "../GBSC-Pro-Source code/gbs-control/src/input/InputAcquisition.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Adc.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputFormatter.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoProcessor.h"

using namespace Tv5725;

// The bench RiscPC as SolvedEngine seeds it, but solved through the layer
// rather than by calling the engine's own poll().
static void seedBenchSource()
{
    Wire.reset();
    poisonChip();
    g_fieldRate = 50.08f;
    seed(3, 0x01, 0, 12, 1915);
    seed(3, 0x02, 4, 11, 1124);
    seed(1, 0x0E, 0, 11, 1276);
    seed(0, 0x19, 0, 12, 181);
    seed(5, 0x12, 0, 12, 2553);
    seed(0, 0x1B, 0, 11, 311);
}

// One tick per interval, which is what the layer's own cadence asks for. The
// clock is carried in so a case can go on ticking after one of these returns.
static bool solveThrough(InputAcquisition &acquisition, uint32_t &nowMs)
{
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i) {
        nowMs += InputAcquisition::DetectionIntervalMs;
        if (acquisition.poll(nowMs))
            return true;
    }
    return false;
}

// The bench anchors: the raster this output asks for, and the divider the
// engine solves for a 311-line 50 Hz source -- not the 2553 the seed left,
// which is the previous load's.
static void checkBenchAnchors()
{
    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 1915);
    CHECK(Adc::PLLAD_MD::read() == 2250);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == 2250 / 2);
}

TEST_CASE("a source driven through InputAcquisition solves the same registers")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);

    uint32_t nowMs = 0;
    REQUIRE(solveThrough(acquisition, nowMs));

    checkBenchAnchors();
}

TEST_CASE("the layer reports what the source is running")
{
    // The measurement is coordinated here, so this is where the answer comes
    // from. Asserted against the solve rather than against the seed: a
    // publisher wired to a second SourceMeasurement would read zero.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);

    uint32_t nowMs = 0;
    REQUIRE(solveThrough(acquisition, nowMs));

    CHECK(acquisition.sourceLineRateHz() == sampling.heldLineRateHz());
    CHECK(acquisition.sourceLineRateHz() != 0);
    CHECK(acquisition.sourceFieldRateHz() == doctest::Approx(50.08f));

    // The bench source is a 15 kHz line, which is what decides whether bypass
    // can be displayed at all.
    CHECK(acquisition.sourceLowLineRate());
}

TEST_CASE("detection runs on the layer's cadence, not on every call")
{
    // loop() goes round far faster than the interval, so a run counted per call
    // is not the same length as one counted per tick -- and every threshold
    // keyed on it means something different. The cadence is the layer's because
    // the layer owns the tick; the engine no longer sees a clock at all.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);

    uint32_t now = 0;
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples; ++i) {
        now += InputAcquisition::DetectionIntervalMs;
        if (acquisition.poll(now))
            break;
    }
    REQUIRE_FALSE(path.changing());

    // The source moves, and the layer is hammered inside one interval.
    seed(0, 0x1B, 0, 11, 524);
    for (uint16_t i = 0; i < 200; ++i)
        acquisition.poll(now);
    CHECK_FALSE(path.changing());

    // On the cadence, the same source change is noticed.
    for (uint8_t i = 0; i < 4 * SourceMeasurement::SteadySamples
                        && !path.changing(); ++i) {
        now += InputAcquisition::DetectionIntervalMs;
        acquisition.poll(now);
    }
    CHECK(path.changing());
}

// The run gate, on the tick rather than inside the engine. loop() reaches this
// layer directly rather than through the sync watcher, so the freeze five sketch
// functions honour reached nothing -- and a bench measurement that froze
// automation had the solver rewriting the windows underneath it.
static bool g_mayRun = true;
static bool runGate() { return g_mayRun; }

TEST_CASE("a shut gate stops the path writing anything")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);
    acquisition.useRunGate(runGate);
    g_mayRun = false;

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);
    Wire.reset();
    poisonChip();

    uint32_t nowMs = 0;
    CHECK_FALSE(solveThrough(acquisition, nowMs));
    CHECK(registersWritten() == 0);
}

TEST_CASE("the gate is asked per tick, so what it stopped resumes")
{
    // A change outstanding when the gate shuts is still outstanding when it
    // opens: the mode change is picked back up rather than lost.
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);
    acquisition.useRunGate(runGate);
    g_mayRun = false;

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);
    uint32_t nowMs = 0;
    REQUIRE_FALSE(solveThrough(acquisition, nowMs));

    g_mayRun = true;
    REQUIRE(solveThrough(acquisition, nowMs));
    checkBenchAnchors();
}

TEST_CASE("no gate runs, which is what every caller did before")
{
    seedBenchSource();
    DisplayClock clock;
    SourceMeasurement sampling;
    VideoPath path(clock, sampling);
    InputAcquisition acquisition(sampling, path);

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);

    uint32_t nowMs = 0;
    CHECK(solveThrough(acquisition, nowMs));
}
