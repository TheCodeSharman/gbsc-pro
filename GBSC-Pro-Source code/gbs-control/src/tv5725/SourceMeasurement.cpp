#include "SourceMeasurement.h"

#include "Tv5725Log.h"

#include <stdio.h>

#include "Adc.h"             // the divider the duty is counted against
#include "InputFormatter.h"   // the line and frame periods, counted at 27 MHz
#include "ModeDetect.h"   // whether the source is interlaced, which it measures
#include "SyncMeasurement.h"   // whether the arrangement can state a polarity
#include "SyncOnGreen.h"
#include "SyncProcessor.h"   // SP_EXT_SYNC_SEL, the path this switches


namespace Tv5725 {

const uint8_t SourceMeasurement::LinesPerCountMax;

bool SourceMeasurement::heldRateJudges(uint16_t lines, uint16_t heldLines,
                                       uint32_t heldLineRateHz)
{
    return heldLineRateHz != 0 && lines == heldLines;
}

bool SourceMeasurement::rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                         uint16_t heldLines, uint32_t heldLineRateHz)
{
    if (lineRateHz == 0 || !heldRateJudges(lines, heldLines, heldLineRateHz))
        return true;

    return VideoSignal::ratesAgree(lineRateHz, heldLineRateHz,
                                   RateFollowsCountPerThousand);
}

// HPERIOD_IF IS A CHANGE DETECTOR AND NOTHING ELSE, AND SAYING SO IS THE POINT.
// It rails, and it reads values that are plainly wrong and perfectly steady --
// 511 on a 311-line source at 50 Hz is 13183 Hz against a real 15625, held
// across every sample, which no run can reject. Asking it what the rate IS
// therefore cannot be made safe.
//
// Asking whether the reading MOVED can, because the comparison is against its
// own earlier value: a bias cancels, a rail that stays railed compares equal,
// and a rail the reference was itself taken from still shows the move away
// from it. It counts against the chip's own 27 MHz rather than the ADC clock,
// so a correct reading is the same number whatever the divider -- which is what
// lets one reference outlive the solves that move it. What the rate actually is comes from the field rate afterwards,
// measured a different way and only where this says something changed.
// docs/investigations/hperiod-if-railing.md
uint16_t SourceMeasurement::settledLinePeriod()
{
    const uint16_t first = inputFormatter_.linePeriod();
    uint16_t low = first, high = first;
    for (uint8_t i = 1; i < HPeriodSamples; ++i) {
        const uint16_t sample = inputFormatter_.linePeriod();
        if (sample < low)
            low = sample;
        if (sample > high)
            high = sample;
    }
    if ((uint16_t)(high - low) > HPeriodAgreement)
        return 0;
    return first;
}

// STATUS_IF_HT_BAD is deliberately not consulted. It is a one-sided gate on the
// same unreliable block, and stuck set it would refuse every reading for ever --
// which is a source change this can never see rather than a false one it
// avoids. A reading that survives here is corroborated before anything acts on
// it, so a false positive costs a measurement and never a wrong solve.
bool SourceMeasurement::hasLineRateMoved(uint16_t reference)
{
    if (reference == 0)
        return false;

    const uint16_t now = settledLinePeriod();
    if (now == 0)
        return false;

    return !VideoSignal::ratesAgree(now, reference, LinePeriodMovedPerThousand);
}

// --- the chosen divider, held ----------------------------------------------

const uint8_t SourceMeasurement::SteadySamples;
const uint16_t SourceMeasurement::RateAgreementPerThousand;
const uint16_t SourceMeasurement::RateFollowsCountPerThousand;
const uint16_t SourceMeasurement::LinePeriodMovedPerThousand;
const uint8_t SourceMeasurement::RateAgreementAttempts;
const uint16_t SourceMeasurement::LatchSettleMs;
const uint32_t SourceMeasurement::UnconfirmedRatePulses;

SourceMeasurement::SourceMeasurement(InputFormatter &inputFormatter)
    : inputFormatter_(inputFormatter), lineRateHz_(0), sourceLines_(0),
      verticalSyncLines_(0), offeredVerticalSyncLines_(0), fieldRateHz_(0.0f),
      agreedRateHz_(0.0f), judgedLines_(0), judgedRateHz_(0), goodLineRateHz_(0),
      rateRejections_(0), hsyncPolarity_(SourceKey::Undetermined),
      vsyncPolarity_(SourceKey::Undetermined), verticalPeriod_(0),
      rateConfirmed_(false), probesAgreed_(false), rateTaken_(false),
      dutyMeasured_(false), settleArmed_(false), settleTiming_(false),
      settleStartedMs_(0),
      steady_(SteadySamples), scanSteady_(SteadySamples), rateAttempts_(0),
      scanReported_(-1)
{
}

