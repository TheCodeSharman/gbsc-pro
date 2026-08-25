#include "SourceMeasurement.h"

#include <stdio.h>

#include "CaptureWindow.h"   // the settling bounds, so there is one owner of them
#include "InputLine.h"   // the capture write limit, likewise

#include "../../gbs_types.h"

namespace Tv5725 {

const uint32_t SourceMeasurement::MaxSampleRateHz;
const uint16_t SourceMeasurement::DividerMax;
const uint16_t SourceMeasurement::RecommendedPercent;
const uint16_t SourceMeasurement::RetimeStopPercent;
const uint16_t SourceMeasurement::LatchedSamplesTolerance;
const uint8_t SourceMeasurement::LinesPerCountMax;
const uint16_t SourceMeasurement::LineDoubleBelowLines;

// A dropped read of ADC_CLK_ICLK1X/2X arrives as 0. Treating that as "no
// oversampling" keeps the ceiling honest; treating it as a divisor would make
// the limit infinite, which is the wrong way to be wrong about a rating.
static uint8_t atLeastOne(uint8_t oversample)
{
    return oversample == 0 ? 1 : oversample;
}

uint16_t SourceMeasurement::ifLineFor(uint16_t divider, bool lineDoubled)
{
    return lineDoubled ? (uint16_t)(divider / 2) : divider;
}

// Integer, because the ESP8266 has no FPU and this runs on every solve. The
// float form it replaces truncated too, and 4095 x 93 is well inside 32 bits.
uint16_t SourceMeasurement::retimeStopFor(uint16_t divider)
{
    return (uint16_t)(((uint32_t)divider * RetimeStopPercent) / 100);
}

uint32_t SourceMeasurement::sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                uint8_t oversample)
{
    return (uint32_t)divider * lineRateHz * atLeastOne(oversample);
}

bool SourceMeasurement::withinLimit(uint16_t divider, uint32_t lineRateHz,
                           uint8_t oversample)
{
    if (lineRateHz == 0)
        return false;
    return sampleRateHz(divider, lineRateHz, oversample) <= MaxSampleRateHz;
}

uint16_t SourceMeasurement::maxDivider(uint32_t lineRateHz, uint8_t oversample)
{
    if (lineRateHz == 0)
        return 0;

    // getSourceFieldRate() reports 0 with no lock and that reaches here as a
    // line rate, so the divide is guarded above. Everything below is integer:
    // the ESP8266 has no FPU and this runs on every solve.
    uint32_t perLine = lineRateHz * atLeastOne(oversample);
    uint32_t largest = MaxSampleRateHz / perLine;

    if (largest > DividerMax)
        return DividerMax;
    return (uint16_t)largest;
}

uint32_t SourceMeasurement::lineRateFrom(uint16_t sourceLines, float fieldRateHz)
{
    if (!countIsSource(sourceLines))
        return 0;

    if (!(fieldRateHz >= FieldRateMinHz) || !(fieldRateHz <= FieldRateMaxHz))
        return 0;

    return (uint32_t)(fieldRateHz * (float)sourceLines);
}

bool SourceMeasurement::rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                         uint16_t heldLines, uint32_t heldLineRateHz)
{
    if (heldLineRateHz == 0 || lineRateHz == 0)
        return true;
    if (lines != heldLines)
        return true;

    uint32_t larger = lineRateHz > heldLineRateHz ? lineRateHz : heldLineRateHz;
    uint32_t smaller = lineRateHz > heldLineRateHz ? heldLineRateHz : lineRateHz;

    return (larger - smaller) * 1000u <= (uint32_t)HeldRateTolerancePerMille * smaller;
}

uint16_t SourceMeasurement::recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                               bool lineDoubled)
{
    uint16_t ceiling = maxDivider(lineRateHz, oversample);
    if (ceiling == 0)
        return 0;

    uint16_t backed = (uint16_t)(((uint32_t)ceiling * RecommendedPercent) / 100);

    // The second ceiling: a line the capture path cannot write to the end of.
    // InputLine::WriteLimitUnits is in IF units and ifLineFor() halves, so the
    // divider that puts the line end exactly on the limit is twice it.
    const uint16_t forWriteLimit = lineDoubled
        ? (uint16_t)(InputLine::WriteLimitUnits * 2)
        : InputLine::WriteLimitUnits;
    if (backed > forWriteLimit)
        backed = forWriteLimit;

    // Even, so ifLineFor() divides exactly. An odd divider leaves the IF half a
    // sample out from the line the ADC is delivering.
    return (uint16_t)(backed & ~1u);
}

