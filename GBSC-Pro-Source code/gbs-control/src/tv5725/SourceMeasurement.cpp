#include "SourceMeasurement.h"

#include <stdio.h>

#include "VideoSourceLine.h"   // the capture write limit, likewise
#include "Adc.h"             // the sample rate, which the divider is half of
#include "InputFormatter.h"   // the line counter, in the units the divider sets
#include "ModeDetect.h"   // whether the source is interlaced, which it measures
#include "SyncProcessor.h"   // SP_EXT_SYNC_SEL, the path this switches


namespace Tv5725 {

const uint16_t SourceMeasurement::LatchedSamplesTolerance;
const uint8_t SourceMeasurement::LinesPerCountMax;

bool SourceMeasurement::heldRateJudges(uint16_t lines, uint16_t heldLines,
                                       uint32_t heldLineRateHz)
{
    return heldLineRateHz != 0 && lines == heldLines;
}

// The counter's reading, with the field rate derived back out of it so both
// halves describe the same frame. Over the frame, not the count: VTOTAL is zero
// based, and this is the inverse of what VideoSignal::lineRateFor() does.
void SourceMeasurement::takeCounterRate(uint32_t lineRateHz)
{
    lineRateHz_ = lineRateHz;
    fieldRateHz_ = (float)lineRateHz_ / (float)(sourceLines_ + 1);
}

bool SourceMeasurement::heldRateCorroborates(uint32_t lineRateHz) const
{
    return heldRateJudges(sourceLines_, goodLines_, goodLineRateHz_)
           && VideoSignal::ratesAgree(lineRateHz, goodLineRateHz_);
}

bool SourceMeasurement::rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                         uint16_t heldLines, uint32_t heldLineRateHz)
{
    if (lineRateHz == 0 || !heldRateJudges(lines, heldLines, heldLineRateHz))
        return true;

    return VideoSignal::ratesAgree(lineRateHz, heldLineRateHz);
}

uint32_t SourceMeasurement::lineRateForHPeriod(uint16_t hperiod)
{
    return 27000000u / (((uint32_t)hperiod + 1u) * 4u);
}

uint32_t SourceMeasurement::lineRateFromHPeriod(const uint16_t *samples, uint8_t count,
                                                uint16_t lines, bool htBadSeen)
{
    if (samples == nullptr || count < 2 || !VideoSignal::countIsSource(lines) || htBadSeen)
        return 0;

    uint16_t low = samples[0];
    uint16_t high = samples[0];
    for (uint8_t i = 1; i < count; ++i) {
        if (samples[i] < low)
            low = samples[i];
        if (samples[i] > high)
            high = samples[i];
    }
    if ((uint16_t)(high - low) > HPeriodAgreement)
        return 0;

    const uint32_t rate = lineRateForHPeriod(samples[0]);
    if (rate < LineRateFloorHz)
        return 0;

    if (!VideoSignal::fieldRateIsSource((float)rate / (float)(lines + 1)))
        return 0;
    return rate;
}

// --- the chosen divider, held ----------------------------------------------

const uint8_t SourceMeasurement::NominalFieldRateHz;
const uint8_t SourceMeasurement::SteadySamples;
const uint16_t SourceMeasurement::RateAgreementPerMille;
const uint8_t SourceMeasurement::RateAgreementAttempts;

SourceMeasurement::SourceMeasurement()
    : divider_(0), lineRateHz_(0), sourceLines_(0), fieldRateHz_(0.0f),
      agreedRateHz_(0.0f), goodLines_(0), goodLineRateHz_(0),
      rateRejections_(0), lineDoubled_(true), steady_(SteadySamples),
      verticalPeriod_(0), rateAttempts_(0), serrationsSeen_(false),
      referenceRateHz_(0)
{
}

bool SourceMeasurement::countWasSerrations() const
{
    return serrationsSeen_;
}

bool SourceMeasurement::countIsSerrations(uint16_t lines, uint16_t halfLines,
                                          bool interlaced)
{
    if (!interlaced)
        return false;

    const uint16_t frameLines = (uint16_t)(halfLines / 2);
    if (!VideoSignal::countIsSource(frameLines))
        return false;

    const int32_t toHalfLines = (int32_t)lines - (int32_t)halfLines;
    const int32_t toFrame = (int32_t)lines - (int32_t)frameLines;
    const int32_t fromHalfLines = toHalfLines < 0 ? -toHalfLines : toHalfLines;
    const int32_t fromFrame = toFrame < 0 ? -toFrame : toFrame;
    return fromHalfLines < fromFrame;
}

