#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "HsyncPulse.h"
#include "SteadyRun.h"
#include "TestBusRateMeasurement.h"
#include "Tv5725Log.h"
#include "VideoSignal.h"

namespace Tv5725 {

// Measures the timing from the video source.
//
// One pass takes every quantity a solve needs and holds it as state, so a
// caller reads back rather than asking the chip again. The readings do not
// share a time base: the line period is counted against the chip's own 27 MHz,
// the field rate by the ESP off the test bus, and the hsync low time in ADC
// samples. One of them rails to a value that is wrong and steady, so returning
// a reading that can be believed is the whole job.
//
// The sampling clock is held here because two of those readings depend on it. A
// low time in ADC samples means nothing without the divider it was counted
// against, and a divider the ADC PLL cannot lock to corrupts the line count as
// well, by locking to every Nth hsync.
class SourceMeasurement {
public:
    SourceMeasurement();

    // Put the chip on the reference divider needed for a valid measurement to be taken.
    // Note this will corrupt the picture.
    void applyReferenceSampling(bool lineDoubled);

    // The source is about to move, so the steadiness run and the rate agreed
    // on mean nothing.
    void modeChanged();

    // Drops the rate HELD, which a mode change deliberately does not: that rate
    // is what refuses a settling transient, and a confirmed rate change is the
    // only thing that makes it the obstacle instead.
    void forgetHeldRate();

    enum MeasurementStatus {
        NotSteady,     // the count is still gathering samples
        Serrations,    // the settled count read the serrations, not the source
        Unmeasurable,  // nothing could speak for a line rate
        Settling,      // a rate, which has not repeated yet
        Measured,
    };

    // Measure the video source timings, holding them as state. Asked on every
    // pass. applyReferenceSampling() must be in force first.
    MeasurementStatus measure();

    enum ScanType {
        ScanUnknown,
        ScanProgressive,
        ScanInterlaced,
    };

    // The scan type, from the half line an interlaced field carries. Takes its
    // own reading, because an interlace change need not move the line count and
    // so need not arm a mode change -- a held one would be the last mode's.
    ScanType measureScanType(bool lineDoubled);

    // Measure the source's line count, corrected for a divider the ADC PLL
    // could not lock to. The first of the two measurement moments: the scan
    // mode is judged from this count and the reference sampling clock follows
    // the scan mode, so it is taken BEFORE that clock and measure() after it.
    uint16_t readSourceLines() const;

    // The line rate a RUN of the input formatter's line period implies, or 0
    // where the run does not stand up to the line count. No vsync spin, so it
    // is affordable on the idle path -- which is what lets a rate change at an
    // unchanged count be seen at all. The judgement is here because one reading
    // of a register that rails is not evidence.
    static uint32_t measureLineRateFromHPeriod(uint16_t lines);

    // --- what the last measure() found ---------------------------------------
    HsyncPulse hsync() const;
    uint16_t sourceLines() const;
    float fieldRateHz() const;

    // A refusal leaves the last believed rate standing rather than zeroing it.
    uint32_t lineRateHz() const;

    // The count the steadiness gate settled on, which is not the last sample.
    uint16_t steadyLines() const;

    // The vertical period the last reading came from, or 0 where it did not
    // complete. verticalTapFor() and the relock want the period itself.
    uint16_t verticalPeriod() const;

    // Whether the source runs the 15.7 kHz broadcast line. Not on its own
    // whether the vertical interval is serrated.
    bool lowLineRate() const;

    uint16_t ifLine(bool lineDoubled) const;
    uint16_t retimeStop() const;

    // --- the bounds the contract is stated in ---------------------------------

    // How many consecutive agreeing samples make the source worth measuring.
    static const uint8_t SteadySamples = 4;

    // How many readings may disagree before the rate is taken as it stands, and
    // how many may be refused before one is. A source whose period genuinely
    // wanders would otherwise hold the mode change open for ever, and the
    // capture stays frozen for as long as it is open.
    static const uint8_t RateAgreementAttempts = 8;
    static const uint8_t HeldRateRejectionLimit = 60;


