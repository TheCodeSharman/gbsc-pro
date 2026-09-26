// Host-compiled unit tests for Tv5725::VideoSourceLine -- `make -C test video-source-line`.
// The counter a capture window is placed in, as the named constructors state
// it. Where a window may sit inside it is CaptureWindow's, and is tested there.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <initializer_list>

#include "CheckNear.h"
#include "LoggedLines.h"
#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"

// The line from a count in ADC samples and the divider it was counted at, which
// is the pair the chip reports. Their ratio is the duty; two samples to the unit
// is what says the line is doubled.
static Tv5725::VideoSourceLine measuredLine(uint16_t units, uint16_t hlowLen,
                                            uint16_t adcLine)
{
    return Tv5725::VideoSourceLine::forDuty(
        units,
        Tv5725::HsyncPulse(adcLine > 0 ? (float)hlowLen / (float)adcLine : 0.0f),
        adcLine >= units + units / 2, false);
}

using namespace Tv5725;

// The pulse is at the HEAD, and its width is derived from HLOW_LEN over
// PLLAD_MD rather than held as a constant.
// docs/scaler-geometry-model.md "The two green regions in an IF line".
TEST_CASE("the hsync pulse width comes from the measured duty")
{
    // The bench RiscPC: a 7.1% hsync duty, measured 2026-08-09 as HLOW_LEN 181
    // of PLLAD_MD 2553 and read here at the 2250 the write limit caps the
    // divider to. 160 x 1126 / 2250 = 80.07 -> 81.
    const uint16_t HsyncLow = 160, AdcLine = 2250, LineUnits = 1126;

    SUBCASE("the pulse width comes from the hsync duty") {
        CHECK(measuredLine(LineUnits, HsyncLow, AdcLine).syncUnits() == 81);
    }

    SUBCASE("a wider pulse excludes proportionally more") {
        // 800x600@60 is hsync 128 of 1056, a duty of 0.121 -- nearly twice the
        // bench source's. A fixed guard would under-clip it.
        CHECK(measuredLine(1126, 128, 1056).syncUnits() == 137);
    }

    SUBCASE("a line with nothing measured keeps all of itself") {
        CHECK(VideoSourceLine(1126).units() == 1126);
        CHECK(VideoSourceLine(1126).syncUnits() == 0);
        CHECK(VideoSourceLine(1126).headBlankingUnits() == 0);
    }
}

// The sync processor normalises the source's polarity before the count is
// taken, so a high-active source and a low-active one both reach this counter
// as a pulse on the leading edge. A line the chip measured therefore always
// carries its pulse at the head, whatever the source sends.
// docs/investigations/the-capture-floor-followed-a-normalised-polarity.md
TEST_CASE("a measured line carries its pulse at the head")
{
    CHECK(measuredLine(1495, 172, 1494).originLeadUnits() == 0);
}

// The capture path writes blanking past the hsync pulse where the line doubler
// is in circuit, and a window opened inside it takes that blanking into the
// picture. Nothing is written past an undoubled line's pulse.
// docs/investigations/tail-green.md
TEST_CASE("head blanking is carried only where the line is doubled")
{
    // Two ADC samples to the unit is the doubled path; one is not.
    CHECK(measuredLine(1103, 156, 2206).headBlankingUnits()
          == VideoSourceLine::DoubledHeadBlankingUnits);
    CHECK(measuredLine(1439, 176, 1438).headBlankingUnits() == 0);
}

// WHERE THE COUNTER ZEROES ON VERTICAL SYNC IS NOT THE SAME ON EVERY SYNC
// ARRANGEMENT, so the frame carries its vertical sync interval the way the line
// carries its hsync pulse. Separate sync zeroes on the pulse's trailing edge and
// video starts at the counter's own origin; sync on green zeroes on the leading
// edge, so the pulse is leading blanking and the video sits that far behind.
// Which edge a sync arrangement zeroes on is not derivable here.
// docs/investigations/the-vertical-origin-follows-the-sync-type.md
TEST_CASE("the frame states where its counter zeroes on vertical sync")
{
    // The Wii at 480p on ypbpr: 524 counted lines, so a 525-unit frame.
    SUBCASE("a frame given no vsync interval excludes nothing") {
        CHECK(VideoSourceLine::frame(525).syncUnits() == 0);
        CHECK(VideoSourceLine::frame(525).originLeadUnits() == 0);
    }

    SUBCASE("a frame zeroed on the leading edge carries the pulse as blanking") {
        CHECK(VideoSourceLine::frame(525, 7).syncUnits() == 7);
        CHECK(VideoSourceLine::frame(525, 7).originLeadUnits() == 7);
        CHECK(VideoSourceLine::frame(525, 7).headBlankingUnits() == 0);
    }
}
