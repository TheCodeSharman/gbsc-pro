// Host-compiled unit tests for Tv5725::CaptureWindow -- `make -C test capture-window`.
// The framing is held as state and every window is derived from it, so a pad
// press recomputes rather than inherits. docs/firmware-geometry-engine.md.
//
// `--dump` is intercepted before the test runner sees argv, and prints the
// windows this class derives, for inspection by hand.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "CheckNear.h"
#include "SketchSeam.h"
#include "fake/Wire.h"

// The bus the register-touching sources link against.
FakeTwoWire Wire;

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/CaptureWindow.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Axis.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/BlankingTiming.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoSourceLine.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/PanAndZoom.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Scale.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/SourceTiming.h"

// The line from a count in ADC samples and the divider it was counted at, which
// is the pair the chip reports. Their ratio is the duty; two samples to the unit
// is what says the line is doubled.
static Tv5725::VideoSourceLine measuredLine(uint16_t units, uint16_t hlowLen,
                                            uint16_t adcLine, bool syncAtHead)
{
    return Tv5725::VideoSourceLine::forDuty(
        units,
        Tv5725::HsyncPulse(adcLine > 0 ? (float)hlowLen / (float)adcLine : 0.0f,
                           syncAtHead),
        adcLine >= units + units / 2);
}

using namespace Tv5725;

// --- the bounds a line offers, which the framing does not move ---------------

// An unframed window on `line`, since where a window MAY sit is the line's
// business rather than the framing's. The other axis is given a line of its own.
static Tv5725::CaptureWindow boundsOn(const Tv5725::VideoSourceLine &line,
                                      const Tv5725::Axis &axis)
{
    return Tv5725::CaptureWindow(axis.vertical() ? Tv5725::VideoSourceLine(64) : line,
                                 axis.vertical() ? line
                                                 : Tv5725::VideoSourceLine::frame(8),
                                 Tv5725::SourceTiming(50.0f));
}

static uint16_t firstUnitOf(const Tv5725::VideoSourceLine &line,
                            const Tv5725::Axis &axis)
{
    return boundsOn(line, axis).firstUnitOn(axis);
}

static uint16_t reachOf(const Tv5725::VideoSourceLine &line, const Tv5725::Axis &axis)
{
    return boundsOn(line, axis).reachOn(axis);
}

static uint16_t capturableOf(const Tv5725::VideoSourceLine &line,
                             const Tv5725::Axis &axis)
{
    return boundsOn(line, axis).capturableOn(axis);
}

static uint16_t videoAtOf(const Tv5725::VideoSourceLine &line,
                          const Tv5725::Axis &axis, float lineFraction)
{
    return boundsOn(line, axis).videoAtOn(axis, lineFraction);
}

static float fractionAtOf(const Tv5725::VideoSourceLine &line,
                          const Tv5725::Axis &axis, uint16_t position)
{
    return boundsOn(line, axis).fractionAtOn(axis, position);
}

// --- the framing, held as state rather than read back ------------------------


// A window whose axis under test is placed on `line`. The other axis is given a
// line of its own, chosen so its width leaves the memory fits-check no reason to
// narrow the one being measured: eight lines allow a capture wider than any
// register can ask for.
static Tv5725::CaptureWindow windowFor(const Tv5725::VideoSourceLine &line,
                                       const Tv5725::SourceTiming &timing,
                                       const Tv5725::Axis &axis,
                                       const Tv5725::PanAndZoom &framing)
{
    Tv5725::CaptureWindow window(axis.vertical() ? Tv5725::VideoSourceLine(64) : line,
                                 axis.vertical() ? line : Tv5725::VideoSourceLine::frame(8),
                                 timing);
    window.setFraming(framing);
    return window;
}

static Tv5725::BlankingTiming windowOn(const Tv5725::CaptureWindow &window,
                                       const Tv5725::Axis &axis)
{
    return axis.vertical() ? window.vertical() : window.horizontal();
}

// The window an axis nobody has framed yet lands on.
static Tv5725::BlankingTiming defaultWindowOn(const Tv5725::VideoSourceLine &line,
                                              const Tv5725::SourceTiming &timing,
                                              const Tv5725::Axis &axis)
{
    return windowOn(windowFor(line, timing, axis, Tv5725::PanAndZoom()), axis);
}

// The framing this line can realise, which is what a solve keeps.
static Tv5725::PanAndZoom clampedTo(const Tv5725::VideoSourceLine &line,
                                    const Tv5725::SourceTiming &timing,
                                    const Tv5725::Axis &axis,
                                    const Tv5725::PanAndZoom &framing = Tv5725::PanAndZoom())
{
    return windowFor(line, timing, axis, framing).framing();
}

// A press, as VideoPath makes one: an axis nobody has framed yet is seeded from
// the line's own default first, so the press lands on this mode's grid -- which
// is what makes one press one unit, and a press with its inverse return the
// same framing.
static Tv5725::PanAndZoom press(const Tv5725::VideoSourceLine &line,
                                const Tv5725::SourceTiming &timing,
                                const Tv5725::Axis &axis,
                                int16_t zoomUnits, int16_t panUnits)
{
    Tv5725::PanAndZoom moved = clampedTo(line, timing, axis);
    if (zoomUnits != 0)
        moved.zoomBy(axis, zoomUnits, line.units(), reachOf(line, axis));
    if (panUnits != 0)
        moved.panBy(axis, panUnits, line.units(), reachOf(line, axis));
    return moved;
}

static Tv5725::BlankingTiming captureOf(const Tv5725::VideoSourceLine &line,
                                        const Tv5725::SourceTiming &timing,
                                        const Tv5725::Axis &axis,
                                        const Tv5725::PanAndZoom &framing)
{
    return windowOn(windowFor(line, timing, axis, framing), axis);
}

// A press onto a framing already held, clamped to what the line allows -- the
// state a solve carries from one press to the next.
static Tv5725::PanAndZoom pressedOn(const Tv5725::VideoSourceLine &line,
                                    const Tv5725::SourceTiming &timing,
                                    const Tv5725::Axis &axis,
                                    int16_t zoomUnits, int16_t panUnits,
                                    const Tv5725::PanAndZoom &from)
{
    Tv5725::PanAndZoom moved = from.tunedOn(axis) ? from
                                                  : clampedTo(line, timing, axis, from);
    if (zoomUnits != 0)
        moved.zoomBy(axis, zoomUnits, line.units(), reachOf(line, axis));
    if (panUnits != 0)
        moved.panBy(axis, panUnits, line.units(), reachOf(line, axis));
    return clampedTo(line, timing, axis, moved);
}