bool SourceMeasurement::countAlternated() const { return scanSteady_.alternated(); }

// What the deinterlacer is steered by, and the one place it is decided. Said
// out loud on change, because the alternative is reading it back off the
// deinterlacer's own registers -- which report what was done, not what was
// measured, and which cost two sessions each time they disagreed.
SourceMeasurement::ScanType SourceMeasurement::measureScanType()
{
    verticalPeriod_ = inputFormatter_.verticalPeriod();

    const uint16_t lines = countNow();
    if (VideoSignal::countIsSource(lines))
        scanSteady_.sample(lines);
    else
        scanSteady_.restart(lines);

    const ScanType scan = countAlternated() ? ScanInterlaced : ScanProgressive;
    if (scan != scanReported_) {
        scanReported_ = (int8_t)scan;
        char line[72];
        snprintf(line, sizeof(line), "scan: %s, count %u%s",
                 scan == ScanInterlaced ? "interlaced" : "progressive",
                 (unsigned)scanSteady_.value(),
                 scanSteady_.settled() ? "" : ", no settled count");
        tv5725Log(line);
    }
    return scan;
}

SourceMeasurement::ScanType SourceMeasurement::scanType() const
{
    return scanReported_ == (int8_t)ScanInterlaced ? ScanInterlaced
                                                   : ScanProgressive;
}

bool SourceMeasurement::scanKnown() const { return scanSteady_.settled(); }

uint16_t SourceMeasurement::verticalPeriod() const { return verticalPeriod_; }

uint16_t SourceMeasurement::countFrom(uint16_t correctedLines) const
{
    return (uint16_t)(correctedLines + verticalSyncLines_);
}

uint16_t SourceMeasurement::countNow() const
{
    return countFrom(measureSourceLinesCorrected(Adc::dividerInForce()));
}

bool SourceMeasurement::sampleSteady()
{
    uint16_t lines = readSourceLines();

    if (!VideoSignal::countIsSource(lines)) {
        steady_.restart(lines);
        return false;
    }

    if (!steady_.sample(lines))
        return false;

    verticalPeriod_ = inputFormatter_.verticalPeriod();
    return true;
}

void SourceMeasurement::modeChanged()
{
    rateTaken_ = false;
    probesAgreed_ = false;
    steady_.reset();
    scanSteady_.reset();
    agreedRateHz_ = 0.0f;
    rateAttempts_ = 0;
    dutyMeasured_ = false;
    verticalSyncLines_ = 0;
    offeredVerticalSyncLines_ = 0;
}

void SourceMeasurement::samplingClockLatched()
{
    settleArmed_ = true;
    settleTiming_ = false;
}

bool SourceMeasurement::clockSettling(uint32_t nowMs)
{
    if (settleArmed_) {
        settleArmed_ = false;
        settleTiming_ = true;
        settleStartedMs_ = nowMs;
        return true;
    }

    if (!settleTiming_)
        return false;

    if (nowMs - settleStartedMs_ < LatchSettleMs)
        return true;

    settleTiming_ = false;

    // The figure and not just the fact: it is what separates a gate that is
    // too short from one that never ran.
    char line[48];
    snprintf(line, sizeof(line), "settle: spent after %lu ms",
             (unsigned long)(nowMs - settleStartedMs_));
    tv5725Log(line);
    return false;
}

// **WHAT A LATER READING IS JUDGED AGAINST MUST NOT ITSELF BE A TRANSIENT.**
// rateFollowsCount() accepts anything at a moved count, because a moved count
// IS a mode change -- so a mid-change reading admitted there can become the
// rate every correct one afterwards is refused against. Measured on the bench,
// 320x256@50 -> 640x480@60: `524 lines x 45.98 Hz` was taken, and the 59
// readings of the real 60.36 Hz that followed were all refused, 2.28 s of them,
// until HeldRateRejectionLimit drained.
void SourceMeasurement::takeJudgedRate()
{
    rateTaken_ = true;
    judgedLines_ = sourceLines_;
    judgedRateHz_ = lineRateHz_;
    rateRejections_ = 0;
}

