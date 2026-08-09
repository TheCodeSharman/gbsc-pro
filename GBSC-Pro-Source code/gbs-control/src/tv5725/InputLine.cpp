#include "InputLine.h"

#include <math.h>

namespace Tv5725 {
namespace {

// The sync processor's own validity window for the hsync duty.
// updateSpDynamic() discards a reading outside it too, and HLOW_LEN is a
// segment 0 live measurement that rails like every other one.
const float DutyMin = 0.041f;
const float DutyMax = 0.152f;

// What the chip is already configured for when the duty cannot be measured.
// Sampling writes SP_RT_HS_SP = PLLAD_MD x 0.93, so 1 - 0.93 IS the sync width
// the retiming module expects rather than a fudge factor. Failing open here
// restores the green bands. docs/scaler-geometry-model.md
const float FallbackDuty = 0.07f;

}  // namespace

InputLine::InputLine(uint16_t units) : units_(units), syncUnits_(0) {}

InputLine::InputLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits) {}

uint16_t InputLine::units() const { return units_; }

uint16_t InputLine::syncUnits() const { return syncUnits_; }

uint16_t InputLine::progressiveStop(uint16_t start) const
{
    return start + units_;
}

uint16_t InputLine::firstCapture() const { return syncUnits_; }

uint16_t InputLine::lastCapture() const
{
    // The capture STOP may never reach `units`, only units - 1. That value is
    // the wrap point -- IF_VB_ST rolls at 2 x (VTOTAL + 1) and IF_HB_ST2 at
    // IF_HSYNC_RST + 1 -- and a window written onto it does not clamp, it
    // rolls, which reads as the picture jumping rather than as a capture fault.
    // geometry_math.py is the reference and has always had it, as `wrap_at - 1`
    // in scale_step() and pan_capture() alike.
    return units_ == 0 ? 0 : units_ - 1;
}

uint16_t InputLine::capturable() const
{
    uint16_t first = firstCapture(), last = lastCapture();
    return last > first ? last - first : 0;
}

InputLine InputLine::measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine)
{
    float duty = adcLine > 0 ? (float)hlowLen / (float)adcLine : 0.0f;
    if (duty < DutyMin || duty > DutyMax)
        duty = FallbackDuty;

    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. DutyMax bounds it at 15% of
    // the line, so what is left is always the greater part of it.
    return InputLine(units, (uint16_t)ceilf(units * duty));
}

}  // namespace Tv5725