// The framing is proportions now, so a test that means "40 units of zoom" has
// to say so in units against a line.
static Tv5725::BlankingTiming captureFor(const Tv5725::VideoSourceLine &line,
                                         const Tv5725::SourceTiming &timing,
                                         const Tv5725::Axis &axis,
                                         int16_t zoomUnits, int16_t panUnits)
{
    return windowOn(windowFor(line, timing, axis,
                              press(line, timing, axis, zoomUnits, panUnits)), axis);
}

TEST_CASE("the framing is held as state and the window is derived")
{
    BlankingTiming wide = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0);
    BlankingTiming centred = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0);

    SUBCASE("the default framing takes the default capture width") {
        BlankingTiming got = defaultWindowOn(VideoSourceLine(1126), 50.0f, AxisHorizontal);
        CHECK(got.start() - got.stop() == CaptureWindow::defaultWidth(VideoSourceLine(1126), 50.0f, AxisHorizontal));
    }

    SUBCASE("one unit of zoom is one unit of capture") {
        // The point of the absolute framing: a tap has to mean one pixel, and a
        // proportional control cannot. 6% of an 890 unit capture is 53 units,
        // and a ratio small enough to give 1 there rounds to nothing at a
        // narrow capture.
        for (int16_t units : {1, 2, 7, 40, 300}) {
            BlankingTiming in = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, units, 0);
            CHECK((wide.start() - wide.stop()) - (in.start() - in.stop()) == units);
        }
    }

    SUBCASE("one unit is one unit at a narrow capture too") {
        BlankingTiming narrow = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 800, 0);
        BlankingTiming narrower = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 801, 0);
        CHECK((narrow.start() - narrow.stop()) - (narrower.start() - narrower.stop())
              == 1);
    }

    SUBCASE("zoom out and back returns the window exactly") {
        // Integer units, so this is exact by construction rather than by the
        // rounding happening to cancel.
        BlankingTiming there = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 137, 0);
        BlankingTiming back = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0);
        CHECK(((back.stop() == wide.stop()) && (back.start() == wide.start())));
        CHECK(there.start() - there.stop() < wide.start() - wide.stop());
    }

    SUBCASE("panning moves the window and keeps its width") {
        // Zoomed first, because the default window is wide enough that a pan of
        // 40 units runs into the end of the line and is clamped there.
        BlankingTiming cropped = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 200, 0);
        BlankingTiming moved = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 200, +40);
        CHECK(moved.stop() == cropped.stop() + 40);
        CHECK(moved.start() - moved.stop() == cropped.start() - cropped.stop());
    }

    SUBCASE("zooming moves the far edge and leaves the near one alone") {
        // The pan places the corner and the zoom sizes the rectangle: two
        // orthogonal controls, so a framing is found in one pass of each. A
        // zoom that kept the CENTRE moved the corner the pan had just placed,
        // and neither control converged.
        BlankingTiming placed = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 200, 0);
        BlankingTiming tighter = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 260, 0);
        CHECK(tighter.stop() == placed.stop());
        CHECK((placed.start() - placed.stop()) - (tighter.start() - tighter.stop())
              == 60);
    }

    SUBCASE("a pan is clamped to the line rather than crossing it") {
        BlankingTiming far_right = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, +5000);
        CHECK(far_right.start() <= 1126);
        CHECK(far_right.start() - far_right.stop() == centred.start() - centred.stop());
        BlankingTiming far_left = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, -5000);
        CHECK(far_left.stop() == CaptureWindow::FirstCapturableUnit);
        CHECK(far_left.start() - far_left.stop() == centred.start() - centred.stop());
    }

    SUBCASE("a zoom in never crops the capture away to nothing") {
        BlankingTiming tiny = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 5000, 0);
        CHECK(tiny.start() - tiny.stop() >= MinimumCapture);
    }

    SUBCASE("zooming out never puts the capture stop on the wrap point") {
        // IF_VB_ST rolls at the frame it is counted on and does not clamp, so
        // a window written onto that value rolls the frame -- which reads as the
        // picture jumping rather than as a capture fault. Three steps of
        // zoom-out reach it: 0..624 of a 624-unit frame.
        for (int16_t units : {-1, -20, -60, -100, -500, -5000}) {
            BlankingTiming w = captureFor(VideoSourceLine(624), 50.0f, AxisVertical, units, 0);
            CHECK(w.start() <= 623);
        }
    }

    SUBCASE("the same wrap bound applies horizontally") {
        for (int16_t units : {-1, -60, -200, -300, -5000}) {
            BlankingTiming w = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, units, 0);
            CHECK(w.start() <= 1276);
        }
    }

    SUBCASE("a pan cannot put the capture stop on the wrap point either") {
        // pan_capture() bounds it the same way, for the same reason.
        for (int16_t p : {+5000, +600, -5000}) {
            CHECK(captureFor(VideoSourceLine(624), 50.0f, AxisVertical, 0, p).start()
                  <= reachOf(VideoSourceLine(624), AxisVertical));
            CHECK(captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, p).start()
                  <= reachOf(VideoSourceLine(1126), AxisHorizontal));
        }
    }

    SUBCASE("the capture stop never lands on the wrap point itself") {
        for (int16_t p : {+5000, +600, +132}) {
            CHECK(captureFor(VideoSourceLine(1265), 50.0f, AxisHorizontal, 0, p).start()
                  <= reachOf(VideoSourceLine(1265), AxisHorizontal));
        }
    }

    SUBCASE("a zoom out never runs past the line") {
        BlankingTiming huge = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, -5000, 0);
        CHECK(huge.start() <= 1126);
        CHECK(huge.stop() <= huge.start());
    }

    SUBCASE("the vertical axis derives from its own frame the same way") {
        // The vertical pair carries captureMargin at each end of the picture,
        // so the span is the default width plus both margins and the near edge
        // opens that far ahead of where the picture starts.
        const long Margin = AxisVertical.captureMargin();
        BlankingTiming v = captureFor(VideoSourceLine(624), 50.0f, AxisVertical, 0, 0);
        CHECK(v.start() - v.stop()
              == CaptureWindow::defaultWidth(VideoSourceLine(624), 50.0f, AxisVertical)
                     + 2 * Margin);
        CHECK_NEAR((int)v.stop(), AxisVertical.activeStart() * 624.0f - Margin, 1.0);
    }
}