SourceMeasurement::ScanType SourceMeasurement::scanTypeFor(uint16_t verticalPeriod,
                                                          bool lineDoubled)
{
    const uint16_t lines = lineDoubled ? (uint16_t)(verticalPeriod / 2)
                                       : verticalPeriod;
    if (!VideoSignal::countIsSource(lines))
        return ScanUnknown;

    const bool carriesHalfLine = (verticalPeriod % 2 != 0) != lineDoubled;
    return carriesHalfLine ? ScanInterlaced : ScanProgressive;
}

bool SourceMeasurement::countAlternated() const { return steady_.alternated(); }

SourceMeasurement::ScanType SourceMeasurement::scanTypeFrom(uint16_t verticalPeriod) const
{
    const ScanType measured = scanTypeFor(verticalPeriod, lineDoubled_);
    if (measured != ScanUnknown)
        return measured;
    return countAlternated() ? ScanInterlaced : ScanUnknown;
}

SourceMeasurement::ScanType SourceMeasurement::measureScanType()
{
    verticalPeriod_ = InputFormatter::verticalPeriod();
    return scanTypeFrom(verticalPeriod_);
}

uint16_t SourceMeasurement::verticalPeriod() const { return verticalPeriod_; }

bool SourceMeasurement::sampleSteady()
{
    uint16_t lines = SyncProcessor::lineCount();

    if (!VideoSignal::countIsSource(lines)) {
        steady_.restart(lines);
        return false;
    }

    if (!steady_.sample(lines))
        return false;

    verticalPeriod_ = InputFormatter::verticalPeriod();
    if (countIsSerrations(lines, verticalPeriod_,
                          ModeDetect::sourceIsInterlaced())) {
        serrationsSeen_ = true;
        steady_.restart(lines);
        return false;
    }
    serrationsSeen_ = false;
    return true;
}

void SourceMeasurement::modeChanged()
{
    steady_.reset();
    agreedRateHz_ = 0.0f;
    rateAttempts_ = 0;
}