float SourceMeasurement::settledFieldRateHz() const
{
    if (judgedRateHz_ == 0 || judgedLines_ == 0)
        return 0.0f;

    return (float)judgedRateHz_ / (float)(judgedLines_ + 1);
}

bool SourceMeasurement::rateConfirmed() const { return rateConfirmed_; }

void SourceMeasurement::confirmRate() { rateConfirmed_ = true; }

bool SourceMeasurement::probesAgreed() const { return probesAgreed_; }

void SourceMeasurement::forgetHeldRate()
{
    judgedLines_ = 0;
    judgedRateHz_ = 0;
    goodLineRateHz_ = 0;
}

float SourceMeasurement::medianOfThree(float a, float b, float c)
{
    const float low = a < b ? a : b;
    const float high = a < b ? b : a;
    if (c < low)
        return low;
    return c < high ? c : high;
}

// A single pulse timed on a CPU that takes interrupts reads percent high often
// enough to reach the key, which nothing re-judges afterwards.
// docs/investigations/single-sample-rate-jitter.md
float SourceMeasurement::sampleFieldRateHz()
{
    // **A MEDIAN REJECTS AN OUTLIER AND NOT A SPREAD**, and a rate nothing has
    // confirmed can be a spread rather than a reading with noise on it: a sync
    // separator inventing and dropping edges makes each reading span k fields,
    // so three readings land on three different multiples and the middle one is
    // a multiple. Measured on a component source at 480p, the true rate is in
    // 14% of readings and a median of three returns it in about 5% of trials.
    // The estimator that finds it asks which value RECURS.
    if (!probesAgreed_) {
        bool agreed = false;
        const float hz = recurringFieldRateHz(&agreed);
        probesAgreed_ = agreed && hz > 0.0f;
        return hz;
    }

    // Every probe agreed, so the readings differ by the instrument's own spread
    // and a median is the right answer. A source that
    // did not pulse has nothing for the other two to time, and a sample that
    // reports none has already waited out two timeouts.
    const float first = TestBusRateMeasurement::sourceFieldRateHz(false, 1);
    if (first == 0.0f)
        return 0.0f;

    const float second = TestBusRateMeasurement::sourceFieldRateHz(false, 1);
    const float third = TestBusRateMeasurement::sourceFieldRateHz(false, 1);
    return medianOfThree(first, second, third);
}

bool SourceMeasurement::sourceIsMeasurable()
{
    const uint16_t lines = SyncProcessor::lineCount();

    uint32_t readings[MeasurableReads];
    bool video = true;
    for (uint8_t i = 0; i < MeasurableReads; ++i) {
        const float hz =
            TestBusRateMeasurement::sourceFieldRateHz(false, MeasurablePulses);
        readings[i] = (uint32_t)(hz * 1000.0f + 0.5f);
        if (!VideoSignal::isVideo(lines, hz))
            video = false;
    }

    bool agree = true;
    for (uint8_t i = 1; i < MeasurableReads; ++i)
        if (!VideoSignal::ratesAgree(readings[0], readings[i],
                                     MeasurableAgreementPerThousand))
            agree = false;

    char line[80];
    snprintf(line, sizeof(line), "sog,%u,%u,%lu,%lu,%lu,%u",
             (unsigned)SyncOnGreen::level(), (unsigned)lines,
             (unsigned long)readings[0], (unsigned long)readings[1],
             (unsigned long)readings[2], (unsigned)(video && agree));
    tv5725Log(line);

    return video && agree;
}