// capture() clamps the WINDOW, and the FRAMING has to be clamped with it, or a
// press past the edge leaves the framing beyond anything achievable and every
// smaller press back produces an identical window -- the control dies in that
// direction. Only the accelerating hold ramp presses that far.
//
// On the bench line: 1126 units, default width 890, so the centred start is 118
// and the largest the line allows is 1124 - 890 = 234, giving a largest
// achievable horizontalPan of 116.
TEST_CASE("a source running a published raster is captured where that raster puts picture")
{
    // 640x480@60 spends 18.0% of the line reaching picture and 80.0% of it on
    // picture, and nothing on this chip can measure that -- so a source running
    // a raster the standards state is placed from the standard rather than from
    // the assumption an unrecognised one takes.
    const VideoSourceLine line(1126);
    const SourceTiming dmt = SourceTiming::matching(SourceKey(524, 59.94f, 96.0f / 800.0f, SourceKey::Negative, SourceKey::Negative));

    BlankingTiming got = defaultWindowOn(line, dmt, AxisHorizontal);

    CHECK_NEAR(got.stop(), 0.180f * 1126.0f, 1.0f);
    CHECK_NEAR(got.width(), 0.800f * 1126.0f, 1.0f);
}

TEST_CASE("a source matching no published raster is captured across the envelope")
{
    // Nothing can measure where a border ends and back porch begins, so a
    // source running no raster the standards state is captured across the
    // widest active region any real source puts on a line: black edges are
    // visible and one press trims them, where a cropped edge looks like a
    // fault. Measured against stock AKF50, whose 15 kHz modes put picture
    // between 15.4% and 90.4% of the line and 7.1% and 99.4% of the frame.
    const VideoSourceLine line(1126);

    BlankingTiming got = defaultWindowOn(line, 50.0f, AxisHorizontal);
    CHECK_NEAR(got.stop(), 0.117f * 1126.0f, 1.0f);
    CHECK_NEAR(got.start(), 0.981f * 1126.0f, 1.0f);

    SUBCASE("and the vertical envelope does not split on field rate") {
        BlankingTiming fifty = defaultWindowOn(VideoSourceLine(624), 50.0f, AxisVertical);
        BlankingTiming sixty = defaultWindowOn(VideoSourceLine(624), 60.0f, AxisVertical);
        CHECK(fifty.stop() == sixty.stop());
        CHECK(fifty.start() == sixty.start());
        const float Margin = (float)AxisVertical.captureMargin();
        CHECK_NEAR(fifty.stop(), 0.061f * 624.0f - Margin, 1.0f);
        CHECK_NEAR(fifty.start(), 0.994f * 624.0f + Margin, 1.0f);
    }
}

TEST_CASE("a press that overshoots the edge leaves no dead zone")
{
    const uint16_t Units = 1126;
    const float Rate = 50.0f;

    SUBCASE("the capture floor is the line's, not the output raster's") {
        // Cropping past what the magnification can put back letterboxes the
        // picture -- fitToRaster clamps the scale and produced shrinks -- rather
        // than stopping the control. The only floor left is the one that stops a
        // press cropping to nothing, and it is a property of the line, so the
        // same framing crops the same amount whatever the output is doing.
        const VideoSourceLine frame(623);
        // hard against the stop
        const PanAndZoom f = pressedOn(frame, Rate, AxisVertical, 5000, 0, PanAndZoom());
        BlankingTiming got = captureOf(frame, Rate, AxisVertical, f);
        CHECK(got.start() - got.stop()
              == MinimumCapture + 2 * AxisVertical.captureMargin());
    }

    SUBCASE("an overshooting pan is brought back to what the line allows") {
        const VideoSourceLine line(Units);
        const PanAndZoom f = pressedOn(line, Rate, AxisHorizontal, 0, 200, PanAndZoom());
        // The framing is a proportion now, so the reachable edge is asserted on
        // the window it lands on rather than on the units behind it.
        BlankingTiming pinned = captureOf(line, Rate, AxisHorizontal, f);
        CHECK(pinned.start() == reachOf(line, AxisHorizontal));
    }

    SUBCASE("and one unit back then actually moves the window") {
        const VideoSourceLine line(Units);
        PanAndZoom f = pressedOn(line, Rate, AxisHorizontal, 0, 200, PanAndZoom());
        BlankingTiming at_edge = captureOf(line, Rate, AxisHorizontal, f);

        f = pressedOn(line, Rate, AxisHorizontal, 0, -1, f);
        BlankingTiming back = captureOf(line, Rate, AxisHorizontal, f);
        CHECK(back.stop() < at_edge.stop());
    }

    SUBCASE("the same holds at the other end of the line") {
        const VideoSourceLine line(Units);
        PanAndZoom f = pressedOn(line, Rate, AxisHorizontal, 0, -200, PanAndZoom());
        CHECK(captureOf(line, Rate, AxisHorizontal, f).stop()
              == firstUnitOf(line, AxisHorizontal));

        BlankingTiming at_edge = captureOf(line, Rate, AxisHorizontal, f);
        f = pressedOn(line, Rate, AxisHorizontal, 0, +1, f);
        CHECK(captureOf(line, Rate, AxisHorizontal, f).stop() > at_edge.stop());
    }

    SUBCASE("vertically too, which is the 'or bottom' half of the report") {
        // The margin takes the near edge past FirstCapturableUnit, so at the
        // floor the pair opens on the counter's origin and the press back is
        // seen at the FAR edge: the near one has nowhere left to go.
        const VideoSourceLine frame(624);
        PanAndZoom f = pressedOn(frame, Rate, AxisVertical, 0, -400, PanAndZoom());
        BlankingTiming at_edge = captureOf(frame, Rate, AxisVertical, f);
        CHECK(at_edge.stop() == 0);

        f = pressedOn(frame, Rate, AxisVertical, 0, +1, f);
        CHECK(captureOf(frame, Rate, AxisVertical, f).start() > at_edge.start());
    }

    SUBCASE("an overshooting zoom is brought back the same way") {
        // clampWidth() pins the width at MinimumCapture, and the same dead zone
        // forms in horizontalZoom_ if the framing is left holding the overshoot.
        const VideoSourceLine line(Units);
        PanAndZoom f = pressedOn(line, Rate, AxisHorizontal, 5000, 0, PanAndZoom());
        BlankingTiming tightest = captureOf(line, Rate, AxisHorizontal, f);
        CHECK(tightest.start() - tightest.stop() == MinimumCapture);

        f = pressedOn(line, Rate, AxisHorizontal, -1, 0, f);
        BlankingTiming wider = captureOf(line, Rate, AxisHorizontal, f);
        CHECK(wider.start() - wider.stop() > tightest.start() - tightest.stop());
    }

    SUBCASE("clamping a reachable framing again changes nothing") {
        // The first clamp puts the proportions on this line's grid, which a
        // hand-written pair is not on. Every clamp after it must be a no-op, or
        // the window walks a unit at a time each time the mode is re-applied.
        CaptureWindow window(VideoSourceLine(Units), VideoSourceLine(624), Rate);
        window.setFraming(PanAndZoom(0.10f, 0.78f, 0.09f, 0.80f));
        const PanAndZoom settled = window.framing();

        window.setFraming(settled);
        CHECK(window.framing() == settled);
    }
}

