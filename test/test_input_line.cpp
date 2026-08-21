// Host-compiled unit tests for Tv5725::InputLine -- `make -C test input-line`.
// What of a line arrives intact: the hsync pulse at the head, and the write
// limit past which nothing is captured. docs/capture-limits.md.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <initializer_list>

#include "CheckNear.h"
#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/InputLine.h"

using namespace Tv5725;

// IF_LINE_ST/SP is the input formatter's PROGRESSIVE line window -- line double
// timing, so deinterlacing's rather than the picture's -- and it has to span
// exactly one line from wherever it starts.
TEST_CASE("the progressive line window spans exactly one line")
{
    const InputLine SourceLine = InputLine::measured(1126, 160, 2250);

    SUBCASE("it starts where IF_LINE_ST says and runs a whole line") {
        // The bench value: 64 + 1126 = 1190.
        CHECK(SourceLine.progressiveStop(64) == 1190);
    }

    SUBCASE("a different start moves the stop with it") {
        // ofw_RGBS and ofw_ypbpr ship IF_LINE_ST 0x18.
        CHECK(SourceLine.progressiveStop(24) == 1150);
    }

    SUBCASE("a longer line makes a longer window") {
        // The whole reason this cannot be a constant: PLLAD_MD moves and the
        // line moves with it.
        CHECK(InputLine::measured(1057, 128, 2114).progressiveStop(64) == 1121);
    }

    SUBCASE("it may run past the end of the line, and that is not a fault") {
        // A stop of 1190 on a 1126 unit line was once reported as a stray write. It is a stop position measured from a start, not a
        // position within the raster, so it rolls.
        CHECK(SourceLine.progressiveStop(64) > SourceLine.units());
    }
}

// The pulse is at the HEAD, and its width is derived from HLOW_LEN over
// PLLAD_MD rather than held as a constant. The tail is deliberately unbounded.
// docs/scaler-geometry-model.md "The two green regions in an IF line".
TEST_CASE("the hsync pulse width comes from the measured duty")
{
    // The bench RiscPC: a 7.1% hsync duty, measured 2026-08-09 as HLOW_LEN 181
    // of PLLAD_MD 2553 and read here at the 2250 the write limit caps the
    // divider to. 160 x 1126 / 2250 = 80.07 -> 81.
    const uint16_t HsyncLow = 160, AdcLine = 2250, LineUnits = 1126;
    const InputLine SourceLine = InputLine::measured(LineUnits, HsyncLow, AdcLine);

    SUBCASE("the pulse width comes from the hsync duty") {
        CHECK(SourceLine.syncUnits() == 81);
    }

    SUBCASE("a wider pulse excludes proportionally more") {
        // 800x600@60 is hsync 128 of 1056, a duty of 0.121 -- nearly twice the
        // bench source's. A fixed guard would under-clip it.
        CHECK(InputLine::measured(1126, 128, 1056).syncUnits() == 137);
    }

    SUBCASE("an unmeasurable duty falls back to what the retimer is set for") {
        // HLOW_LEN is a live measurement and rails; the firmware discards a
        // reading outside 0.041..0.152 too (gbs-control.ino:4858). Failing open
        // would restore the green bands, so the fallback is the fraction
        // SP_RT_HS_SP = PLLAD_MD x 0.93 configures the retimer for.
        for (uint16_t railed : {(uint16_t)0, (uint16_t)4095, (uint16_t)10}) {
            // ceil(1126 x 0.07) = 79, against the 81 the duty measures.
            CHECK(InputLine::measured(1126, railed, 2250).syncUnits() == 79);
        }
    }

    SUBCASE("a line with nothing measured keeps all of itself") {
        CHECK(InputLine(1126).syncUnits() == 0);
        CHECK(InputLine(1126).firstCapture() == 0);
        CHECK(InputLine(1126).lastCapture() == 1124);
    }
}

TEST_CASE("the capture stops at the write limit, however long the line is")
{
    // SourceMeasurement caps the divider so the line arrives inside the limit, but
    // adopt() takes whatever a custom preset or a bypass switch left in
    // PLLAD_MD, so a longer line still reaches the engine. Past the limit
    // nothing is written, and a window that reaches there loses the picture in
    // it rather than showing it. docs/capture-limits.md
    CHECK(InputLine(1277).lastCapture() == InputLine::WriteLimitUnits);

    SUBCASE("a line already inside it is bound by its own wrap") {
        // The two bounds meet at the divider SourceMeasurement now chooses: 1126 units,
        // where the wrap is the tighter by two.
        CHECK(InputLine(1126).lastCapture() == 1124);
        CHECK(InputLine(1126).lastCapture() < InputLine::WriteLimitUnits);
    }

    SUBCASE("the head guard still applies, and the two do not cross") {
        InputLine bench = InputLine::measured(1277, 181, 2553);
        CHECK(bench.firstCapture() < bench.lastCapture());
        CHECK(bench.capturable() == InputLine::WriteLimitUnits - bench.syncUnits());
    }
}
