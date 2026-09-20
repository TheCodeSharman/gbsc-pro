// Host-compiled unit tests for Tv5725::VideoSourceLine -- `make -C test input-line`.
// What of a line arrives intact: the hsync pulse at the head, and the write
// limit past which nothing is captured. docs/capture-limits.md.

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

using namespace Tv5725;

// IF_LINE_ST/SP is the input formatter's PROGRESSIVE line window -- line double
// timing, so deinterlacing's rather than the picture's -- and it has to span
// exactly one line from wherever it starts.
TEST_CASE("the progressive line window spans exactly one line")
{
    const VideoSourceLine SourceLine = VideoSourceLine::measured(1126, 160, 2250, true);

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
        CHECK(VideoSourceLine::measured(1057, 128, 2114, true).progressiveStop(64) == 1121);
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
    const VideoSourceLine SourceLine = VideoSourceLine::measured(LineUnits, HsyncLow, AdcLine, true);

    SUBCASE("the pulse width comes from the hsync duty") {
        CHECK(SourceLine.syncUnits() == 81);
    }

    SUBCASE("a wider pulse excludes proportionally more") {
        // 800x600@60 is hsync 128 of 1056, a duty of 0.121 -- nearly twice the
        // bench source's. A fixed guard would under-clip it.
        CHECK(VideoSourceLine::measured(1126, 128, 1056, true).syncUnits() == 137);
    }

    SUBCASE("an unmeasurable duty falls back to what the retimer is set for") {
        // HLOW_LEN is a live measurement and rails; the firmware discards a
        // reading outside 0.041..0.152 too (gbs-control.ino:4858). Failing open
        // would restore the green bands, so the fallback is the fraction
        // SP_RT_HS_SP = PLLAD_MD x 0.93 configures the retimer for.
        for (uint16_t railed : {(uint16_t)0, (uint16_t)4095, (uint16_t)10}) {
            // ceil(1126 x 0.07) = 79, against the 81 the duty measures.
            CHECK(VideoSourceLine::measured(1126, railed, 2250, true).syncUnits() == 79);
        }
    }

    SUBCASE("a line with nothing measured keeps all of itself") {
        CHECK(VideoSourceLine(1126).syncUnits() == 0);
        CHECK(VideoSourceLine(1126).firstCapture() == VideoSourceLine::FirstCapturableUnit);
        CHECK(VideoSourceLine(1126).lastCapture() == 1124);
    }
}

TEST_CASE("the capture stops where the line wraps, and nowhere earlier")
{
    // Neither of the last two units is a capture stop: `units` is the wrap
    // point and units - 1 is the line reset, where the input formatter stops
    // producing pixels at all.
    CHECK(VideoSourceLine(1277).lastCapture() == 1275);
    CHECK(VideoSourceLine(1126).lastCapture() == 1124);

    SUBCASE("the head guard still applies, and the two do not cross") {
        VideoSourceLine bench = VideoSourceLine::measured(1277, 181, 2553, true);
        CHECK(bench.firstCapture() < bench.lastCapture());
        CHECK(bench.lastCapture() == 1275);
        CHECK(bench.maxCaptureWidth() == bench.capturable());
    }
}

// Video does not reach the input formatter at the sync edge the line is counted
// from. Measured on four undoubled modes: the first active pixel lands ~72 units
// later than the sync-and-porch arithmetic places it, and one whole sync width
// earlier again where the hsync pulse is inverted, because the origin is then
// the pulse's trailing edge and the sync interval is already behind it.
// docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
// A doubled line was given no lag at all, on the reading that the line-double
// FIFO's own reset placed the picture. It does not: past the hsync pulse the
// capture path still writes blanking, and a window opened there captures it as
// the saturated green Y=U=V=0 decodes to.
//
// Measured on the bench RiscPC at 320x256@50, PLLAD_MD 2206, output 1080p, by
// stepping IF_HB_SP2 one unit at a time and counting green photo columns down
// the left edge:
//
//     IF_HB_SP2  82  88  90  92  94  95  96
//     green cols 27  19  15  12  10   6   0
//
// 82 is this line's firstCapture() plus the 3 units the framing origin was
// off zero, so the guard that creep asks for is 96 - 79 = 17.
//
// **A second creep at PLLAD_MD 2200 -- the same divider -- needs 20.** It reads
// the artefact as green above the blanking beside it rather than as a count of
// columns over a threshold, and it puts 17.9 units past the pulse at 24.1 and
// 26.6 where under 8 is clean, 19.9 at 6.0 and 21.9 at 1.3.
//
// The two do not contradict each other: a shortfall of a few CAPTURE units is
// magnified onto the output, so how many columns it covers depends on the
// framing each creep was taken at, and a residue that is sub-pixel at one
// magnification is several columns at another. The requirement is in capture
// units and the guard takes the larger of the two.
//
// docs/investigations/tail-green.md
TEST_CASE("a doubled line's capture clears the blanking the chip writes past the pulse")
{
    // The bench line: 1103 IF units, and HLOW_LEN 156 of an ADC line of 2206
    // is the 7.07% duty its hsync gives, which the round-up makes 79 units of
    // pulse. measured() recognises the doubling itself -- two ADC samples to
    // the unit.
    const uint16_t Units = 1103, HsyncLow = 156, AdcLine = 2206;

    VideoSourceLine line = VideoSourceLine::measured(
        Units, HsyncLow, AdcLine, true);

    CHECK(line.syncUnits() == 79);
    CHECK(line.firstCapture() == 79 + VideoSourceLine::DoubledHeadBlankingUnits);
}