// --- the chosen divider, held ----------------------------------------------

const uint8_t SourceMeasurement::NominalFieldRateHz;
const uint8_t SourceMeasurement::SteadySamples;
const uint16_t SourceMeasurement::RateAgreementPerMille;
const uint8_t SourceMeasurement::RateAgreementAttempts;

SourceMeasurement::SourceMeasurement()
    : divider_(0), lineRateHz_(0), sourceLines_(0), fieldRateHz_(0.0f),
      agreedRateHz_(0.0f), goodLines_(0), goodLineRateHz_(0),
      rateRejections_(0), lineDoubled_(true), steadyLines_(0), steadyRun_(0),
      rateAttempts_(0)
{
}

bool SourceMeasurement::countIsSource(uint16_t lines)
{
    return lines >= CaptureWindow::SourceVerticalTotalMin
        && lines <= CaptureWindow::SourceVerticalTotalMax;
}

bool SourceMeasurement::sampleSteady()
{
    uint16_t lines = measureSourceLines();

    if (!countIsSource(lines)) {
        steadyLines_ = lines;
        steadyRun_ = 0;
        return false;
    }

    if (lines != steadyLines_) {
        steadyLines_ = lines;
        steadyRun_ = 1;
        return false;
    }

    if (steadyRun_ < SteadySamples)
        ++steadyRun_;
    return steadyRun_ >= SteadySamples;
}

void SourceMeasurement::resetSteadiness()
{
    steadyRun_ = 0;
    steadyLines_ = 0;
    agreedRateHz_ = 0.0f;
    rateAttempts_ = 0;
}

bool SourceMeasurement::rateSettled()
{
    float previous = agreedRateHz_;
    agreedRateHz_ = fieldRateHz_;

    if (rateAttempts_ < RateAgreementAttempts)
        ++rateAttempts_;

    if (previous > 0.0f && fieldRateHz_ > 0.0f) {
        float error = fieldRateHz_ > previous ? fieldRateHz_ / previous
                                              : previous / fieldRateHz_;
        if (error < 1.0f + (float)RateAgreementPerMille / 1000.0f)
            return true;
    }
    return rateAttempts_ >= RateAgreementAttempts;
}

// The line rate the source is running at, in Hz, or 0 when it cannot be
// measured -- field rate x source lines, the two quantities the divider is a
// function of.
//
// **A PLAIN BOUNDS CHECK IS NOT ENOUGH.** A field rate measured while the
// source is still settling after a preset load passes one comfortably -- 57.9
// Hz against a real 50.08 -- and the divider comes out proportionally wrong:
// PLLAD_MD 2204 where 2548 is due, on a source locked at 311 lines / 50.08 Hz.
// rateFollowsCount() rejects it: the count did not move, so the rate did not
// either. Both inputs are logged, because neither alone says which was at
// fault. docs/firmware-geometry-engine.md
bool SourceMeasurement::measureLineRate()
{
    sourceLines_ = measureSourceLines();
    fieldRateHz_ = getSourceFieldRate(0);
    lineRateHz_ = lineRateFrom(sourceLines_, fieldRateHz_);

    // Against the last reading that was GOOD, not the last one taken: a refusal
    // that cleared the held rate would disarm this for the pass after it.
    if (!rateFollowsCount(sourceLines_, lineRateHz_, goodLines_, goodLineRateHz_)
        && ++rateRejections_ < HeldRateRejectionLimit) {
        lineRateHz_ = 0;
    }

    if (lineRateHz_ != 0) {
        goodLines_ = sourceLines_;
        goodLineRateHz_ = lineRateHz_;
        rateRejections_ = 0;
    }

    char line[72];
    snprintf(line, sizeof(line), "sampling: %u lines x %u.%02u Hz -> line rate %u",
             (unsigned)sourceLines_, (unsigned)fieldRateHz_,
             (unsigned)(fieldRateHz_ * 100) % 100, (unsigned)lineRateHz_);
    tv5725Log(line);

    return lineRateHz_ != 0;
}

bool SourceMeasurement::solve(uint32_t lineRateHz, uint8_t oversample)
{
    uint16_t chosen = recommendedDivider(lineRateHz, oversample, lineDoubled_);
    if (chosen == 0)
        return false;
    divider_ = chosen;
    return true;
}

bool SourceMeasurement::usable() const { return divider_ != 0; }

uint16_t SourceMeasurement::divider() const { return divider_; }

uint32_t SourceMeasurement::lineRateHz() const { return lineRateHz_; }