// The line doubler's own window, IF_LINE_ST and IF_LINE_SP. It is the line
// double timing rather than the picture -- it has to span exactly one line, so
// the stop follows the line length and may roll past it. The line length moves
// with PLLAD_MD, so a constant stop sizes the window for whichever line it was
// picked against.
TEST_CASE("the progressive window spans exactly one line")
{
    const VideoSourceLine line(1126);
    const CaptureWindow window(line, VideoSourceLine::frame(628), 50.0f);

    CHECK(window.progressiveWindow().stop() == CaptureWindow::ProgressiveStart);
    CHECK(window.progressiveWindow().start() - window.progressiveWindow().stop()
          == line.units());

    SUBCASE("and it follows the line rather than a constant") {
        const CaptureWindow shorter(VideoSourceLine(563),
                                    VideoSourceLine::frame(628), 50.0f);
        CHECK(shorter.progressiveWindow().start()
              < window.progressiveWindow().start());
    }

    SUBCASE("it may run past the end of the line, and that is not a fault") {
        // A stop of 1190 on a 1126 unit line was once reported as a stray
        // write. It is a stop measured from a start, not a position within the
        // raster, so it rolls.
        CHECK(window.progressiveWindow().start() > line.units());
    }
}

// The capture path drops a unit at each end of the vertical window, so a window
// opened on the picture loses the source's first and last lines. The pair the
// registers take therefore carries the axis's margin at each end, floored at the
// counter's origin and clamped to the last unit before it wraps.
TEST_CASE("the window each axis reports is the register pair")
{
    const long Margin = AxisVertical.captureMargin();
    const VideoSourceLine frame = VideoSourceLine::frame(628);

    SUBCASE("the vertical pair carries a margin at each end of the picture") {
        const PanAndZoom framing(0.0f, 1.0f, 100.0f / 628.0f, 400.0f / 628.0f);
        const BlankingTiming got = captureOf(frame, 60.0f, AxisVertical, framing);
        CHECK(got.start() - got.stop() == 400 + 2 * Margin);
    }

    SUBCASE("the horizontal pair carries none, which is its axis's margin") {
        REQUIRE(AxisHorizontal.captureMargin() == 0);
        const VideoSourceLine line(1126);
        const PanAndZoom framing(100.0f / 1126.0f, 400.0f / 1126.0f, 0.0f, 1.0f);
        const BlankingTiming got = captureOf(line, 50.0f, AxisHorizontal, framing);
        CHECK(got.start() - got.stop() == 400);
    }

    SUBCASE("the near end floors at the counter's origin") {
        const PanAndZoom framing(0.0f, 1.0f, 0.0f, 400.0f / 628.0f);
        const BlankingTiming got = captureOf(frame, 60.0f, AxisVertical, framing);
        CHECK(got.stop() == 0);
    }

    SUBCASE("the far end clamps at the last unit before the wrap") {
        const PanAndZoom framing(0.0f, 1.0f, 0.0f, 1.0f);
        const BlankingTiming got = captureOf(frame, 60.0f, AxisVertical, framing);
        CHECK(got.start() == reachOf(frame, AxisVertical));
    }
}