// The blanking is written INTO the head of the line; the video behind it is
// where IF_HBIN_SP's own reset put it. So it bounds where a window may OPEN and
// displaces nothing -- which is what a lag does, and the difference is a default
// framing's worth of picture. Charged as a lag, every position in the line moves
// on by it: the bench default window ran 129..1083 and became 146..1100, keeping
// its extent and taking seventeen more units of the line's tail, where the
// source's picture has already stopped.
// docs/investigations/the-bar-at-the-right-edge-is-captured-line-tail.md
TEST_CASE("the blanking at a doubled line's head moves no position in it")
{
    const uint16_t Units = 1103, HsyncLow = 156, AdcLine = 2206;
    // Where AxisHorizontal puts active video on a source running no raster the
    // standards state. docs/vesa-gtf.md
    const float ActiveStart = 0.117f;

    VideoSourceLine line = VideoSourceLine::measured(
        Units, HsyncLow, AdcLine, true);

    CHECK(line.videoAt(ActiveStart) == 129);
}

TEST_CASE("the capture starts where the sync pulse ends")
{
    // 800x600@60 at PLLAD_MD 1124. HLOW_LEN 136 of 1124 is the 12.1% duty its
    // 128-of-1056 hsync gives, so 137 units of pulse.
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;

    SUBCASE("a positive pulse sits at the head and the floor clears it") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, true).firstCapture()
              == 137 + VideoSourceLine::CaptureLagUnits);
    }

    SUBCASE("an inverted pulse is behind the origin, leaving only the lag") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, false).firstCapture()
              == VideoSourceLine::CaptureLagUnits);
    }

    SUBCASE("an inverted pulse keeps the span the head guard would take") {
        VideoSourceLine positive = VideoSourceLine::measured(900, 109, 900, true);
        VideoSourceLine inverted = VideoSourceLine::measured(900, 109, 900, false);
        CHECK(inverted.capturable() - positive.capturable() == positive.syncUnits());
    }

    SUBCASE("a line whose origin is placed for it takes no lag") {
        // The doubled path: IF_HBIN_SP is the FIFO's line reset there and puts
        // the picture where it wants it, so the lag is not the caller's to add.
        // The blanking written past the pulse is a separate term and remains.
        CHECK(VideoSourceLine::measured(1126, 160, 2250, true).firstCapture()
              == 81 + VideoSourceLine::DoubledHeadBlankingUnits);
    }
}

// The capture path writes blanking past the hsync pulse on a doubled line, and
// a window opened inside it takes that blanking into the picture as saturated
// green. DoubledHeadBlankingUnits is what holds the window off it.
TEST_CASE("the doubled head guard clears the blanking the capture path writes")
{
    // The bench line at PLLAD_MD 2200: IF line 1101 and HLOW_LEN 156 put the
    // pulse at 78.1 units. Crept on the unit with the display window held
    // still, so only how much of the source's head is taken changes:
    //
    //     17.9 units past the pulse   green 24.1, 26.6   the picture is fouled
    //     19.9                        green  6.0         clean
    //     21.9                        green  1.3         clean
    //     25.9, 29.9                  green  0.4, 0.7    clean
    //
    // on a metric where under 8 is clean. What the guard costs is the source's
    // own blanking, which a full framing reaches into anyway, so the picture
    // does not shrink -- the solve refills the raster from a capture a few
    // units narrower.
    const uint16_t Units = 1101, HsyncLow = 156, AdcLine = 2200;
    VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, true);

    CHECK(line.firstCapture() - line.syncUnits() >= 22);
}

// A video standard states where active video begins as a position in its own
// line, counted from the hsync leading edge. This line is counted from whichever
// edge the chip triggered on, so the two are the same position only where that
// edge is the leading one.
TEST_CASE("a position in the source's line maps onto where video lands in this one")
{
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;
    // 800x600@60: sync, back porch and border are 216 of its 1056 pixels.
    const float ActiveStart = 216.0f / 1056.0f;

    SUBCASE("a positive pulse shares the standard's own origin") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, true)
                  .videoAt(ActiveStart) == 230);
    }

    SUBCASE("an inverted pulse moves it back by the sync interval as well") {
        VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, false);
        CHECK(line.videoAt(ActiveStart) == 230 - line.syncUnits());
    }

    SUBCASE("a line placed by something else maps one to one") {
        // The vertical axis, and the doubled horizontal one.
        CHECK(VideoSourceLine(624).videoAt(0.5f) == 312);
    }
}

