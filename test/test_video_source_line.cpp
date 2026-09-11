// Host-compiled unit tests for Tv5725::VideoSourceLine -- `make -C test input-line`.
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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"

using namespace Tv5725;

// IF_LINE_ST/SP is the input formatter's PROGRESSIVE line window -- line double
// timing, so deinterlacing's rather than the picture's -- and it has to span
// exactly one line from wherever it starts.
TEST_CASE("the progressive line window spans exactly one line")
{
    const VideoSourceLine SourceLine = VideoSourceLine::measured(1126, 160, 2250, 0, true);

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
        CHECK(VideoSourceLine::measured(1057, 128, 2114, 0, true).progressiveStop(64) == 1121);
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
    const VideoSourceLine SourceLine = VideoSourceLine::measured(LineUnits, HsyncLow, AdcLine, 0, true);

    SUBCASE("the pulse width comes from the hsync duty") {
        CHECK(SourceLine.syncUnits() == 81);
    }

    SUBCASE("a wider pulse excludes proportionally more") {
        // 800x600@60 is hsync 128 of 1056, a duty of 0.121 -- nearly twice the
        // bench source's. A fixed guard would under-clip it.
        CHECK(VideoSourceLine::measured(1126, 128, 1056, 0, true).syncUnits() == 137);
    }

    SUBCASE("an unmeasurable duty falls back to what the retimer is set for") {
        // HLOW_LEN is a live measurement and rails; the firmware discards a
        // reading outside 0.041..0.152 too (gbs-control.ino:4858). Failing open
        // would restore the green bands, so the fallback is the fraction
        // SP_RT_HS_SP = PLLAD_MD x 0.93 configures the retimer for.
        for (uint16_t railed : {(uint16_t)0, (uint16_t)4095, (uint16_t)10}) {
            // ceil(1126 x 0.07) = 79, against the 81 the duty measures.
            CHECK(VideoSourceLine::measured(1126, railed, 2250, 0, true).syncUnits() == 79);
        }
    }

    SUBCASE("a line with nothing measured keeps all of itself") {
        CHECK(VideoSourceLine(1126).syncUnits() == 0);
        CHECK(VideoSourceLine(1126).firstCapture() == 0);
        CHECK(VideoSourceLine(1126).lastCapture() == 1124);
    }
}

TEST_CASE("the capture stops at the write limit, however long the line is")
{
    // SourceMeasurement caps the divider so the line arrives inside the limit,
    // but a bypass switch writes PLLAD_MD itself, so a longer line still reaches
    // the engine. Past the limit nothing is written, and a window that reaches
    // there loses the picture in it rather than showing it.
    // docs/capture-limits.md
    CHECK(VideoSourceLine(1277).lastCapture() == VideoSourceLine::WriteLimitUnits);

    SUBCASE("a line already inside it is bound by its own wrap") {
        // The two bounds meet at the divider SourceMeasurement now chooses: 1126 units,
        // where the wrap is the tighter by two.
        CHECK(VideoSourceLine(1126).lastCapture() == 1124);
        CHECK(VideoSourceLine(1126).lastCapture() < VideoSourceLine::WriteLimitUnits);
    }

    SUBCASE("the head guard still applies, and the two do not cross") {
        VideoSourceLine bench = VideoSourceLine::measured(1277, 181, 2553, 0, true);
        CHECK(bench.firstCapture() < bench.lastCapture());
        // The ends would leave 1034 units between them; what the capture path
        // will write is less, and that is what may be offered.
        CHECK(bench.lastCapture() - bench.firstCapture()
              > VideoSourceLine::CaptureWidthLimitUnits);
        CHECK(bench.maxCaptureWidth() == VideoSourceLine::CaptureWidthLimitUnits);
    }
}

