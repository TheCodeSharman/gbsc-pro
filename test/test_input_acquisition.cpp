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

TEST_CASE("a source driven through InputAcquisition solves the same registers")
{
    seedBenchSource();
    DisplayClock clock;
    VideoPath path(clock);
    InputAcquisition acquisition(path);

    path.outputModeChanged(OutputChoice(Output1080P));
    path.inputTimingsChanged(4);

    uint32_t nowMs = 0;
    bool solved = false;
    for (uint8_t i = 0; !solved && i < 4 * SourceMeasurement::SteadySamples; ++i) {
        nowMs += VideoPath::DetectionIntervalMs;
        solved = acquisition.poll(nowMs);
    }

    REQUIRE(solved);

    // The bench anchors: the raster this output asks for, and the divider the
    // engine solves for a 311-line 50 Hz source -- not the 2553 the seed left,
    // which is the previous load's.
    CHECK(VideoProcessor::VDS_HSYNC_RST::read() == 1915);
    CHECK(Adc::PLLAD_MD::read() == 2250);
    CHECK(InputFormatter::IF_HSYNC_RST::read() == 2250 / 2);
}