// Nothing in the capture path bounds the width of a window. The bound this
// suite used to assert was the VDS's one-line delay painting the tail of the
// line green, which is a stage downstream of the capture and is now off.
// docs/investigations/the-tail-green-is-the-vds-line-filter.md
TEST_CASE("a capture window may be as wide as the line allows")
{
    SUBCASE("a line with room to spare is offered all of it") {
        VideoSourceLine line = VideoSourceLine::measured(
            2047, 248, 2046, true);
        CHECK(line.maxCaptureWidth() == line.capturable());
    }

    SUBCASE("and so is a doubled one") {
        VideoSourceLine line = VideoSourceLine::measured(1270, 160, 2540, true);
        CHECK(line.maxCaptureWidth() == line.capturable());
    }

    SUBCASE("a window the ends already bound is left alone") {
        VideoSourceLine narrow = VideoSourceLine::measured(900, 64, 900, true);
        CHECK(narrow.maxCaptureWidth() == narrow.capturable());
    }
}

// A REFUSED DUTY IS A FAULT, NOT A DEFAULT. FallbackDuty places the capture
// window from a guess, and on the bench source the guess happens to be right to
// one unit -- 7.03% against a fallback of 7.00% -- so a wrong reading on every
// OTHER mode is invisible from the picture. It has to say so.
TEST_CASE("a duty outside the sync processor's window is announced, not swallowed")
{
    SUBCASE("a duty the processor could not have measured says so") {
        g_logLines.clear();
        VideoSourceLine::forDuty(1253, 0.93f, true, true);
        CHECK(loggedContaining("duty"));
        CHECK(loggedContaining("refused"));
    }

    SUBCASE("a plausible duty says nothing") {
        g_logLines.clear();
        VideoSourceLine::forDuty(1253, 0.0703f, true, true);
        CHECK(g_logLines.empty());
    }
}

// A 100% framing exposes the whole of the source that is not synchronisation:
// back porch, border and picture alike. The only interval hidden is the hsync
// pulse, because that is the one part of the line the chip can MEASURE as not
// being image -- blanking is black active video and is undetectable. Whether
// the pulse sits at the head of the line is the polarity's to say: normalising
// inverts a high-active source, which swaps the edge the counter triggers on.
TEST_CASE("the capture floor hides the sync pulse and nothing else")
{
    // 640x480@60 on the bench at PLLAD_MD 1494, HLOW_LEN 172.
    const uint16_t Units = 1495, HsyncLow = 172, AdcLine = 1494;
    SUBCASE("a low-active source has the pulse behind the origin already") {
        CHECK(VideoSourceLine::measured(Units, HsyncLow, AdcLine, false)
                  .firstCapture() == VideoSourceLine::CaptureLagUnits);
    }

    SUBCASE("a high-active source has it at the head, so the floor clears it") {
        VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, true);
        CHECK(line.firstCapture() == line.syncUnits() + VideoSourceLine::CaptureLagUnits);
    }
}

// IF_HB_SP2 AT ZERO IS NOT A WINDOW THAT STARTS AT ZERO. Measured on the bench
// at 640x480@60, the one mode whose pulse is behind the origin and so whose
// floor is otherwise nothing: at 0 the picture is doubled and smeared, and at
// 1 it is clean, with every other register identical. The tail already keeps
// two units clear of the wrap for the same kind of reason.
TEST_CASE("the capture floor never reaches zero")
{
    // A line nothing has measured: no pulse, no lag, no head blanking, so the
    // floor is the clamp and nothing else.
    CHECK(VideoSourceLine(1126).firstCapture()
          == VideoSourceLine::FirstCapturableUnit);
}

// THE CAPTURE PATH DELIVERS VIDEO LATE, SO THE WINDOW MOVES WITH IT -- both
// ends. Added to the floor alone it narrows the window instead of translating
// it, which costs the head of the picture and leaves the tail short.
//
// Measured at 640x480@60 three ways that agree: the four-mode table gives
// 69..76 units, a capture-floor sweep puts the first content at unit 110..115,
// and RetroScaler-Acorn.mdf states a back porch of 22 of 800 -- 41 units --
// against a measured 116, leaving 75.
TEST_CASE("the whole window is translated by the lag, not just its floor")
{
    // 640x480@60 at PLLAD_MD 1494, low-active: the pulse is at the tail.
    const uint16_t Units = 1495, HsyncLow = 172, AdcLine = 1494;
    VideoSourceLine line = VideoSourceLine::measured(Units, HsyncLow, AdcLine, false);
    const uint16_t Lag = VideoSourceLine::CaptureLagUnits;

    SUBCASE("the floor is where the sync ends, a lag later") {
        CHECK(line.firstCapture() == Lag);
    }

    SUBCASE("the stop is the wrap, which the lag cannot move past") {
        // Video displaced past the counter's reset arrives at the head of the
        // next line, so the tail is bounded by the wrap and not by the lag.
        CHECK(line.lastCapture() == Units - 2);
    }

    SUBCASE("a doubled line is placed by IF_HBIN_SP and takes no lag") {
        VideoSourceLine doubled = VideoSourceLine::measured(1254, 89, 2506, true);
        CHECK(doubled.firstCapture()
              == doubled.syncUnits() + VideoSourceLine::DoubledHeadBlankingUnits);
    }
}
