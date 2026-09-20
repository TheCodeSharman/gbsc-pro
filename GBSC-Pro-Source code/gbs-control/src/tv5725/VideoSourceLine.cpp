#include "VideoSourceLine.h"

#include "Tv5725Log.h"

#include <math.h>
#include <stdio.h>

namespace Tv5725 {

const uint16_t VideoSourceLine::DoubledHeadBlankingUnits;
const uint16_t VideoSourceLine::FirstCapturableUnit;
const uint16_t VideoSourceLine::CaptureLagUnits;

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
    : units_(units), syncUnits_(0), lagUnits_(0), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits)
    : units_(units), syncUnits_(syncUnits), lagUnits_(0), headBlankingUnits_(0),
      syncAtHead_(true) {}

VideoSourceLine::VideoSourceLine(uint16_t units, uint16_t syncUnits,
                                 uint16_t headBlankingUnits, bool syncAtHead)
    : units_(units), syncUnits_(syncUnits),
      lagUnits_(headBlankingUnits ? 0 : CaptureLagUnits),
      headBlankingUnits_(headBlankingUnits), syncAtHead_(syncAtHead) {}

uint16_t VideoSourceLine::units() const { return units_; }

uint16_t VideoSourceLine::syncUnits() const { return syncUnits_; }

uint16_t VideoSourceLine::progressiveStop(uint16_t start) const
{
    return start + units_;
}

uint16_t VideoSourceLine::firstCapture() const
{
    // Zero is not a capture start. Measured at 640x480@60, whose pulse is
    // behind the origin and so raises the floor off nothing: IF_HB_SP2 at 0
    // doubles and smears the picture, and 1 is clean with every other register
    // identical. The tail keeps two units clear of the wrap for its own
    // reasons; this is the head's equivalent.
    const uint16_t floor = lagUnits_ + headBlankingUnits_ + (syncAtHead_ ? syncUnits_ : 0);
    return floor < FirstCapturableUnit ? FirstCapturableUnit : floor;
}

uint16_t VideoSourceLine::maxCaptureWidth() const
{
    return capturable();
}

uint16_t VideoSourceLine::videoAt(float lineFraction) const
{
    long at = lrintf(lineFraction * (float)units_)
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
    //
    // THE PULSE IS NOT TAKEN OFF THE TAIL. Where the line is counted from the
    // pulse's trailing edge the next line's pulse does occupy the tail, and
    // excluding it costs picture: measured at 640x480@60 the right-hand border
    // goes with it. What arrives there is bounded by the wrap, not by the
    // pulse. docs/known-issues.md
    return units_ < 2 ? 0 : units_ - 2;
}

uint16_t VideoSourceLine::capturable() const
{
    uint16_t first = firstCapture(), last = lastCapture();
    return last > first ? last - first : 0;
}

VideoSourceLine VideoSourceLine::forDuty(uint16_t units, float duty, bool lineDoubled,
                                         bool syncAtHead)
{
    if (duty < DutyMin || duty > DutyMax) {
        // The capture window is placed from a GUESS from here on. Silent, this
        // is invisible from the picture wherever the guess is close -- the
        // bench source's 7.03% against a 7.00% fallback -- while every mode
        // whose pulse is a different fraction of the line is placed wrong.
        char line[80];
        snprintf(line, sizeof(line),
                 "duty refused: %u/1000 outside %u..%u, falling back to %u/1000",
                 (unsigned)lrintf(duty * 1000.0f),
                 (unsigned)lrintf(DutyMin * 1000.0f),
                 (unsigned)lrintf(DutyMax * 1000.0f),
                 (unsigned)lrintf(FallbackDuty * 1000.0f));
        tv5725Log(line);
        duty = FallbackDuty;
    }

    // Round UP, so a pulse that ends part way through a unit leaves that unit
    // outside the capture rather than half in it. DutyMax bounds it at 15% of
    // the line, so what is left is always the greater part of it.
    return VideoSourceLine(units, (uint16_t)ceilf(units * duty),
                           lineDoubled ? DoubledHeadBlankingUnits : 0, syncAtHead);
}

VideoSourceLine VideoSourceLine::measured(uint16_t units, uint16_t hlowLen, uint16_t adcLine,
                                          bool syncAtHead)
{
    // Two ADC samples to the unit is what says the line is doubled.
    return forDuty(units, adcLine > 0 ? (float)hlowLen / (float)adcLine : 0.0f,
                   adcLine >= units + units / 2, syncAtHead);
}


}  // namespace Tv5725