// The pulse is at the HEAD, and its width is derived from HLOW_LEN over
// PLLAD_MD rather than held as a constant. The tail is deliberately unbounded.
// docs/scaler-geometry-model.md "The two green regions in an IF line".
TEST_CASE("the capture window never takes the hsync pulse")
{
    // The bench RiscPC: a 7.1% hsync duty, HLOW_LEN 181 of PLLAD_MD 2553, read
    // here at the 2250 the write limit caps the divider to.
    // 160 x 1126 / 2250 = 80.07 -> 81.
    const uint16_t HsyncLow = 160, AdcLine = 2250, LineUnits = 1126;
    const VideoSourceLine SourceLine = measuredLine(LineUnits, HsyncLow, AdcLine, true);
    const float Rate = 50.0f;

    SUBCASE("zooming all the way out stops clear of the sync") {
        BlankingTiming huge = captureFor(SourceLine, Rate, AxisHorizontal, -5000, 0);
        CHECK(huge.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("and widens to the end of the line, leaving the near edge alone") {
        // The zoom moves the FAR edge and nothing else, so it reaches the last
        // unit the path can write and stops. Opening the near edge is the pan's
        // job. docs/investigations/tail-green.md
        BlankingTiming rest = defaultWindowOn(SourceLine, Rate, AxisHorizontal);
        BlankingTiming huge = captureFor(SourceLine, Rate, AxisHorizontal, -5000, 0);
        CHECK(huge.stop() == rest.stop());
        CHECK(huge.start() == reachOf(SourceLine, AxisHorizontal));
    }

    SUBCASE("panning to the left stop cannot walk into the sync") {
        BlankingTiming left = captureFor(SourceLine, Rate, AxisHorizontal, 0, -5000);
        CHECK(left.stop() >= SourceLine.syncUnits());
    }

    SUBCASE("panning to the right stop reaches the last unit before the reset") {
        BlankingTiming right = captureFor(SourceLine, Rate, AxisHorizontal, 0, +5000);
        CHECK(right.start() == reachOf(SourceLine, AxisHorizontal));
    }

    SUBCASE("the resting picture is untouched") {
        // The default capture is 890 units of a 1126 unit line and the pulse
        // takes 81 at the head, so 1045 remain: the picture nobody complained
        // about must not move by so much as a unit.
        BlankingTiming guarded = defaultWindowOn(SourceLine, Rate, AxisHorizontal);
        BlankingTiming whole = defaultWindowOn(VideoSourceLine(LineUnits), Rate, AxisHorizontal);
        CHECK(guarded.stop() == whole.stop());
        CHECK(guarded.start() == whole.start());
    }

    SUBCASE("zoom out still has somewhere to go") {
        // Clipping the sync must not cost the reach that finds the active video
        // the default extent crops. The far edge is the one the zoom has, so
        // the reach is what stands between the picture and the end of the line.
        BlankingTiming rest = defaultWindowOn(SourceLine, Rate, AxisHorizontal);
        BlankingTiming out = captureFor(SourceLine, Rate, AxisHorizontal, -10, 0);
        CHECK(out.start() - out.stop() == (rest.start() - rest.stop()) + 10);
    }

    SUBCASE("the framing is clamped to the same bound the window is") {
        // The window is clamped and the framing with it; a
        // difference of one unit between them is a dead zone.
        PanAndZoom f = pressedOn(SourceLine, Rate, AxisHorizontal, 0, -5000, PanAndZoom());
        BlankingTiming at_edge = captureOf(SourceLine, Rate, AxisHorizontal, f);
        CHECK(at_edge.stop() == firstUnitOf(SourceLine, AxisHorizontal));

        f = pressedOn(SourceLine, Rate, AxisHorizontal, 0, +1, f);
        CHECK(captureOf(SourceLine, Rate, AxisHorizontal, f).stop() > at_edge.stop());
    }
}

// --- a capture window when there is not a usable one --------------------------

TEST_CASE("a nonsense capture is replaced, not trusted")
{
    // The stock preset commonly leaves IF_VB_ST <= IF_VB_SP, and what the chip
    // means by that is not established. The engine computes a window rather
    // than decoding one.
    BlankingTiming w = captureFor(VideoSourceLine(1126), 50.0f, AxisHorizontal, 0, 0);

    SUBCASE("a default capture is placed on the line, not centred in it") {
        // Active video is never centred: sync plus back porch runs far longer
        // than the front porch, so a centred window is offset left of the
        // picture and crops its far edge.
        CHECK(w.start() > w.stop());
        CHECK_NEAR((int)w.stop(), AxisHorizontal.activeStart() * 1126.0f, 1.0);
        CHECK((int)w.start() > 1126 - (int)w.stop());
    }

    SUBCASE("a default capture over-captures rather than cropping") {
        // Black edges are visible and adjustable; a cropped edge looks like a
        // tuning fault and sends you hunting for a problem that is not there.
        CHECK(w.start() - w.stop() > 1126 * 0.76);
    }

    SUBCASE("a default capture never exceeds the line it sits in") {
        for (uint16_t units : {64, 256, 624, 1277, 2559}) {
            BlankingTiming any = captureFor(VideoSourceLine(units), 50.0f, AxisHorizontal, 0, 0);
            CHECK(any.start() <= units);
            CHECK(any.start() > any.stop());
        }
    }

    SUBCASE("the default is one envelope, whatever the field rate") {
        // The envelope contains what every real source puts on a line, so
        // splitting it by field rate would only make one of the two crop.
        CHECK(CaptureWindow::defaultWidth(VideoSourceLine(624), 60.0f, AxisVertical)
              == CaptureWindow::defaultWidth(VideoSourceLine(624), 50.0f, AxisVertical));
        CHECK(CaptureWindow::defaultWidth(VideoSourceLine(1126), 50.0f, AxisHorizontal)
              == CaptureWindow::defaultWidth(VideoSourceLine(1126), 60.0f, AxisHorizontal));
    }

    SUBCASE("a line of zero yields nothing rather than a wrapped window") {
        BlankingTiming none = captureFor(VideoSourceLine(0), 50.0f, AxisHorizontal, 0, 0);
        CHECK(((none.stop() == 0) && (none.start() == 0)));
    }
}

TEST_CASE("no framing puts the capture stop past what the line can write")
{
    const uint16_t lines[] = {624, 1126, 1265, 1277, 2250};
    const int16_t pans[] = {0, +40, +600, +5000, -600, -5000};
    const int16_t zooms[] = {0, +137, +800, +5000, -40, -5000};

    for (uint16_t units : lines) {
        for (bool vertical : {false, true}) {
            const VideoSourceLine line = vertical ? VideoSourceLine(units)
                                            : measuredLine(units, 181, 2553, true);
            const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
            CAPTURE(units);
            CAPTURE(vertical);
            CAPTURE(reachOf(line, axis));

            for (int16_t p : pans) {
                for (int16_t z : zooms) {
                    CAPTURE(p);
                    CAPTURE(z);
                    const BlankingTiming got = captureFor(line, 50.0f, axis, z, p);

                    CHECK(got.start() <= reachOf(line, axis));
                    // The margin is the only thing allowed to reach ahead of
                    // the first capturable unit; the picture behind it is not.
                    CHECK(got.stop() + axis.captureMargin()
                          >= firstUnitOf(line, axis));
                }
            }
        }
    }
}

// `--dump` prints the derived windows for inspection by hand.
static void dumpGrid()
{
    // The horizontal default, which geometry_math has a twin for. There is no
    // Python vertical equivalent, so that half is covered by the host tests only.
    for (uint16_t units : {320, 624, 1277, 2048, 2559})
        for (float rate : {50.0f, 60.0f}) {
            BlankingTiming w = defaultWindowOn(VideoSourceLine(units), rate, AxisHorizontal);
            std::printf("default %u %.0f %u %u\n", units, rate, w.stop(), w.start());
        }

    // The window the clamp and the placement agree on, which is the one a solve
    // writes: they come from one call, and a divergence would show as the
    // framing walking a unit every time the mode is re-applied.
    for (uint16_t units : {320, 624, 1277, 2048})
        for (int16_t zoom : {0, 40, 200, -60})
            for (int16_t pan : {0, 50, 400, -400})
                for (int vertical = 0; vertical < 2; ++vertical) {
                    const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
                    VideoSourceLine line(units, vertical ? 0 : (uint16_t)(units / 14));

                    // Seed the untuned default for this line, then move it by the
                    // same units the old four-integer framing carried, so the
                    // window columns stay comparable across the change.
                    PanAndZoom moved = clampedTo(line, 50.0f, axis);
                    moved.zoomBy(axis, zoom, capturableOf(line, axis));
                    moved.panBy(axis, pan, capturableOf(line, axis));

                    BlankingTiming placed = captureOf(line, 50.0f, axis, moved);

                    long width = (long)placed.start() - (long)placed.stop();
                    long zoomUnits = (long)CaptureWindow::defaultWidth(line, 50.0f, axis) - width;
                    long panUnits = (long)placed.stop() - (long)(line.units() - width) / 2;

                    std::printf("framing %u %d %d %d %d %d %u %u\n",
                                units, zoom, pan, vertical,
                                (int)zoomUnits, (int)panUnits,
                                placed.stop(), placed.start());
                }
}

int main(int argc, char **argv)
{
    // Before the test runner, which exits non-zero on an option it does not
    // know.
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dumpGrid();
        return 0;
    }
    return doctest::Context(argc, argv).run();
}

// The default framing is where the picture sits before anyone frames it, and it
// comes from the standard's own active start -- a position in the SOURCE's line.
// Placing it at that fraction of the IF line assumes the two lines share an
// origin, which they do not.
// docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md
TEST_CASE("the default capture starts where video lands, not where the standard states it")
{
    // 800x600@60 at PLLAD_MD 1124: HLOW_LEN 138 of 1124 is its 12.1% duty.
    const uint16_t Units = 1125, HsyncLow = 138, AdcLine = 1124;
    const float Rate = 60.0f;

    SUBCASE("an inverted pulse puts it a sync width the other way") {
        VideoSourceLine positive = measuredLine(Units, HsyncLow, AdcLine, true);
        VideoSourceLine inverted = measuredLine(Units, HsyncLow, AdcLine, false);
        BlankingTiming at_head = defaultWindowOn(positive, Rate, AxisHorizontal);
        BlankingTiming behind = defaultWindowOn(inverted, Rate, AxisHorizontal);
        CHECK(at_head.stop() - behind.stop()
              == positive.syncUnits() - CaptureWindow::FirstCapturableUnit);
    }
}

TEST_CASE("a framing survives a round trip through a coarser capture grid")
{
    // An output too short for a doubled frame turns the line doubler off, and
    // the vertical capture then counts whole source lines where it counted
    // half-lines -- 621 units becomes 309 on the bench source. The framing is a
    // proportion, so it carries across; what must not happen is the coarse
    // grid's quantisation being written back as the user's framing, because
    // then the fine grid can no longer express what they set.
    //
    // Measured on the bench before this held: 1080p ev 513, 480p ev 255, and
    // 1080p ev 512 on the way back. One unit lost per excursion.
    const VideoSourceLine doubled(624);      // half-lines
    const VideoSourceLine single(312);       // whole source lines

    // The pair carries a margin at each end, so the picture is the span less
    // both of them -- and it is the picture the framing is a proportion of.
    const long Margin = AxisVertical.captureMargin();
    PanAndZoom framing(0.0f, 1.0f, 62.0f / 624.0f, 513.0f / 624.0f);

    const BlankingTiming fine = captureOf(doubled, 50.0f, AxisVertical, framing);
    CHECK(fine.start() - fine.stop() - 2 * Margin == 513);

    framing = clampedTo(single, 50.0f, AxisVertical, framing);
    const BlankingTiming coarse = captureOf(single, 50.0f, AxisVertical, framing);
    // 513 half-lines is 256.5
    CHECK(coarse.start() - coarse.stop() - 2 * Margin == 256);

    framing = clampedTo(doubled, 50.0f, AxisVertical, framing);
    const BlankingTiming back = captureOf(doubled, 50.0f, AxisVertical, framing);
    CHECK(back.start() - back.stop() - 2 * Margin == 513);
}

TEST_CASE("one framing takes the same span of the line in either scan mode")
{
    // The capture path excludes DoubledHeadBlankingUnits at the head of a
    // doubled line and CaptureLagUnits at the head of an undoubled one, and
    // counts the sync pulse in units of different size. So what is left to
    // capture is not the same span of source in the two modes -- 9.2%..99.8%
    // of the line doubled against 11.0%..99.9% undoubled on the bench source.
    //
    // A framing anchored to that span therefore names a different part of the
    // picture in each: photographed at one framing, 480p fitted at 0.980 of
    // the 1080p frame horizontally and 576p at 0.962. Anchored to the LINE it
    // names the same part, because the line is the same either way.
    const VideoSourceLine doubled = VideoSourceLine::forDuty(1100, HsyncPulse(0.0718f, true), true);
    const VideoSourceLine single = VideoSourceLine::forDuty(1881, HsyncPulse(0.0718f, true), false);

    const PanAndZoom framing(0.2036f, 0.6245f, 0.0f, 1.0f);

    const BlankingTiming fine = captureOf(doubled, 50.0f, AxisHorizontal, framing);
    const BlankingTiming coarse = captureOf(single, 50.0f, AxisHorizontal, framing);

    CHECK((coarse.start() - coarse.stop()) / 1881.0
          == doctest::Approx((fine.start() - fine.stop()) / 1100.0).epsilon(0.004));
}

// A PROPORTION NAMES A POSITION IN THE SOURCE'S VIDEO, AND THE TWO SCAN MODES
// PUT THAT VIDEO IN DIFFERENT PLACES IN THE COUNTER. Anchoring the proportion
// to units() makes the two scales agree; it does nothing about the origins,
// and the origins differ because the capture path delivers video a lag behind
// the counter on an undoubled line and IF_HBIN_SP's FIFO reset places it ahead
// of the counter on a doubled one.
//
// Measured on the bench source at one stored framing: the same source content
// sits at counter fraction 0.2046 with the doubler in and 0.2674 with it
// bypassed, 6.28% of a line apart. Panning 480p right by exactly those 118
// units collapsed the fitted displacement against the 1080p frame from
// +130.7 photo px to -26.2, and left the fitted scale untouched at 1.048.
TEST_CASE("one framing names the same source video in either scan mode")
{
    // 1080p doubles the bench source at PLLAD_MD 2200; 480p cannot fit the
    // doubled frame and captures it whole at 1880.
    const VideoSourceLine doubled = VideoSourceLine::forDuty(1100, HsyncPulse(0.0718f, true), true);
    const VideoSourceLine single = VideoSourceLine::forDuty(1880, HsyncPulse(0.0718f, true), false);

    const PanAndZoom framing(0.2036f, 0.6245f, 0.0f, 1.0f);

    const uint16_t fine = captureOf(doubled, 50.0f, AxisHorizontal, framing).stop();
    const uint16_t coarse = captureOf(single, 50.0f, AxisHorizontal, framing).stop();

    CHECK(fractionAtOf(single, AxisHorizontal, coarse)
          == doctest::Approx(fractionAtOf(doubled, AxisHorizontal, fine))
                 .epsilon(0.001));
}

// --- where a window may sit in the counter -----------------------------------

TEST_CASE("the capture stops where the line wraps, and nowhere earlier")
{
    // `units` is the wrap point, so the last unit a window may stop on is the
    // one before it. That unit holds frame: the tail of an undoubled line is
    // the front porch, and stopping a unit earlier loses a sample of it.
    CHECK(reachOf(VideoSourceLine(1277), AxisHorizontal) == 1276);
    CHECK(reachOf(VideoSourceLine(1126), AxisHorizontal) == 1125);

    SUBCASE("the head guard still applies, and the two do not cross") {
        const VideoSourceLine bench = measuredLine(1277, 181, 2553, true);
        CHECK(firstUnitOf(bench, AxisHorizontal) < reachOf(bench, AxisHorizontal));
        CHECK(reachOf(bench, AxisHorizontal) == 1276);
    }
}

// IF_HB_SP2 AT ZERO IS NOT A WINDOW THAT STARTS AT ZERO. Measured on the bench
// at 640x480@60, the one mode whose pulse is behind the origin and so whose
// floor is otherwise nothing: at 0 the picture is doubled and smeared, and at
// 1 it is clean, with every other register identical. The tail already keeps
// two units clear of the wrap for the same kind of reason.
TEST_CASE("the capture floor never reaches zero")
{
    // A line nothing has measured: no pulse, no head blanking, so the floor is
    // the clamp and nothing else.
    CHECK(firstUnitOf(VideoSourceLine(1126), AxisHorizontal)
          == CaptureWindow::FirstCapturableUnit);
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
        const VideoSourceLine line = measuredLine(Units, HsyncLow, AdcLine, false);
        CHECK(firstUnitOf(line, AxisHorizontal) == CaptureWindow::FirstCapturableUnit);
    }

    SUBCASE("a high-active source has it at the head, so the floor clears it") {
        const VideoSourceLine line = measuredLine(Units, HsyncLow, AdcLine, true);
        CHECK(firstUnitOf(line, AxisHorizontal) == line.syncUnits());
    }

    SUBCASE("the stop is the last unit before the wrap either way") {
        const VideoSourceLine line = measuredLine(Units, HsyncLow, AdcLine, false);
        CHECK(reachOf(line, AxisHorizontal) == Units - 1);
    }

    SUBCASE("an inverted pulse keeps the span the head guard would take") {
        const VideoSourceLine positive = measuredLine(900, 109, 900, true);
        const VideoSourceLine inverted = measuredLine(900, 109, 900, false);
        CHECK(capturableOf(inverted, AxisHorizontal)
                  - capturableOf(positive, AxisHorizontal)
              == positive.syncUnits() - CaptureWindow::FirstCapturableUnit);
    }
}

