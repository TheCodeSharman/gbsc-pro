#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "SourceReading.h"
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

    enum Reading {
        NotSteady,     // the count is still gathering samples
        Serrations,    // the settled count read the serrations, not the source
        Unmeasurable,  // nothing could speak for a line rate
        Settling,      // a rate, which has not repeated yet
        Measured,
    };

    enum ScanType {
        ScanUnknown,
        ScanProgressive,
        ScanInterlaced,
    };

    // Put the chip on the reference divider needed for a valid measurement to be taken.
    // Note this will corrupt the picture.
    void applyReferenceSampling(uint8_t oversample);

    // A mode change is about to move the count and the rate, so the run so far
    // and the rate agreed on mean nothing.
    void resetSteadiness();

    // Drops the held rate, so the next measurement has nothing to be rejected
    // against -- which is what a re-solve was armed for.
    void forgetHeldRate();

    // Measure the video source timings, holding them as state. Asked on every
    // pass. applyReferenceSampling() must be in force first.
    Reading measure();



    // --- what the last measure() found ---------------------------------------

    SourceReading hsync() const;
    uint16_t sourceLines() const;
    float fieldRateHz() const;

    // The last rate that passed the cross-check against the line count, which
    // a refusal leaves standing -- so a reader that has to survive a sync loss
    // gets the last one believed rather than a zero.
    uint32_t lineRateHz() const;

    // The count the steadiness gate settled on, which is not the last sample.
    uint16_t steadyLines() const;

    // The scan type, from the half line an interlaced field carries.
    // `verticalPeriod` is VPERIOD_IF.
    ScanType scanType(uint16_t verticalPeriod) const;

    // Whether the source runs the 15.7 kHz broadcast line. Not on its own
    // whether the vertical interval is serrated.
    bool lowLineRate() const;

    // The count, corrected for a divider the ADC PLL could not lock to. Read
    // BEFORE the reference sampling clock: the scan mode is judged from it and
    // the clock follows the scan mode, so it is the one reading that cannot
    // come from a measure() pass.
    uint16_t readSourceLines() const;



    // Take a divider that was chosen rather than solved.
    void holdDivider(uint16_t divider);

    // Whether the line doubler is in the capture path, which the scan mode
    // decides. Held rather than read back: InputFormatter::applyScanMode() owns
    // the registers and this owns the arithmetic that has to match them.
    void holdLineDoubling(bool lineDoubled);

    bool usable() const;
    uint16_t divider() const;
    bool lineDoubled() const;
    uint16_t ifLine() const;
    uint16_t retimeStop() const;

    // Whether the ADC PLL is running at the ratio the divider asked for.
    bool dividerLatched() const;
    static bool dividerLatched(uint16_t lineSamples, uint16_t divider,
                               uint16_t tolerance = LatchedSamplesTolerance);

    // --- single register reads, for a caller with no measurement to hold ------
    //
    // The only reads of STATUS_SYNC_PROC_* anywhere: nothing else on the board
    // can supply them, and every other quantity the engine needs it computed.

    static uint16_t measureSourceLines();

    // The line in ADC samples, which is what STATUS_SYNC_PROC_HTOTAL counts.
    // Read to decide whether a count can be believed, never to derive a
    // register from.
    static uint16_t measureLineSamples();

    // The line rate from HPERIOD_IF alone, or 0 where the run does not stand up
    // to the line count. No vsync spin, so it is affordable on the idle path --
    // which is what lets a rate change at an unchanged count be seen at all.
    static uint32_t measureLineRateFromHPeriod(uint16_t lines);

    // --- the bounds the contract is stated in ---------------------------------

    // How many consecutive agreeing samples make the source worth measuring.
    static const uint8_t SteadySamples = 4;

    // How many readings may disagree before the rate is taken as it stands, and
    // how many may be refused before one is. A source whose period genuinely
    // wanders would otherwise hold the mode change open for ever, and the
    // capture stays frozen for as long as it is open.
    static const uint8_t RateAgreementAttempts = 8;
    static const uint8_t HeldRateRejectionLimit = 60;

    // How far the count may sit from the divider and still be the same
    // quantity. The register wobbles by a sample either way when locked.
    static const uint16_t LatchedSamplesTolerance = 2;

    // The field rate the ADC's crossover row is picked against before one has
    // been measured.
    static const uint8_t NominalFieldRateHz = 60;

    // The 15.7 kHz broadcast line, split from the 31.5 kHz VGA one clear of
    // both and of the ~21.8 kHz a programmable source reaches between them.
    static const uint32_t LowLineRateBelowHz = 20000;

private:
    // --- the one pass, in the order it takes them ----------------------------

    bool sampleSteady();
    bool countWasSerrations() const;
    bool measureLineRate();
    bool rateSettled();
    SourceReading readSource() const;

    // --- what the pass reads and judges --------------------------------------

    static uint16_t measureSourceHalfLines();
    static uint16_t measureHsyncLow();
    static bool measureHsyncPositive();
    static uint16_t measureSourceLinesCorrected(uint16_t divider);

    // Whether the sync processor counted the source's lines or the serration
    // and equalisation pulses either side of the vertical interval. `interlaced`
    // is measured rather than inferred: the half-line witness alone cannot
    // separate the two, because a correct 480p count sits exactly on the total.
    static bool countIsSerrations(uint16_t lines, uint16_t halfLines,
                                  bool interlaced);

    static ScanType scanTypeFor(uint16_t verticalPeriod, bool lineDoubled);
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
    static const uint8_t LinesPerCountMax = 4;

    uint16_t divider_;
    uint32_t lineRateHz_;
    uint16_t sourceLines_;
    float fieldRateHz_;
    float agreedRateHz_;
    uint16_t goodLines_;
    uint32_t goodLineRateHz_;
    uint8_t rateRejections_;
    bool lineDoubled_;

    SourceReading hsync_;
    SteadyRun steady_;
    uint8_t rateAttempts_;
    bool serrationsSeen_;  // the last completed steadiness run read the serrations
    uint32_t referenceRateHz_;  // the estimate the reference sample rate was sized from
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