// Video does not reach the input formatter at the sync edge the line is counted
// from. Measured on four undoubled modes: the first active pixel lands ~72 units
// later than the sync-and-porch arithmetic places it, and one whole sync width
// earlier again where the hsync pulse is inverted, because the origin is then
// the pulse's trailing edge and the sync interval is already behind it.
// docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
TEST_CASE("the capture starts where video arrives, not at the sync edge")
{
    // 800x600@60 at PLLAD_MD 1124. HLOW_LEN 136 of 1124 is the 12.1% duty its
    // 128-of-1056 hsync gives, so 137 units of pulse.
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;
    const uint16_t Lag = VideoSourceLine::CaptureLagUnits;

    SUBCASE("a positive pulse sits at the head and the lag follows it") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, true).firstCapture()
              == 137 + Lag);
    }

    SUBCASE("an inverted pulse is behind the origin, leaving the lag alone") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, false).firstCapture()
              == Lag);
    }

    SUBCASE("an inverted pulse gives back the units the head guard was taking") {
        // A line short enough that what the capture path writes is not the
        // tighter of the two bounds, or both sides come back clamped equal.
        VideoSourceLine positive = VideoSourceLine::measured(900, 109, 900, Lag, true);
        VideoSourceLine inverted = VideoSourceLine::measured(900, 109, 900, Lag, false);
        CHECK(inverted.capturable() - positive.capturable() == positive.syncUnits());
    }

    SUBCASE("a line whose origin is placed for it takes no lag") {
        // The doubled path: IF_HBIN_SP is the FIFO's line reset there and puts
        // the picture where it wants it, so the lag is not the caller's to add.
        CHECK(VideoSourceLine::measured(1126, 160, 2250, 0, true).firstCapture() == 81);
    }
}

// A video standard states where active video begins as a position in its own
// line, counted from the hsync leading edge. This line is counted from whichever
// edge the chip triggered on, and delivers video a lag after it, so the two are
// not the same position.
TEST_CASE("a position in the source's line maps onto where video lands in this one")
{
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;
    const uint16_t Lag = VideoSourceLine::CaptureLagUnits;
    // 800x600@60: sync, back porch and border are 216 of its 1056 pixels.
    const float ActiveStart = 216.0f / 1056.0f;

    SUBCASE("a positive pulse moves it on by the lag alone") {
        // 230 by the arithmetic, 302 measured on the bench.
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, true)
                  .videoAt(ActiveStart) == 230 + Lag);
    }

    SUBCASE("an inverted pulse moves it back by the sync interval as well") {
        VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, Lag, false);
        CHECK(line.videoAt(ActiveStart) == 230 + Lag - line.syncUnits());
    }

    SUBCASE("a line placed by something else maps one to one") {
        // The vertical axis, and the doubled horizontal one.
        CHECK(VideoSourceLine(624).videoAt(0.5f) == 312);
    }
}

// The capture path writes a bounded number of units and then writes blanking,
// and it counts them FROM THE START OF THE WINDOW rather than from the start of
// the line. Measured by creeping the output blanking onto the band: 1035 units
// at a window starting on IF 491 and 1031 at one starting on 560, both on an
// undoubled line, against 1034 on a doubled one whose window started at 91.
// docs/investigations/tail-green.md
TEST_CASE("a capture window may not be wider than the path will write")
{
    SUBCASE("a line with room to spare is offered only what the path writes") {
        // An undoubled 2047-unit line: the ends alone would allow far more.
        VideoSourceLine line = VideoSourceLine::measured(
            2047, 248, 2046, VideoSourceLine::CaptureLagUnits, true);
        CHECK(line.capturable() > VideoSourceLine::CaptureWidthLimitUnits);
        CHECK(line.maxCaptureWidth() == VideoSourceLine::CaptureWidthLimitUnits);
    }

    SUBCASE("the bound is the same on a doubled line") {
        // The bench source one divider step past its cap, where the band was
        // reproduced: a 1270-unit line whose ends would allow 1119.
        VideoSourceLine line = VideoSourceLine::measured(1270, 181, 2540, 0, true);
        CHECK(line.maxCaptureWidth() == VideoSourceLine::CaptureWidthLimitUnits);
    }

    SUBCASE("a window the ends already bound is left alone") {
        VideoSourceLine narrow = VideoSourceLine::measured(900, 64, 900, 0, true);
        CHECK(narrow.maxCaptureWidth() == narrow.capturable());
    }
}
