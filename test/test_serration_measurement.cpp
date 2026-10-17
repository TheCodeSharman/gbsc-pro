// Host-compiled unit tests for Tv5725::SerrationMeasurement -- `make -C test serration-measurement`.
//
// SP_H_PULSE_IGNOR has to sit above the serrations a broadcast vertical
// interval carries and below the source's own sync pulse, and on this bench
// those two bounds do not overlap: the serrated source needs 3.68% of the line
// or more and the unserrated ones hold the count only at 2.81% or less.
// docs/investigations/the-composite-count-dithers-where-the-ignore-nears-the-pulse.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SerrationMeasurement.h"

using namespace Tv5725;

namespace {

uint16_t admitting = 0;
uint16_t rejecting = 0;
int readings = 0;

const uint16_t *dithering = 0;
uint8_t dithered = 0;

uint16_t countAt(bool rejectsSerrations)
{
    if (dithering != 0 && !rejectsSerrations) {
        ++readings;
        return dithering[dithered++ % 5];
    }
    ++readings;
    return rejectsSerrations ? rejecting : admitting;
}

void given(uint16_t below, uint16_t above)
{
    SerrationMeasurement::forget();
    admitting = below;
    rejecting = above;
    readings = 0;
    dithering = 0;
    dithered = 0;
}

}  // namespace

TEST_CASE("a count the lower threshold raises by the vertical interval is serrated")
{
    // The Wii in PAL 576i: 319 against a true 310 below the floor.
    given(319, 310);

    CHECK(SerrationMeasurement::serration(countAt));
}

TEST_CASE("a source reading the same count through both thresholds is not serrated")
{
    // The RISC PC at 320x256@50 on composite sync: 308 either side.
    given(308, 308);

    CHECK_FALSE(SerrationMeasurement::serration(countAt));
}

TEST_CASE("the count's own dither is not a vertical interval")
{
    // 640x512@50 on composite sync dithers across 527..531, and the widest
    // separation two readings of it can show is below the threshold.
    given(530, 527);

    CHECK_FALSE(SerrationMeasurement::serration(countAt));
}

TEST_CASE("a count the lower threshold LOWERS is not serrated either")
{
    given(527, 531);

    CHECK_FALSE(SerrationMeasurement::serration(countAt));
}

TEST_CASE("the probe runs once per source, not once per pass")
{
    given(319, 310);

    SerrationMeasurement::serration(countAt);
    SerrationMeasurement::serration(countAt);
    SerrationMeasurement::serration(countAt);

    CHECK(readings == 2 * SerrationMeasurement::Samples);
}

TEST_CASE("forgetting re-arms the probe")
{
    given(319, 310);
    SerrationMeasurement::serration(countAt);

    SerrationMeasurement::forget();
    SerrationMeasurement::serration(countAt);

    CHECK(readings == 4 * SerrationMeasurement::Samples);
}

TEST_CASE("a value merely set does not suppress the probe")
{
    given(319, 310);
    SerrationMeasurement::set(false);

    CHECK_FALSE(SerrationMeasurement::isSet());
}

TEST_CASE("a probe through counts no source produces establishes nothing")
{
    // The reference clock before the divider latches reads 97 for no lock, and
    // a comparison of two such readings is arithmetic on noise.
    given(97, 97);

    SerrationMeasurement::serration(countAt);

    CHECK_FALSE(SerrationMeasurement::isSet());
}

TEST_CASE("one stray reading does not carry the verdict")
{
    // The count dithers, so a single pair can separate by more than the
    // interval does. 640x512@50 composite visits 527..531, and one reading at
    // each end of that against a steady 527 is four lines apart.
    static const uint16_t wandering[5] = {531, 528, 529, 527, 530};
    given(0, 527);
    dithering = wandering;

    CHECK_FALSE(SerrationMeasurement::serration(countAt));
}

TEST_CASE("and the interval still carries it through the same dither")
{
    static const uint16_t wandering[5] = {321, 318, 319, 317, 320};
    given(0, 310);
    dithering = wandering;

    CHECK(SerrationMeasurement::serration(countAt));
}

TEST_CASE("a source nothing has probed is assumed serrated")
{
    // The two errors are not equal. The higher threshold costs an unserrated
    // source a dithering count until the probe runs, where the lower one
    // leaves a serrated source counting its vertical interval as lines -- a
    // count nine lines out, which no raster matches and every window is then
    // solved from.
    SerrationMeasurement::forget();

    CHECK(SerrationMeasurement::assumeSerrated());
}

TEST_CASE("and takes the measurement once there is one")
{
    given(308, 308);
    SerrationMeasurement::serration(countAt);

    CHECK_FALSE(SerrationMeasurement::assumeSerrated());
}
