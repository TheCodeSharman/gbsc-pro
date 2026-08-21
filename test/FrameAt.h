#ifndef TEST_FRAME_AT_H_
#define TEST_FRAME_AT_H_

// Walk the framing to an exact state through the controls, one capture granule
// a press, so a test states what it needs without a door the user does not have.

#include <doctest/doctest.h>

#include "../GBSC-Pro-Source code/gbs-control/src/tv5725/Geometry.h"

static void frameAt(Tv5725::Geometry &engine, int16_t zh, int16_t zv,
                    int16_t ph, int16_t pv)
{
    struct Target { int16_t wanted; int16_t (Tv5725::PanAndZoom::*held)() const; };
    const Target targets[] = {
        {zh, &Tv5725::PanAndZoom::horizontalZoom},
        {zv, &Tv5725::PanAndZoom::verticalZoom},
        {ph, &Tv5725::PanAndZoom::horizontalPan},
        {pv, &Tv5725::PanAndZoom::verticalPan},
    };
    for (uint8_t axis = 0; axis < 4; ++axis) {
        for (;;) {
            const int16_t at = (engine.framing().*targets[axis].held)();
            if (at == targets[axis].wanted)
                break;
            const int16_t way = targets[axis].wanted > at ? 1 : -1;
            switch (axis) {
            case 0: engine.zoom(way, 0); break;
            case 1: engine.zoom(0, way); break;
            case 2: engine.pan(way, 0); break;
            case 3: engine.pan(0, way); break;
            }
            // A solve clamps the framing it was given, so not every state is
            // reachable -- and a press that moves nothing would spin here.
            REQUIRE((engine.framing().*targets[axis].held)() != at);
        }
    }
}

#endif  // TEST_FRAME_AT_H_