    // The field rate the ADC's crossover row is picked against before one has
    // been measured.
    static const uint8_t NominalFieldRateHz = 60;

private:
    // --- the one pass, in the order it takes them ----------------------------

    bool sampleSteady();
    bool countWasSerrations() const;
    bool measureLineRate();
    bool rateSettled();
    HsyncPulse readSource() const;

    // --- what the pass reads and judges --------------------------------------

    static uint16_t measureSourceLinesCorrected(uint16_t divider);

    // Whether the sync processor counted the source's lines or the serration
    // and equalisation pulses either side of the vertical interval. Only an
    // interlaced source can have its count doubled, and the half-line witness
    // alone cannot separate the two: a correct 480p count sits exactly on the
    // total and reads identically to a doubled one.
    static bool countIsSerrations(uint16_t lines, uint16_t halfLines,
                                  bool interlaced);

    static ScanType scanTypeFor(uint16_t verticalPeriod, bool lineDoubled);
    static ScanType scanTypeFrom(uint16_t verticalPeriod, bool lineDoubled,
                                 bool countAlternated);
    bool countAlternated() const;

    // The line rate one HPERIOD_IF reading states, and the rate a RUN of them
    // implies -- 0 where the run cannot be believed. `htBadSeen` is
    // STATUS_IF_HT_BAD anywhere in the window, a ONE-SIDED gate: set means
    // refuse, clear does not mean good.
    static uint32_t lineRateForHPeriod(uint16_t hperiod);
    static uint32_t lineRateFromHPeriod(const uint16_t *samples, uint8_t count,
                                        uint16_t lines, bool htBadSeen);

    // Whether a new measurement is consistent with the one already held, and
    // whether what is held can judge it at all.
    static bool rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                 uint16_t heldLines, uint32_t heldLineRateHz);
    static bool heldRateJudges(uint16_t lines, uint16_t heldLines,
                               uint32_t heldLineRateHz);
    bool heldRateCorroborates(uint32_t lineRateHz) const;
    void takeCounterRate(uint32_t lineRateHz);

    // --- the sampling clock ---------------------------------------------------

    // The divider the capture write limit allows, which puts the line counter
    // on WriteLimitUnits whatever the scan mode -- one state, every source.
    static uint16_t referenceDivider(bool lineDoubled);

    // A rate good enough to pick the ADC's crossover row on a pass that has
    // measured none. From the line count, never from the held rate.
    uint32_t estimatedLineRateHz() const;

    // How many source lines the sync processor is counting as one, or 0 when
    // the count is not a multiple. A PLL asked for a frequency under its lock
    // range locks to every kth hsync instead.
    static uint8_t linesPerCount(uint16_t lineSamples, uint16_t divider);

    // --- bounds nothing outside this class states ----------------------------

    static const uint16_t HPeriodAgreement = 2;
    static const uint8_t HPeriodSamples = 8;
    static const uint32_t LineRateFloorHz = 15000;
    static const uint16_t RateAgreementPerMille = 1;

    // The 15.7 kHz broadcast line, split from the 31.5 kHz VGA one clear of
    // both and of the ~21.8 kHz a programmable source reaches between them.
    static const uint32_t LowLineRateBelowHz = 20000;
    static const uint8_t LinesPerCountMax = 4;

    uint32_t lineRateHz_;
    uint16_t sourceLines_;
    float fieldRateHz_;
    float agreedRateHz_;
    uint16_t goodLines_;
    uint32_t goodLineRateHz_;
    uint8_t rateRejections_;

    HsyncPulse hsync_;
    uint16_t verticalPeriod_;
    SteadyRun steady_;
    uint8_t rateAttempts_;
    bool serrationsSeen_;  // the last completed steadiness run read the serrations
    uint32_t referenceRateHz_;  // the estimate the reference sample rate was sized from
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