TEST_CASE("the capture starts where the sync pulse ends")
{
    // 800x600@60 at PLLAD_MD 1124. HLOW_LEN 136 of 1124 is the 12.1% duty its
    // 128-of-1056 hsync gives, so 137 units of pulse.
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;

    SUBCASE("a positive pulse sits at the head and the floor clears it") {
        CHECK(firstUnitOf(measuredLine(Units, HsyncLow, AdcLine, true),
                          AxisHorizontal) == 137);
    }

    SUBCASE("an inverted pulse is behind the origin, so the floor is the first unit") {
        CHECK(firstUnitOf(measuredLine(Units, HsyncLow, AdcLine, false),
                          AxisHorizontal) == CaptureWindow::FirstCapturableUnit);
    }
}

// The capture path writes blanking past the hsync pulse on a doubled line, and
// a window opened inside it takes that blanking into the picture as saturated
// green.
//
// Measured on the bench RiscPC at 320x256@50, PLLAD_MD 2206, output 1080p, by
// stepping IF_HB_SP2 one unit at a time and counting green photo columns down
// the left edge:
//
//     IF_HB_SP2  82  88  90  92  94  95  96
//     green cols 27  19  15  12  10   6   0
//
// 82 is that line's floor plus the 3 units the framing origin was off zero, so
// the guard that creep asks for is 96 - 79 = 17.
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
    SUBCASE("the floor clears the pulse by the head blanking") {
        // The bench line: 1103 IF units, and HLOW_LEN 156 of an ADC line of
        // 2206 is the 7.07% duty its hsync gives, which the round-up makes 79
        // units of pulse.
        const VideoSourceLine line = measuredLine(1103, 156, 2206, true);
        CHECK(line.syncUnits() == 79);
        CHECK(firstUnitOf(line, AxisHorizontal)
              == 79 + VideoSourceLine::DoubledHeadBlankingUnits);
    }

    SUBCASE("and the clearance is what the creep asked for") {
        // The bench line at PLLAD_MD 2200: IF line 1101 and HLOW_LEN 156 put
        // the pulse at 78.1 units.
        const VideoSourceLine line = measuredLine(1101, 156, 2200, true);
        CHECK(firstUnitOf(line, AxisHorizontal) - line.syncUnits() >= 22);
    }

    SUBCASE("a doubled line is placed by IF_HBIN_SP") {
        const VideoSourceLine doubled = measuredLine(1254, 89, 2506, true);
        CHECK(firstUnitOf(doubled, AxisHorizontal)
              == doubled.syncUnits() + VideoSourceLine::DoubledHeadBlankingUnits);
    }
}

