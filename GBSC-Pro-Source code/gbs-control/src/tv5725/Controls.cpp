#include "Controls.h"

#include <Arduino.h>
#include <math.h>

namespace Tv5725 {

Controls::Controls(Geometry &engine, Print &console)
    : engine_(engine), console_(console) {}

bool Controls::horizontalPan(int16_t pixels)
{
    bool moved = engine_.pan(pixels, 0);
    report("horizontalPan", pixels);
    return moved;
}

bool Controls::verticalPan(int16_t pixels)
{
    bool moved = engine_.pan(0, pixels);
    report("verticalPan", pixels);
    return moved;
}

bool Controls::horizontalZoom(int16_t pixels)
{
    bool moved = engine_.zoom(pixels, 0);
    report("horizontalZoom", pixels);
    return moved;
}

bool Controls::verticalZoom(int16_t pixels)
{
    bool moved = engine_.zoom(0, pixels);
    report("verticalZoom", pixels);
    return moved;
}

Geometry &Controls::engine() const { return engine_; }

void Controls::report(const char *control, int16_t pixels) const
{
#if GBS_DEBUG
    // Read from the chip on purpose: this line is the only thing that can show
    // the engine's model and the hardware disagreeing.
    console_.printf_P(PSTR("ADJ %s %+dpx -> framing h %u+%u/%u v %u+%u/%u  "
                           "IF_HB %d..%d  IF_VB %d..%d  HSCALE %d VSCALE %d\n"),
                      control, (int)pixels,
                      engine_.originUnitsOn(AxisHorizontal),
                      engine_.extentUnitsOn(AxisHorizontal),
                      engine_.capturableOn(AxisHorizontal),
                      engine_.originUnitsOn(AxisVertical),
                      engine_.extentUnitsOn(AxisVertical),
                      engine_.capturableOn(AxisVertical),
                      GBS::IF_HB_SP2::read(), GBS::IF_HB_ST2::read(),
                      GBS::IF_VB_SP::read(), GBS::IF_VB_ST::read(),
                      GBS::VDS_HSCALE::read(), GBS::VDS_VSCALE::read());
#else
    (void)control;
    (void)pixels;
#endif
}

}  // namespace Tv5725
