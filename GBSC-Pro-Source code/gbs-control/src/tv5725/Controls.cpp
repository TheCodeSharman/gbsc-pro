#include "Controls.h"

#include <Arduino.h>
#include <math.h>

namespace Tv5725 {

Controls::Controls(Geometry &engine, Print &console)
    : engine_(engine), console_(console) {}

bool Controls::panH(int16_t pixels)
{
    int16_t units = unitsFor(pixels, GBS::VDS_HSCALE::read(), AxisHorizontal);
    bool moved = engine_.pan(units, 0);
    report("panH", pixels, units);
    return moved;
}

bool Controls::panV(int16_t pixels)
{
    int16_t units = unitsFor(pixels, GBS::VDS_VSCALE::read(), AxisVertical);
    bool moved = engine_.pan(0, units);
    report("panV", pixels, units);
    return moved;
}

bool Controls::zoomH(int16_t pixels)
{
    int16_t units = unitsFor(pixels, GBS::VDS_HSCALE::read(), AxisHorizontal);
    bool moved = engine_.zoom(units, 0);
    report("zoomH", pixels, units);
    return moved;
}

bool Controls::zoomV(int16_t pixels)
{
    int16_t units = unitsFor(pixels, GBS::VDS_VSCALE::read(), AxisVertical);
    bool moved = engine_.zoom(0, units);
    report("zoomV", pixels, units);
    return moved;
}

Geometry &Controls::engine() const { return engine_; }

int16_t Controls::unitsFor(int16_t pixels, uint16_t scaleReg, const Axis &axis)
{
    return axis.stepUnits(pixels, Scale(scaleReg).magnification());
}

void Controls::report(const char *control, int16_t pixels,
                      int16_t units) const
{
#if GBS_DEBUG
    const PanAndZoom &framing = engine_.framing();
    console_.printf_P(PSTR("ADJ %s %+dpx = %+du -> framing zh:%d zv:%d ph:%d pv:%d  "
                           "IF_HB %d..%d  IF_VB %d..%d  HSCALE %d VSCALE %d\n"),
                      control, (int)pixels, (int)units,
                      framing.zoomH(), framing.zoomV(),
                      framing.panH(), framing.panV(),
                      GBS::IF_HB_SP2::read(), GBS::IF_HB_ST2::read(),
                      GBS::IF_VB_SP::read(), GBS::IF_VB_ST::read(),
                      GBS::VDS_HSCALE::read(), GBS::VDS_VSCALE::read());
#else
    (void)control;
    (void)pixels;
    (void)units;
#endif
}

}  // namespace Tv5725
