// Host-compiled unit tests for the OSD and IR control surface --
// `make -C test controls`.
//
// Controls is the OSD and remote path. The hardware pad suite drives /geometry
// over HTTP and never reaches it, so which axis a press lands on is pinned
// nowhere else.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "BenchGeometry.h"

// GBS_DEBUG is 0 in a host build, so report() compiles to its (void) casts and
// the console is never written to. Controls.h forward-declares this.
class Print {};

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Controls.h"

using namespace Tv5725;

struct Panel {
    Bench bench;
    Print console;
    Controls controls;

    Panel() : controls(bench.engine, console) {}

    const PanAndZoom &framing() const { return bench.engine.framing(); }
};

TEST_CASE("a press moves the axis it names and leaves the other alone")
{
    SUBCASE("horizontal pan") {
        Panel panel;
        // Cropped first, or there is nothing to pan within.
        panel.controls.horizontalZoom(400);
        const int16_t vertical = panel.framing().verticalPan();

        panel.controls.horizontalPan(16);
        CHECK(panel.framing().horizontalPan() != 0);
        CHECK(panel.framing().verticalPan() == vertical);
    }

    SUBCASE("vertical pan") {
        Panel panel;
        panel.controls.verticalZoom(100);
        const int16_t horizontal = panel.framing().horizontalPan();

        panel.controls.verticalPan(16);
        CHECK(panel.framing().verticalPan() != 0);
        CHECK(panel.framing().horizontalPan() == horizontal);
    }

    SUBCASE("horizontal zoom") {
        Panel panel;
        panel.controls.horizontalZoom(16);
        CHECK(panel.framing().horizontalZoom() != 0);
        CHECK(panel.framing().verticalZoom() == 0);
    }

    SUBCASE("vertical zoom") {
        Panel panel;
        panel.controls.verticalZoom(16);
        CHECK(panel.framing().verticalZoom() != 0);
        CHECK(panel.framing().horizontalZoom() == 0);
    }
}

TEST_CASE("a press is in output pixels all the way from the panel")
{
    Panel panel;

    // What the solve wrote, read back as an outside observer would.
    const float magnification = Scale(Wire.field(3, 0x16, 0, 10)).magnification();
    const int16_t wanted = AxisHorizontal.stepUnits(16, magnification);
    REQUIRE(wanted != 16);

    panel.controls.horizontalZoom(16);
    CHECK(panel.framing().horizontalZoom() == wanted);
}

TEST_CASE("a press with nowhere to go leaves the framing where it was")
{
    // The default capture already runs to the last unit the line can write, so
    // there is nowhere further right to go and repeated presses must not drift.
    Panel panel;
    const int16_t before = panel.framing().horizontalPan();

    for (int i = 0; i < 5; ++i)
        panel.controls.horizontalPan(16);
    CHECK(panel.framing().horizontalPan() == before);
}