float SourceMeasurement::recurringFieldRateHz(bool *everyProbeAgreed)
{
    if (everyProbeAgreed != NULL)
        *everyProbeAgreed = false;

    uint32_t mHz[RecurringRateSamples];
    uint8_t taken = 0;
    for (uint8_t i = 0; i < RecurringRateSamples; ++i) {
        const float hz = TestBusRateMeasurement::sourceFieldRateHz(false, 1);

        // A SOURCE THAT DID NOT PULSE HAS NOTHING FOR THE REST TO TIME, and
        // each reading that reports none has already waited out two timeouts --
        // so a silent source must cost one reading and not a run of them. The
        // first answers whether anything is pulsing at all; a later zero is a
        // dropped reading and the run carries on without it.
        if (hz == 0.0f) {
            if (i == 0)
                return 0.0f;
            continue;
        }
        mHz[taken++] = (uint32_t)(hz * 1000.0f + 0.5f);
    }

    uint8_t bestAt = 0;
    uint8_t best = 0;
    for (uint8_t i = 0; i < taken; ++i) {
        uint8_t agreeing = 0;
        for (uint8_t j = 0; j < taken; ++j)
            if (VideoSignal::ratesAgree(mHz[i], mHz[j], RecurringRatePerThousand))
                ++agreeing;
        if (agreeing > best) {
            best = agreeing;
            bestAt = i;
        }
    }

    if (best < RecurringRateAgreements)
        return 0.0f;

    if (everyProbeAgreed != NULL)
        *everyProbeAgreed = best == taken;

    // WHICH MULTIPLE IS THE TRUTH IS WHAT THE SINGLE PULSES ANSWERED; HOW
    // PRECISELY IT IS KNOWN IS NOT. One pulse carries the whole of both edge
    // ISRs' latency -- 3.2 per thousand at 311 lines -- which is wider than the
    // tolerance a boot's rate is later confirmed against, so a spanned reading
    // is taken and kept only if it lands in the cluster the probes found. That
    // is the test a span alone cannot do: a spanned reading on this source is
    // wrong 86% of the time and says nothing about it.
    const float spanned =
        TestBusRateMeasurement::sourceFieldRateHz(false, UnconfirmedRatePulses);
    const uint32_t spannedMHz = (uint32_t)(spanned * 1000.0f + 0.5f);
    if (spanned > 0.0f
        && VideoSignal::ratesAgree(mHz[bestAt], spannedMHz, RecurringRatePerThousand))
        return spanned;

    // The cluster's mean, so one pulse's latency averages down across the
    // readings that agreed rather than being carried by whichever was picked.
    uint32_t total = 0;
    uint8_t counted = 0;
    for (uint8_t j = 0; j < taken; ++j)
        if (VideoSignal::ratesAgree(mHz[bestAt], mHz[j], RecurringRatePerThousand)) {
            total += mHz[j];
            ++counted;
        }
    return (float)total / (float)counted / 1000.0f;
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
        if (error < 1.0f + (float)RateAgreementPerThousand / 1000.0f)
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
    sourceLines_ = readSourceLines();

    // ONE MEASUREMENT, so nothing about the answer depends on which of two
    // happened to be taken. HPERIOD_IF states the line rate for a register read
    // where this spins for vsync edges, and it was preferred for that -- but at
    // 800x600 it reads 38135 Hz where the field rate gives 37878 and DMT states
    // 37879. The 0.68% sits inside the 2% the corroboration allowed, so the
    // counter won and put the source a whole hertz out, 60.72 against 60.32.
    // The key is rounded to a whole hertz and the raster is generated from it,
    // so which measurement answered decided the framing.
    // docs/investigations/hperiod-if-railing.md
    fieldRateHz_ = sampleFieldRateHz();
    lineRateHz_ = VideoSignal::isVideo(sourceLines_, fieldRateHz_)
        ? VideoSignal::lineRateFor(sourceLines_, fieldRateHz_) : 0;

    // Against the last reading that was GOOD, not the last one taken: a refusal
    // that cleared the held rate would disarm this for the pass after it.
    if (!rateFollowsCount(sourceLines_, lineRateHz_, judgedLines_, judgedRateHz_)
        && ++rateRejections_ < HeldRateRejectionLimit) {
        lineRateHz_ = 0;
    }

    if (lineRateHz_ != 0)
        goodLineRateHz_ = lineRateHz_;

    // THE CLOCK THE READING WAS TAKEN THROUGH, WITH THE READING. The count and
    // the field rate survive a divider the PLL never latched -- measured, a
    // correct 524 lines and 59.93 Hz with STATUS_SYNC_PROC_HTOTAL reading 425
    // against a divider of 2040 -- so a reading can look healthy while the
    // pipeline runs the 27 MHz reference instead. Nothing else on the board
    // reports a missing latch.
    const uint16_t divider = Adc::dividerInForce();
    const uint16_t lineSamples = SyncProcessor::lineSamples();
    char line[128];
    snprintf(line, sizeof(line),
             "measured: %u lines x %u.%02u Hz -> line rate %u, "
             "clock %u@%lu htotal %u%s",
             (unsigned)sourceLines_, (unsigned)fieldRateHz_,
             (unsigned)(fieldRateHz_ * 100) % 100, (unsigned)lineRateHz_,
             (unsigned)divider, (unsigned long)Adc::rateInForce(),
             (unsigned)lineSamples,
             Adc::dividerLatched(lineSamples) ? "" : " UNLATCHED");
    tv5725Log(line);

    return lineRateHz_ != 0;
}