// --- a framing proportion against the counter --------------------------------

// A video standard states where active video begins as a position in its own
// line, counted from the hsync leading edge. The counter is zeroed on whichever
// edge the chip triggered on, so the two are the same position only where that
// edge is the leading one.
TEST_CASE("a position in the source's line maps onto where video lands in the counter")
{
    const uint16_t Units = 1125, HsyncLow = 136, AdcLine = 1124;
    // 800x600@60: sync, back porch and border are 216 of its 1056 pixels.
    const float ActiveStart = 216.0f / 1056.0f;

    SUBCASE("a positive pulse shares the standard's own origin") {
        const VideoSourceLine line = measuredLine(Units, HsyncLow, AdcLine, true);
        CHECK(videoAtOf(line, AxisHorizontal, ActiveStart) == 230);
    }

    SUBCASE("an inverted pulse moves it back by the sync interval as well") {
        const VideoSourceLine line = measuredLine(Units, HsyncLow, AdcLine, false);
        CHECK(videoAtOf(line, AxisHorizontal, ActiveStart) == 230 - line.syncUnits());
    }

    SUBCASE("a line nothing has measured maps one to one") {
        CHECK(videoAtOf(VideoSourceLine(624), AxisVertical, 0.5f) == 312);
    }
}

// The blanking is written INTO the head of a doubled line; the video behind it
// is where IF_HBIN_SP's own reset put it. So it bounds where a window may OPEN
// and displaces nothing -- which is what a lag does, and the difference is a
// default framing's worth of picture. Charged as a lag, every position in the
// line moves on by it: the bench default window ran 129..1083 and became
// 146..1100, keeping its extent and taking seventeen more units of the line's
// tail, where the source's picture has already stopped.
// docs/investigations/the-bar-at-the-right-edge-is-captured-line-tail.md
TEST_CASE("the blanking at a doubled line's head moves no position in it")
{
    // Where AxisHorizontal puts active video on a source running no raster the
    // standards state. docs/vesa-gtf.md
    const float ActiveStart = 0.117f;
    const VideoSourceLine line = measuredLine(1103, 156, 2206, true);

    CHECK(videoAtOf(line, AxisHorizontal, ActiveStart) == 129);
}

