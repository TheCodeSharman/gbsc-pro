#include "VideoSourceLine.h"

#include <math.h>

namespace Tv5725 {

const uint16_t VideoSourceLine::WriteLimitUnits;
const uint16_t VideoSourceLine::CaptureLagUnits;
const uint16_t VideoSourceLine::CaptureWidthLimitUnits;

namespace {

// The sync processor's own validity window for the hsync duty.
// updateSpDynamic() discards a reading outside it too, and HLOW_LEN is a
// segment 0 live measurement that rails like every other one.
const float DutyMin = 0.041f;
const float DutyMax = 0.152f;

// What the chip is already configured for when the duty cannot be measured.
// SourceMeasurement writes SP_RT_HS_SP = PLLAD_MD x 0.93, so 1 - 0.93 IS the sync width
// the retiming module expects rather than a fudge factor. Failing open here
// restores the green bands. docs/scaler-geometry-model.md
const float FallbackDuty = 0.07f;

}  // namespace

VideoSourceLine::VideoSourceLine(uint16_t units)
    : units_(units), syncUnits_(0), lagUnits_(0), writeLimitUnits_(WriteLimitUnits),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits), lagUnits_(0),
      writeLimitUnits_(WriteLimitUnits), syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits, uint16_t lagUnits,
                                 bool syncAtHead)
    : units_(units), syncUnits_(syncUnits), lagUnits_(lagUnits),
      writeLimitUnits_(WriteLimitUnits), syncAtHead_(syncAtHead) {}

uint16_t VideoSourceLine::units() const { return units_; }

uint16_t VideoSourceLine::syncUnits() const { return syncUnits_; }

uint16_t VideoSourceLine::progressiveStop(uint16_t start) const
{
    return start + units_;
}

uint16_t VideoSourceLine::firstCapture() const
{
    return lagUnits_ + (syncAtHead_ ? syncUnits_ : 0);
}

uint16_t VideoSourceLine::maxCaptureWidth() const
{
    const uint16_t span = capturable();
    return span > CaptureWidthLimitUnits ? CaptureWidthLimitUnits : span;
}

uint16_t VideoSourceLine::videoAt(float lineFraction) const
{
    long at = lrintf(lineFraction * (float)units_) + (long)lagUnits_
            - (syncAtHead_ ? 0L : (long)syncUnits_);
    if (at < 0)
        at = 0;
    return at > (long)units_ ? units_ : (uint16_t)at;
}

uint16_t VideoSourceLine::lastCapture() const
{
    // Neither of the last two units is a capture stop. `units` is the wrap
    // point, and a window written onto it rolls rather than clamping;
    // units - 1 is the line reset value, where the input formatter stops
    // producing pixels at all. docs/scaler-geometry-model.md
    uint16_t beforeWrap = units_ < 2 ? 0 : units_ - 2;

    // And the far end: past WriteLimitUnits the capture path writes blanking
    // rather than video. SourceMeasurement caps the divider to keep the line inside the
    // limit, so what this catches is the lines it did not choose -- a custom
    // preset's, a bypass switch's.
    return beforeWrap > writeLimitUnits_ ? writeLimitUnits_ : beforeWrap;
}

uint16_t VideoSourceLine::capturable() const
{
    uint16_t first = firstCapture(), last = lastCapture();
    return last > first ? last - first : 0;
}

VideoSourceLine VideoSourceLine::forDuty(uint16_t units, float duty, bool lineDoubled,
                                         uint16_t lagUnits, bool syncAtHead)
{
    if (duty < DutyMin || duty > DutyMax)
        duty = FallbackDuty;

    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. DutyMax bounds it at 15% of
    // the line, so what is left is always the greater part of it.
    VideoSourceLine line(units, (uint16_t)ceilf(units * duty), lagUnits, syncAtHead);

    // A doubled line greens past a POSITION; an undoubled one past a capture
    // WIDTH, which capturable() bounds instead.
    // docs/investigations/tail-green.md
    if (units == 0 || !lineDoubled)
        line.writeLimitUnits_ = units;
    return line;
}

VideoSourceLine VideoSourceLine::measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine,
                                          uint16_t lagUnits, bool syncAtHead)
{
    // Two ADC samples to the unit is what says the line is doubled.
    return forDuty(units, adcLine > 0 ? (float)hlowLen / (float)adcLine : 0.0f,
                   adcLine >= units + units / 2, lagUnits, syncAtHead);
}

uint16_t VideoSourceLine::framableIfLine(float syncDuty, uint16_t lagUnits,
                                         bool syncAtHead, bool lineDoubled)
{
    if (syncDuty < DutyMin || syncDuty > DutyMax)
        syncDuty = FallbackDuty;

    // capturable() only grows with the line, so the largest one that fits is
    // the closed-form inverse of it, walked down by the unit or two that
    // rounding the sync guard up can cost.
    const float head = syncAtHead ? syncDuty : 0.0f;
    const float units = ((float)CaptureWidthLimitUnits + 2.0f + (float)lagUnits)
                        / (1.0f - head);
    uint16_t ifLine = units > 2.0f ? (uint16_t)units : 2;

    while (ifLine > 2
           && forDuty((uint16_t)(ifLine + 1), syncDuty, lineDoubled, lagUnits, syncAtHead)
                  .capturable() > CaptureWidthLimitUnits)
        --ifLine;
    return ifLine;
}

}  // namespace Tv5725
