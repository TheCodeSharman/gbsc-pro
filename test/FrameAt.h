#ifndef TEST_FRAME_AT_H_
#define TEST_FRAME_AT_H_

// Walk the framing to an exact state through the controls, one capture granule
// a press, so a test states what it needs without a door the user does not have.
//
// Stated in INPUT UNITS: zoom is units cropped off the width the default
// placed, pan is units moved from where that put it. The framing holds
// proportions, so each target is read back through the engine's own conversion
// against the capturable region its last solve ran on.

#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"

static void frameAt(Tv5725::Geometry &engine, int16_t zh, int16_t zv,
                    int16_t ph, int16_t pv)
{
    using namespace Tv5725;
    for (int vertical = 0; vertical < 2; ++vertical) {
        const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
        REQUIRE(engine.capturableOn(axis) > 0);

        const int16_t zoom = vertical ? zv : zh;
        const int16_t pan = vertical ? pv : ph;

        // Zoom keeps the window centred, so it takes half of what the extent
        // loses with it -- the same arithmetic the control applies.
        const long wantExtent = (long)engine.extentUnitsOn(axis) - zoom;
        const long wantOrigin = (long)engine.originUnitsOn(axis) + pan + zoom / 2;

        for (;;) {
            const long at = engine.extentUnitsOn(axis);
            if (at == wantExtent)
                break;
            vertical ? engine.zoom(0, at > wantExtent ? 1 : -1)
                     : engine.zoom(at > wantExtent ? 1 : -1, 0);
            // A solve clamps the framing it was given, so not every state is
            // reachable -- and a press that moves nothing would spin here.
            REQUIRE((long)engine.extentUnitsOn(axis) != at);
        }

        for (;;) {
            const long at = engine.originUnitsOn(axis);
            if (at == wantOrigin)
                break;
            vertical ? engine.pan(0, wantOrigin > at ? 1 : -1)
                     : engine.pan(wantOrigin > at ? 1 : -1, 0);
            REQUIRE((long)engine.originUnitsOn(axis) != at);
        }
    }
}

#endif  // TEST_FRAME_AT_H_