uint16_t SourceMeasurement::sourceLines() const { return sourceLines_; }

uint16_t SourceMeasurement::steadyLines() const { return steadyLines_; }

float SourceMeasurement::fieldRateHz() const { return fieldRateHz_; }

uint16_t SourceMeasurement::ifLine() const { return ifLineFor(divider_, lineDoubled_); }

void SourceMeasurement::forgetSource()
{
    lineRateHz_ = 0;
    goodLineRateHz_ = 0;
    goodLines_ = 0;
}

uint16_t SourceMeasurement::referenceDivider(bool lineDoubled)
{
    const uint16_t limit = lineDoubled ? (uint16_t)(2 * InputLine::WriteLimitUnits)
                                       : InputLine::WriteLimitUnits;
    // Even, for the reason recommendedDivider() masks: an odd divider leaves
    // the input formatter half a sample out from the line the ADC delivers, and
    // the rate is timed off that block. WriteLimitUnits is odd, so only the
    // progressive reference needs it.
    return (uint16_t)(limit & ~1u);
}

void SourceMeasurement::holdDivider(uint16_t divider) { divider_ = divider; }

uint32_t SourceMeasurement::estimatedLineRateHz() const
{
    if (steadyLines_ != 0)
        return (uint32_t)steadyLines_ * NominalFieldRateHz;
    return goodLineRateHz_;
}

uint32_t SourceMeasurement::heldLineRateHz() const { return goodLineRateHz_; }

bool SourceMeasurement::lowLineRate() const
{
    return heldLineRateHz() != 0 && heldLineRateHz() < LowLineRateBelowHz;
}

bool SourceMeasurement::lineDoublingFor(uint16_t sourceLines,
                                       uint16_t showableUnits)
{
    if (sourceLines == 0)
        return true;
    if (sourceLines >= LineDoubleBelowLines)
        return false;
    if (showableUnits == 0)
        return true;

    // The IF counts half-lines with the doubler in, so the doubled frame asks
    // for twice the source's own count.
    return 2u * ((uint32_t)sourceLines + 1u) <= showableUnits;
}

void SourceMeasurement::holdLineDoubling(bool lineDoubled) { lineDoubled_ = lineDoubled; }

bool SourceMeasurement::lineDoubled() const { return lineDoubled_; }

uint16_t SourceMeasurement::retimeStop() const { return retimeStopFor(divider_); }

uint16_t SourceMeasurement::measureSourceLines()
{
    return GBS::STATUS_SYNC_PROC_VTOTAL::read();
}

uint16_t SourceMeasurement::measureLineSamples()
{
    return GBS::STATUS_SYNC_PROC_HTOTAL::read();
}

bool SourceMeasurement::dividerLatched(uint16_t lineSamples, uint16_t divider,
                                       uint16_t tolerance)
{
    if (divider == 0)
        return false;

    uint16_t larger = lineSamples > divider ? lineSamples : divider;
    uint16_t smaller = lineSamples > divider ? divider : lineSamples;
    return (uint16_t)(larger - smaller) <= tolerance;
}

bool SourceMeasurement::dividerLatched() const
{
    return dividerLatched(measureLineSamples(), divider_);
}

uint16_t SourceMeasurement::measureSourceLinesCorrected(uint16_t divider)
{
    const uint16_t lines = measureSourceLines();
    if (countIsSource(lines))
        return lines;

    const uint8_t multiple = linesPerCount(measureLineSamples(), divider);
    if (multiple == 0)
        return lines;

    const uint32_t corrected = (uint32_t)lines * multiple;
    return countIsSource(corrected) ? (uint16_t)corrected : lines;
}

uint8_t SourceMeasurement::linesPerCount(uint16_t lineSamples, uint16_t divider)
{
    if (divider == 0 || lineSamples == 0)
        return 0;

    for (uint8_t lines = 2; lines <= LinesPerCountMax; ++lines) {
        uint32_t wanted = (uint32_t)divider * lines;
        uint32_t apart = lineSamples > wanted ? lineSamples - wanted
                                              : wanted - lineSamples;
        if (apart <= (uint32_t)LatchedSamplesTolerance * lines)
            return lines;
    }
    return 0;
}

// How much of the line the hsync pulse takes, in ADC samples -- the same space
// the divider is in, which is why the denominator is the divider and never
// STATUS_SYNC_PROC_HTOTAL, that being an echo of PLLAD_MD.
uint16_t SourceMeasurement::measureHsyncLow()
{
    return GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
}

}  // namespace Tv5725