bool SourceMeasurement::normalisePolarity()
{
    const bool positive = SyncProcessor::hsyncPositive();
    SyncProcessor::normaliseHsyncPolarity(SyncProcessor::hsyncFound(), positive);
    return positive;
}

SourceMeasurement::MeasurementStatus SourceMeasurement::measureRate(uint32_t nowMs)
{
    // BEFORE EVERYTHING, INCLUDING THE SETTLE. A positive-going hsync reaches
    // the counter as the line minus the pulse, and the rate measurement is one
    // of the things that defeats -- so leaving this to measureDuty() puts the
    // correction downstream of a measurement that cannot succeed without it.
    normalisePolarity();

    // Ahead of the steadiness run as well as of the duty: the count is
    // corrected against the divider in force, so a run gathering samples while
    // the processor still counts the previous line fills with readings the
    // correction cannot judge.
    if (clockSettling(nowMs))
        return ClockSettling;

    // TAKEN, AND THE DUTY STILL OUTSTANDING, so what is left of this
    // measurement is the duty. The divider in force was derived from this rate,
    // so reading the rate again asks the chip for a value the engine itself
    // chose -- and stakes a completed acquisition on one reading of it, taken
    // through a clock that has just moved. The duty is the half that cannot
    // survive the install, it being a ratio of the pulse to the LINE in ADC
    // samples, so that is what the passes after a latch are for.
    //
    // ONLY UNTIL THE DUTY PASS RUNS, which is what releases it.
    // ../../../docs/investigations/the-rate-is-re-read-through-the-clock-it-chose.md
    if (rateTaken_) {
        char line[72];
        snprintf(line, sizeof(line),
                 "measure: rate held %u.%02u Hz, measuring duty",
                 (unsigned)fieldRateHz_, (unsigned)(fieldRateHz_ * 100) % 100);
        tv5725Log(line);
        return Measured;
    }

    if (!sampleSteady())
        return NotSteady;

    if (!measureLineRate())
        return Unmeasurable;

    if (!rateSettled())
        return Settling;

    takeJudgedRate();
    return Measured;
}

SourceMeasurement::MeasurementStatus SourceMeasurement::measureDuty(uint32_t nowMs)
{
    if (clockSettling(nowMs))
        return ClockSettling;

    // THE PAIR ENDS HERE, however the duty reads. What measureRate() holds back
    // is one re-read of a rate the divider in force was derived from, not the
    // rate for the rest of the mode change -- a source may change rate at an
    // unchanged count, and rateFollowsCount() is what refuses the transient.
    // Released on the pass RUNNING, not on the duty succeeding: a source whose
    // duty cannot be read at all would otherwise never let the rate be read
    // again.
    rateTaken_ = false;

    return readSource() ? Measured : Settling;
}

HsyncPulse SourceMeasurement::hsync() const { return hsync_; }

SourceKey::Polarity SourceMeasurement::hsyncPolarity() const { return hsyncPolarity_; }

SourceKey::Polarity SourceMeasurement::vsyncPolarity() const { return vsyncPolarity_; }

// Composite sync and sync on green state no polarity: measured on two modes the
// monitor definition gives as V positive, both bits read 0 on composite.
//
// The arrangement IN FORCE rather than whether it has been probed -- a held
// sync type nothing probed still says which path the pins are read through,
// and isSet() governs whether to probe again, which is a different question.
SourceKey::Polarity SourceMeasurement::polarityOf(bool positive)
{
    if (SyncMeasurement::isCsync())
        return SourceKey::Undetermined;
    return positive ? SourceKey::Positive : SourceKey::Negative;
}

uint32_t SourceMeasurement::lineRateHz() const { return goodLineRateHz_; }

const uint16_t SourceMeasurement::VerticalSyncMaxLines;

