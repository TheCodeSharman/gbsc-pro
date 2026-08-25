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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Controls.h"

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