void SourceMeasurement::forgetHeldRate()
{
    goodLines_ = 0;
    goodLineRateHz_ = 0;
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
    sourceLines_ = SyncProcessor::lineCount();

    // HPERIOD_IF states the line rate for the cost of a register read, where
    // TestBusRateMeasurement::sourceFieldRateHz() spins for vsync edges. It also rails to a value that
    // is WRONG AND STABLE, and every test lineRateFromHPeriod() applies is
    // passed by one -- so it is believed only where something corroborates it.
    // docs/investigations/hperiod-if-railing.md
    const uint32_t fromCounter = measureLineRateFromHPeriod(sourceLines_);

    if (fromCounter != 0 && heldRateCorroborates(fromCounter)) {
        // Free, and the settled case: the rate already held stands behind it.
        takeCounterRate(fromCounter);
    } else {
        // The field rate is measured a different way and does not rail with the
        // counter. It answers where the counter disagrees, and where it cannot
        // be measured NOTHING is -- a reading nothing can speak to is the one
        // form the window's tests cannot judge.
        fieldRateHz_ = TestBusRateMeasurement::sourceFieldRateHz(false);
        lineRateHz_ = VideoSignal::isVideo(sourceLines_, fieldRateHz_)
            ? VideoSignal::lineRateFor(sourceLines_, fieldRateHz_) : 0;
        if (fromCounter != 0 && lineRateHz_ != 0
            && VideoSignal::ratesAgree(fromCounter, lineRateHz_))
            takeCounterRate(fromCounter);
    }

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

SourceMeasurement::MeasurementStatus SourceMeasurement::measure()
{
    if (!sampleSteady())
        return countWasSerrations() ? Serrations : NotSteady;

    if (!measureLineRate())
        return Unmeasurable;

    // Last, because the duty is counted against the divider and the divider is
    // what the readings above were taken through.
    hsync_ = readSource();

    return rateSettled() ? Measured : Settling;
}

HsyncPulse SourceMeasurement::hsync() const { return hsync_; }

bool SourceMeasurement::usable() const { return divider_ != 0; }

uint16_t SourceMeasurement::divider() const { return divider_; }

uint32_t SourceMeasurement::lineRateHz() const { return goodLineRateHz_; }

uint16_t SourceMeasurement::readSourceLines() const
{
    return measureSourceLinesCorrected(divider_);
}

HsyncPulse SourceMeasurement::readSource() const
{
    // The duty rather than the register, because the divider this was counted
    // against is about to move. HsyncPulse.h.
    const float duty = divider_ > 0
        ? (float)SyncProcessor::hsyncLowSamples() / (float)divider_ : 0.0f;
    return HsyncPulse(duty, SyncProcessor::hsyncPositive());
}

uint16_t SourceMeasurement::sourceLines() const { return sourceLines_; }

uint16_t SourceMeasurement::steadyLines() const { return steady_.value(); }

float SourceMeasurement::fieldRateHz() const { return fieldRateHz_; }

uint16_t SourceMeasurement::ifLine() const
{
    return InputFormatter::lineCounterFor(
        divider_, lineDoubled_);
}

uint16_t SourceMeasurement::referenceDivider(bool lineDoubled)
{
    const uint16_t limit = lineDoubled ? (uint16_t)(2 * VideoSourceLine::WriteLimitUnits)
                                       : VideoSourceLine::WriteLimitUnits;
    // Even, for the reason SamplingClock::recommendedDivider() masks: an odd
    // divider leaves
    // the input formatter half a sample out from the line the ADC delivers, and
    // the rate is timed off that block. WriteLimitUnits is odd, so only the
    // progressive reference needs it.
    return (uint16_t)(limit & ~1u);
}

void SourceMeasurement::holdDivider(uint16_t divider) { divider_ = divider; }

uint32_t SourceMeasurement::estimatedLineRateHz() const
{
    if (steady_.value() != 0)
        return (uint32_t)steady_.value() * NominalFieldRateHz;
    return goodLineRateHz_;
}

bool SourceMeasurement::lowLineRate() const
{
    return lineRateHz() != 0 && lineRateHz() < LowLineRateBelowHz;
}

void SourceMeasurement::holdLineDoubling(bool lineDoubled) { lineDoubled_ = lineDoubled; }

bool SourceMeasurement::lineDoubled() const { return lineDoubled_; }

uint16_t SourceMeasurement::retimeStop() const
{
    return SyncProcessor::retimeStopFor(divider_);
}

void SourceMeasurement::applyReferenceSampling()
{
    const uint16_t reference = referenceDivider(lineDoubled_);
    const uint32_t estimate = estimatedLineRateHz();

    // Unconditional, ahead of the return below. The reference divider is a
    // function of the scan mode alone, so a source that did not move asks for
    // the one already in force -- and a window is not only stranded by a mode
    // change. Nothing else writes these two until a solve succeeds, which is
    // the thing they are stopping.
    InputFormatter::writeReferenceVerticalBlank();

    if (divider_ == reference && estimate == referenceRateHz_)
        return;

    referenceRateHz_ = estimate;
    holdDivider(reference);

    // The most the clock allows, rather than whatever the output mode is
    // running: a reference that follows a picture setting is not a reference.
    Adc::applySampleRate(reference, estimate, Adc::OversampleAsClockAllows);
    InputFormatter::writeLineCounter(ifLine());
    SyncProcessor::writeRetimeStop(retimeStop());
}

uint32_t SourceMeasurement::measureLineRateFromHPeriod(uint16_t lines)
{
    uint16_t hperiod[HPeriodSamples];
    bool htBadSeen = false;
    for (uint8_t i = 0; i < HPeriodSamples; ++i) {
        hperiod[i] = InputFormatter::linePeriod();
        if (InputFormatter::lineCounterFlagged())
            htBadSeen = true;
    }
    return lineRateFromHPeriod(hperiod, HPeriodSamples, lines, htBadSeen);
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
    return dividerLatched(SyncProcessor::lineSamples(), divider_);
}

uint16_t SourceMeasurement::measureSourceLinesCorrected(uint16_t divider)
{
    const uint16_t lines = SyncProcessor::lineCount();
    if (VideoSignal::countIsSource(lines))
        return lines;

    const uint8_t multiple = linesPerCount(SyncProcessor::lineSamples(), divider);
    if (multiple == 0)
        return lines;

    const uint32_t corrected = (uint32_t)lines * multiple;
    return VideoSignal::countIsSource(corrected) ? (uint16_t)corrected : lines;
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

}  // namespace Tv5725