// Two registers measure the same frame and only one of them loses the vertical
// sync pulse, so the pair says how much was lost. Which multiple of the frame
// VPERIOD_IF holds is not derivable -- measured, it is one on some modes and
// two on others with the whole ADC clock group identical -- so both are tried
// and the one that lands just above the count is the reading.
uint16_t SourceMeasurement::reconciledFrame(uint16_t verticalPeriod, uint16_t lines)
{
    if (verticalPeriod == 0)
        return 0;

    const uint16_t measured = (uint16_t)(verticalPeriod + 1);
    const uint16_t counted = (uint16_t)(lines + 1);

    for (uint8_t factor = 1; factor <= 2; factor++) {
        if (measured % factor != 0)
            continue;
        const uint16_t frame = (uint16_t)(measured / factor);
        if (frame >= counted && (uint16_t)(frame - counted) <= VerticalSyncMaxLines)
            return frame;
    }

    return 0;
}

void SourceMeasurement::holdVerticalSync(uint16_t lines)
{
    const uint16_t frame = reconciledFrame(inputFormatter_.verticalPeriod(), lines);
    if (frame == 0)
        return;

    const uint16_t offered = (uint16_t)(frame - (lines + 1));
    if (offered == offeredVerticalSyncLines_)
        verticalSyncLines_ = offered;
    offeredVerticalSyncLines_ = offered;
}

uint16_t SourceMeasurement::readSourceLines()
{
    const uint16_t lines = measureSourceLinesCorrected(Adc::dividerInForce());
    holdVerticalSync(lines);

    return countFrom(lines);
}

bool SourceMeasurement::readSource()
{
    // NORMALISE BEFORE COUNTING. The count is the low time of the sync reaching
    // the counter, so on an uncorrected positive-going source it is the line
    // minus the pulse -- around 0.9, which forDuty() refuses.
    const bool found = SyncProcessor::hsyncFound();
    const bool positive = normalisePolarity();
    hsyncPolarity_ = polarityOf(positive);
    vsyncPolarity_ = polarityOf(SyncProcessor::vsyncPositive());

    // The duty rather than the register, because the divider this was counted
    // against is about to move. HsyncPulse.h.
    const uint16_t divider = Adc::dividerInForce();
    const uint16_t low = SyncProcessor::hsyncPulseSamples(divider, positive);
    const uint16_t lineSamples = SyncProcessor::lineSamples();
    const bool latched = Adc::dividerLatched(lineSamples);
    const float duty = divider > 0 ? (float)low / (float)divider : 0.0f;

    // Both sides of the ratio, because the divider the samples were counted at
    // is not necessarily the one this divides by, and no reading taken
    // afterwards can separate the two.
    char line[112];
    snprintf(line, sizeof(line), "duty: %u pulse / %u divider, htotal %u, %s%s%s",
             (unsigned)low, (unsigned)divider, (unsigned)lineSamples,
             positive ? "positive" : "negative", found ? "" : ", NO EDGE",
             latched ? (HsyncPulse(duty).isPulse() ? "" : ", NOT A PULSE")
                     : ", UNLOCKED");
    tv5725Log(line);

    return takeDuty(latched, HsyncPulse(duty));
}

// Whether the solve has a duty it can use, taking this reading if it is one.
//
// A count taken while the processor was counting another line length divides by
// a divider it never saw, so it is not a measurement and is never taken. What a
// locked pass gave stands; a source that has had none waits, because the only
// alternative is a plausible number -- 9.96% measured on a source whose duty is
// 7.03% -- that the window is then sized from for the life of the mode.
//
// Waiting once looked impossible, and a floor of passes was put under it. That
// was the reference clock being sized from a nominal field rate, which put the
// ADC PLL on a post divider row it could not hold, so a locked reading never
// arrived at all.
// docs/investigations/the-duty-is-counted-before-the-processor-relocks.md
bool SourceMeasurement::takeDuty(bool latched, const HsyncPulse &reading)
{
    if (latched && reading.isPulse()) {
        hsync_ = reading;
        dutyMeasured_ = true;
        return true;
    }
    return dutyMeasured_;
}

uint16_t SourceMeasurement::sourceLines() const { return sourceLines_; }

uint16_t SourceMeasurement::steadyLines() const { return steady_.value(); }

uint16_t SourceMeasurement::settledSourceLines() const
{
    return steady_.settled() ? steady_.value() : 0;
}

float SourceMeasurement::fieldRateHz() const { return fieldRateHz_; }


bool SourceMeasurement::lowLineRate() const
{
    return lineRateHz() != 0 && lineRateHz() < LowLineRateBelowHz;
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
        if (apart <= (uint32_t)Adc::LatchedSamplesTolerance * lines)
            return lines;
    }
    return 0;
}

}  // namespace Tv5725
