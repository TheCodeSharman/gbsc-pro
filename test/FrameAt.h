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

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/VideoPath.h"

static long framedAt(Tv5725::VideoPath &engine, const Tv5725::Axis &axis,
                     bool zooming)
{
    return zooming ? (long)engine.extentUnitsOn(axis)
                   : (long)engine.originUnitsOn(axis);
}

static void pressFraming(Tv5725::VideoPath &engine, bool vertical, bool zooming,
                         int16_t pixels)
{
    if (zooming)
        vertical ? engine.zoom(0, pixels) : engine.zoom(pixels, 0);
    else
        vertical ? engine.pan(0, pixels) : engine.pan(pixels, 0);
}

// The most output pixels one granule can cost: the capture moves in twos and
// the engine magnifies by at most Scale::Unity / Scale::Min, so a request this
// large has always crossed half a granule.
static const int16_t MostPixelsPerGranule = 16;

static void walkFraming(Tv5725::VideoPath &engine, const Tv5725::Axis &axis,
                        bool vertical, bool zooming, long want)
{
    for (;;) {
        const long at = framedAt(engine, axis, zooming);
        if (at == want)
            break;
        // Zoom in CROPS, so it moves the extent the other way from the pan.
        const int16_t way = zooming ? (at > want ? 1 : -1) : (want > at ? 1 : -1);

        // A press is stated in OUTPUT PIXELS and the capture moves in its own
        // units, so what one granule costs depends on the magnification this
        // solve landed on. Ask for more until the framing moves, rather than
        // assuming a single pixel reaches a granule.
        int16_t pixels = 0;
        do {
            ++pixels;
            // A solve clamps the framing it was given, so not every state is
            // reachable -- and a press that moves nothing would spin here.
            REQUIRE(pixels <= MostPixelsPerGranule);
            pressFraming(engine, vertical, zooming, (int16_t)(way * pixels));
        } while (framedAt(engine, axis, zooming) == at);
    }
}

static void frameAt(Tv5725::VideoPath &engine, int16_t zh, int16_t zv,
                    int16_t ph, int16_t pv)
{
    using namespace Tv5725;
    for (int vertical = 0; vertical < 2; ++vertical) {
        const Axis &axis = vertical ? AxisVertical : AxisHorizontal;
        REQUIRE(engine.lineUnitsOn(axis) > 0);

        const int16_t zoom = vertical ? zv : zh;
        const int16_t pan = vertical ? pv : ph;

        // The two controls are orthogonal, so each target is reached on its own.
        walkFraming(engine, axis, vertical != 0, true,
                    (long)engine.extentUnitsOn(axis) - zoom);
        walkFraming(engine, axis, vertical != 0, false,
                    (long)engine.originUnitsOn(axis) + pan);
    }
}

#endif  // TEST_FRAME_AT_H_