// The input formatter's counter is reset by the RETIMED hsync, so video sits
// where the counter says and a progressive line carries no displacement of its
// own.
//
// A fractional lag was applied here instead, and it was a correction for the
// retiming being bypassed: SyncProcessor::applyForSyncType() wrote
// SP_HS_LOOP_SEL 1 on both sync types, which takes the retiming module out of
// circuit. Measured on the bench at 800x600@60, one frozen state, the only
// variable the routing bit: engaging the retiming moved the card's corner
// square 99 photo columns, which is 77.4 counter units, against the 77.6 the
// correction was applying. The whole of it was the bypass.
// docs/investigations/the-capture-lag-was-the-retiming-bypassed.md
TEST_CASE("a progressive line carries no video lag")
{
    // 800x600@60 at PLLAD_MD 1438, positive-going pulse, undoubled.
    const VideoSourceLine line = measuredLine(1439, 176, 1438, true);

    CHECK(videoAtOf(line, AxisHorizontal, 0.0f) == 0);
}

// ONE FRAMING MUST TAKE THE SAME VIDEO IN BOTH SCAN MODES. The framing names a
// proportion of the source, so the two counters have to be brought onto one
// another -- and they do not sit where the model had them.
//
// Measured on the bench, RiscPC X320 Y256 F50 on `vga`, automation frozen, the
// capture window crept a unit at a time until the source's flashing border
// entered the picture. `RetroScaler-Acorn.mdf` states the mode, so the feature
// is known: h_timings 36,30,44,320,44,38 puts active video at 110..430 of 512,
// and v_timings 3,16,17,256,17,3 at lines 36..292 of 312.
//
// Where each counter puts the card's own edges:
//
//   edge                      doubled        undoubled
//   top / bottom, lines       30.0 / 288.5   28.5 / 287.0
//   right, of the line        0.8300         0.8839
//
// So the undoubled line delivers video 0.0539 of a line LATE, where the frame
// delivers it early. The two pipelines are not the same one and nothing
// requires them to agree in sign, nor to agree on a unit: the frame's is a
// count of counter units and the line's a fraction, each measured as such.
//
// **THE MEASUREMENT IS RELATIVE, WHICH IS WHY IT IS WORTH MORE THAN THE ONE IT
// REPLACES.** Both readings take the same feature on the same source with the
// same edge finder, so the knee's systematic biases -- the aperture's far-end
// inset, the interpolation, the threshold -- fall out of the difference. Read
// against the mode file instead, each counter is out by a further 0.010 to
// 0.020 of a line, which is the bias rather than a second finding.
TEST_CASE("one framing takes the same video along the line in both scan modes")
{
    // The bench source either side of the doubler: PLLAD_MD 2200 on a 1100
    // unit line doubled, 1852 undoubled, sync 36 of 512.
    const float Duty = 36.0f / 512.0f;
    const VideoSourceLine doubled = measuredLine(
        1100, (uint16_t)lrintf(2200 * Duty), 2200, true);
    const VideoSourceLine undoubled = measuredLine(
        1852, (uint16_t)lrintf(1852 * Duty), 1852, true);

    const float Framing = 0.2036f;
    const float apart = (float)videoAtOf(undoubled, AxisHorizontal, Framing) / 1852.0f
                      - (float)videoAtOf(doubled, AxisHorizontal, Framing) / 1100.0f;
    CHECK(apart == doctest::Approx(0.0f).scale(1.0f).epsilon(0.002f));
}

// The doubled counter runs at twice the source's line rate, so the same
// proportion is twice as far in and nothing else differs.
TEST_CASE("one framing takes the same video whichever scan mode is in force")
{
    const float Framing = 36.0f / 312.0f;
    const VideoSourceLine undoubled = VideoSourceLine::frame(312);
    const VideoSourceLine doubled = VideoSourceLine::frame(624);

    // Further in by the position and by nothing else: a term that differed
    // between the modes is the stored framing taking different picture at two
    // output resolutions.
    CHECK(videoAtOf(doubled, AxisVertical, Framing)
              - videoAtOf(undoubled, AxisVertical, Framing)
          == lrintf(Framing * 312.0f));
}

// The frame's counter zeroes on the vertical sync pulse's trailing edge, so
// video starts at the counter's own origin and a framing proportion maps
// straight onto it.
TEST_CASE("a framing proportion maps onto the frame's own counter")
{
    // The Wii at 480p on ypbpr: 524 counted lines, so a 525-unit frame.
    const VideoSourceLine frame = VideoSourceLine::frame(525);

    CHECK(videoAtOf(frame, AxisVertical, 0.4f) == 210);

    SUBCASE("and a position maps back to the framing it was taken from") {
        CHECK(fractionAtOf(frame, AxisVertical,
                           videoAtOf(frame, AxisVertical, 0.4f))
              == doctest::Approx(0.4f));
    }
}
