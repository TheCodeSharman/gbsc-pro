// Host-compiled unit tests for the OSD and IR control surface --
// `make -C test controls`.
//
// Controls is the OSD and remote path. The hardware pad suite drives /geometry
// over HTTP and never reaches it, so which axis a press lands on is pinned
// nowhere else.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "SolvedEngine.h"

// GBS_DEBUG is 0 in a host build, so report() compiles to its (void) casts and
// the console is never written to. Controls.h forward-declares this.
class Print {};

#include <string>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Controls.h"
#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Nudge.h"

using namespace Tv5725;

struct Panel {
    SolvedEngine solved;
    Print console;
    Controls controls;

    Panel() : controls(solved.engine, console) {}

    long origin(const Axis &axis) const { return solved.engine.originUnitsOn(axis); }
    long extent(const Axis &axis) const { return solved.engine.extentUnitsOn(axis); }
};

TEST_CASE("a press moves the axis it names and leaves the other alone")
{
    SUBCASE("horizontal pan") {
        Panel panel;
        // Cropped first, or there is nothing to pan within.
        panel.controls.horizontalZoom(400);
        const long horizontal = panel.origin(AxisHorizontal);
        const long vertical = panel.origin(AxisVertical);

        panel.controls.horizontalPan(16);
        CHECK(panel.origin(AxisHorizontal) != horizontal);
        CHECK(panel.origin(AxisVertical) == vertical);
    }

    SUBCASE("vertical pan") {
        Panel panel;
        panel.controls.verticalZoom(100);
        const long horizontal = panel.origin(AxisHorizontal);
        const long vertical = panel.origin(AxisVertical);

        panel.controls.verticalPan(16);
        CHECK(panel.origin(AxisVertical) != vertical);
        CHECK(panel.origin(AxisHorizontal) == horizontal);
    }

    SUBCASE("horizontal zoom") {
        Panel panel;
        const long horizontal = panel.extent(AxisHorizontal);
        const long vertical = panel.extent(AxisVertical);

        panel.controls.horizontalZoom(16);
        CHECK(panel.extent(AxisHorizontal) != horizontal);
        CHECK(panel.extent(AxisVertical) == vertical);
    }

    SUBCASE("vertical zoom") {
        Panel panel;
        const long horizontal = panel.extent(AxisHorizontal);
        const long vertical = panel.extent(AxisVertical);

        panel.controls.verticalZoom(16);
        CHECK(panel.extent(AxisVertical) != vertical);
        CHECK(panel.extent(AxisHorizontal) == horizontal);
    }
}

TEST_CASE("a press is in output pixels all the way from the panel")
{
    Panel panel;

    // What the solve wrote, read back as an outside observer would.
    const float magnification = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
    const int16_t wanted = AxisHorizontal.stepUnits(16, magnification);
    REQUIRE(wanted != 16);

    const long before = panel.extent(AxisHorizontal);
    panel.controls.horizontalZoom(16);
    CHECK(before - panel.extent(AxisHorizontal) == wanted);
}

TEST_CASE("a press with nowhere to go leaves the framing where it was")
{
    // Panned to the last unit the line can write, further presses must not
    // drift. Where that limit falls depends on the divider, so the test walks
    // to it rather than assuming the default framing is already there.
    Panel panel;

    long atLimit = panel.origin(AxisHorizontal);
    for (int i = 0; i < 200; ++i) {
        panel.controls.horizontalPan(16);
        const long now = panel.origin(AxisHorizontal);
        if (now == atLimit)
            break;
        atLimit = now;
    }

    for (int i = 0; i < 5; ++i)
        panel.controls.horizontalPan(16);
    CHECK(panel.origin(AxisHorizontal) == atLimit);
}

// AN OSD TAP ASKS FOR THE SMALLEST MOVE THERE IS, not for a number of output
// pixels. One capture granule is 2 x magnification output pixels across, five
// of them at the bench Acorn framing, so a tap stated as one pixel rounds to
// nothing and the remote reports a limit that is not there.
TEST_CASE("a fine press moves one granule, whatever that costs in pixels")
{
    Panel panel;

    SUBCASE("one tap is one granule on each axis") {
        // Cropped first, or there is nothing to pan within.
        panel.controls.horizontalZoom(400);
        panel.controls.verticalZoom(100);

        const long horizontal = panel.origin(AxisHorizontal);
        REQUIRE(panel.controls.horizontalPanFine(1));
        CHECK(panel.origin(AxisHorizontal) - horizontal
              == AxisHorizontal.captureGranularity());

        const long vertical = panel.origin(AxisVertical);
        REQUIRE(panel.controls.verticalPanFine(-1));
        CHECK(vertical - panel.origin(AxisVertical)
              == AxisVertical.captureGranularity());
    }

    SUBCASE("and the hold ramp multiplies it") {
        const long before = panel.extent(AxisHorizontal);
        REQUIRE(panel.controls.horizontalZoomFine(4));
        CHECK(before - panel.extent(AxisHorizontal)
              == 4 * AxisHorizontal.captureGranularity());
    }

    SUBCASE("the vertical zoom answers the same way") {
        const long before = panel.extent(AxisVertical);
        REQUIRE(panel.controls.verticalZoomFine(3));
        CHECK(before - panel.extent(AxisVertical)
              == 3 * AxisVertical.captureGranularity());
    }
}

// A REMOTE'S PAD ASKS FOR A CONTROL AND A DIRECTION, not for a letter. The four
// /sc? geometry letters are stated in output pixels, which a tap cannot use:
// the described menu names the control instead and the hold ramp supplies the
// number of granules.
TEST_CASE("a nudge reaches the control it names, a granule at a time")
{
    SUBCASE("horizontal pan") {
        Panel asked;
        asked.controls.horizontalZoom(400);
        const long before = asked.origin(AxisHorizontal);

        REQUIRE(asked.controls.nudge(Nudge::HorizontalPan, 2));
        CHECK(asked.origin(AxisHorizontal) - before
              == 2 * AxisHorizontal.captureGranularity());
    }

    SUBCASE("vertical pan") {
        Panel asked;
        asked.controls.verticalZoom(100);
        const long before = asked.origin(AxisVertical);

        REQUIRE(asked.controls.nudge(Nudge::VerticalPan, -1));
        CHECK(before - asked.origin(AxisVertical)
              == AxisVertical.captureGranularity());
    }

    SUBCASE("horizontal zoom") {
        Panel asked;
        const long before = asked.extent(AxisHorizontal);

        REQUIRE(asked.controls.nudge(Nudge::HorizontalZoom, 3));
        CHECK(before - asked.extent(AxisHorizontal)
              == 3 * AxisHorizontal.captureGranularity());
    }

    SUBCASE("vertical zoom") {
        Panel asked;
        const long before = asked.extent(AxisVertical);

        REQUIRE(asked.controls.nudge(Nudge::VerticalZoom, 3));
        CHECK(before - asked.extent(AxisVertical)
              == 3 * AxisVertical.captureGranularity());
    }
}

TEST_CASE("a control is named, which is how /menu reports a pad press")
{
    CHECK(std::string(Nudge::name(Nudge::HorizontalPan)) == "hpan");
    CHECK(std::string(Nudge::name(Nudge::VerticalPan)) == "vpan");
    CHECK(std::string(Nudge::name(Nudge::HorizontalZoom)) == "hzoom");
    CHECK(std::string(Nudge::name(Nudge::VerticalZoom)) == "vzoom");
}
