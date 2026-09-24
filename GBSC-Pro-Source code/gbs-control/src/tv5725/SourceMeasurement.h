#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "HsyncPulse.h"
#include "SourceKey.h"
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
// The clock is not chosen here. The divider the source is left on depends on
// the output mode, which this class knows nothing about -- so it measures the
// line rate through whatever is in force, hands that back, and reads the duty
// once the caller has installed the clock it sized from it.
class InputFormatter;

class SourceMeasurement {
public:

    // How far the line rate may sit from the one held at the SAME line count
    // before the reading is refused. Measured on a settled source through the
    // current path, 250 samples a mode: 0.000% spread at both 311 x 50 and
    // 627 x 60. What it has to reject is 15.6% (57.9 Hz against a real 50.08
    // after a preset load), and the mid-change readings run to 98% and beyond.
    //
    // **THE GAP IS WIDE AND THE MIDDLE IS THE RIGHT PLACE TO SIT.** Tightening
    // costs genuine movement: a source may change rate at a constant count --
    // a RISC PC does -- and every such change inside this is refused until
    // HeldRateRejectionLimit lets it through.
    // ../../../docs/investigations/the-rate-tolerance-answered-five-questions.md
    static const uint16_t RateFollowsCountPerMille = 50;
    explicit SourceMeasurement(InputFormatter &inputFormatter);

    // A sampling clock has just been latched, so nothing counted in ADC samples
    // is the source's until the PLL has relocked and the sync processor has
    // counted a line at the new rate.
    void samplingClockLatched();

    // The source is about to move, so the steadiness run and the rate agreed
    // on mean nothing.
    void modeChanged();

    // Drops the rate HELD, which a mode change deliberately does not: that rate
    // is what refuses a settling transient, and a confirmed rate change is the
    // only thing that makes it the obstacle instead.
    void forgetHeldRate();

    enum MeasurementStatus {
        ClockSettling, // the sampling clock was latched too recently to read through
        NotSteady,     // the count is still gathering samples
        Serrations,    // the settled count read the serrations, not the source
        Unmeasurable,  // nothing could speak for a line rate
        Settling,      // a reading, which has not repeated or cannot be believed
        Measured,
    };

    // THE MEASUREMENT IS TWO HALVES WITH A HARDWARE WRITE BETWEEN THEM, which
    // is why it is two calls. Both are asked on every pass.
    //
    // The count, the field rate and HPERIOD_IF all survive a divider the ADC
    // PLL cannot lock to -- measured, the source's 311 lines and 50.08 Hz on
    // both test buses at four such dividers -- so the rate is measured through
    // whatever is in force. The duty is the one reading that cannot: it is a
    // ratio of the pulse to the LINE, counted in ADC samples, so a duty taken
    // through one divider and spent on a window sized in another's units is out
    // by the ratio between them. The divider the source is left on depends on
    // the output mode, which this class knows nothing about -- so the caller
    // installs it from the rate, between the two.

    // The count, the field rate and the line rate they give. Measured means the
    // rate has repeated and is worth sizing a clock from.
    MeasurementStatus measureRate();

    // The hsync duty, counted against the clock the caller has just installed.
    MeasurementStatus measureDuty();

    enum ScanType {
        ScanProgressive,
        ScanInterlaced,
    };

    // The scan type, from the half line an interlaced field puts into the line
    // count. Interlace offsets vertical sync by half a line every other field,
    // so the count alternates by one and nothing else makes it do that.
    //
    // **THERE IS NO UNKNOWN ANSWER.** A source that cannot be shown to be
    // interlaced is taken as progressive and the ladder says so, because the
    // costs are not symmetric: deinterlacing a progressive source corrupts the
    // picture, where leaving an interlaced one alone combs it and the user can
    // turn the deinterlacer on. An answer that declined to choose left the
    // previous source's steering in force instead.
    // ../../../../docs/investigations/interlaced-source-measurement.md
    ScanType measureScanType();

    // Measure the source's line count, corrected for a divider the ADC PLL
    // could not lock to. The ONE way the count is read, here and inside every
    // half of the measurement: a divider far from the source's line puts the
    // PLL outside its lock range and it locks to every kth hsync instead --
    // measured, a 311-line source reads 155 with the divider left at another
    // mode's 1124. The divider is sized from the rate and the rate from the
    // count, so a count taken at face value anywhere leaves that state with no
    // exit.
    //
    // Also taken by the caller before measuring, because the scan mode is
    // judged from it and the divider asked for follows the scan mode.
    uint16_t readSourceLines();

    // The input formatter's line period, settled, or 0 where the samples will
    // not agree. A REFERENCE TO COMPARE AGAINST, never a measurement: the
    // register rails to values that are wrong and steady.
    uint16_t settledLinePeriod();

    // Whether the source's line period has moved away from `reference`, for the
    // idle path. This is the whole contract -- it answers "did it change" and
    // cannot be asked what the rate is, so no caller can come to depend on a
    // number that is not trustworthy. What the rate IS is measured a different
    // way, and only where this says something moved.
    bool hasLineRateMoved(uint16_t reference);

    // --- what the last measure() found ---------------------------------------

    // A duty read while the sync processor was counting another line length is
    // refused, and leaves the last one that was not standing.
    HsyncPulse hsync() const;

    // The sync polarities, which join the count, the rate and the sync width in
    // identifying the source. Undetermined on an arrangement that states
    // neither, which is composite sync and sync on green.
    //
    // TAKEN ON THE PASS THAT MEASURED THE SOURCE, not derived when asked. The
    // sync type latches, so a later re-derivation could move the key with no
    // measurement behind it and lose a framing the user tuned.
    // docs/sync-type-selection.md
    SourceKey::Polarity hsyncPolarity() const;
    SourceKey::Polarity vsyncPolarity() const;

    uint16_t sourceLines() const;

    float fieldRateHz() const;

    // A refusal leaves the last believed rate standing rather than zeroing it.
    uint32_t lineRateHz() const;


    // The count the steadiness gate settled on, which is not the last sample.
    uint16_t steadyLines() const;

    // The vertical period the last reading came from, or 0 where it did not
    // complete. verticalTapFor() and the relock want the period itself.
    uint16_t verticalPeriod() const;

    // The sync processor's count with the vertical sync it loses restored, for
    // a caller reading the count for itself. **ONE OWNER OF WHAT THE COUNT
    // IS**: the presence poll and the measurement have to agree, and a reader
    // taking the register raw sees the source move on every pass against a
    // solve made from the restored one.
    uint16_t countNow() const;

    // Whether the source runs the 15.7 kHz broadcast line. Not on its own
    // whether the vertical interval is serrated.
    bool lowLineRate() const;


    // --- the bounds the contract is stated in ---------------------------------

    // How many consecutive agreeing samples make the source worth measuring.
    static const uint8_t SteadySamples = 4;

    // How many readings may disagree before the rate is taken as it stands, and
    // how many may be refused before one is. A source whose period genuinely
    // wanders would otherwise hold the mode change open for ever, and the
    // capture stays frozen for as long as it is open.
    static const uint8_t RateAgreementAttempts = 8;
    static const uint8_t HeldRateRejectionLimit = 60;

    // How many passes after the sampling clock is latched before anything read
    // through it means a thing. applySampleRate() writes the PLL group and
    // latches it, and the PLL has to relock and the sync processor count a line
    // at the new rate before either the count or the duty is the source's.
    //
    // Measured with SamplingLog across a source mode change:
    // STATUS_SYNC_PROC_HTOTAL took 82 ms to echo a newly latched divider,
    // which is four passes at the detection cadence.
    // STATUS_MISC_PLLAD_LOCK is not what this waits on -- on a settled source
    // it dithers, 136 transitions in 1109 samples with the count exact
    // throughout, so it can corroborate a reading and can never refuse one.
    // docs/investigations/the-duty-is-counted-before-the-processor-relocks.md
    static const uint8_t LatchSettlePasses = 5;

private:
    // --- the one pass, in the order it takes them ----------------------------

    bool sampleSteady();
    bool countWasSerrations() const;
    bool measureLineRate();
    float sampleFieldRateHz();
    static float medianOfThree(float a, float b, float c);
    bool rateSettled();
    void takeJudgedRate();
    bool normalisePolarity();
    bool readSource();
    bool takeDuty(bool latched, const HsyncPulse &reading);
    static SourceKey::Polarity polarityOf(bool positive);

    // --- what the pass reads and judges --------------------------------------

    static uint16_t measureSourceLinesCorrected(uint16_t divider);

    // Whether the sync processor counted the source's lines or the serration
    // and equalisation pulses either side of the vertical interval. Only an
    // interlaced source can have its count doubled, and the half-line witness
    // alone cannot separate the two: a correct 480p count sits exactly on the
    // total and reads identically to a doubled one.
    // The widest vertical sync a reconciliation may restore. The widest in the
    // DMT set this bench carries is 7 lines, and a bound is what refuses a torn
    // reading that happens to land a plausible distance away.
    static const uint16_t VerticalSyncMaxLines = 8;

    static bool countIsSerrations(uint16_t lines, uint16_t halfLines,
                                  bool interlaced);

    bool countAlternated() const;

    // The frame the source sends, reconciled from the input formatter's own
    // measurement of it against the sync processor's count. 0 where the two
    // cannot be reconciled, which is a torn read of either, or a source whose
    // counter holds a field where the formatter holds a frame.
    static uint16_t reconciledFrame(uint16_t verticalPeriod, uint16_t lines);

    // What the sync processor's count is short by, held once two readings
    // agree on it. HELD RATHER THAN RECOMPUTED: VPERIOD_IF tears, and a
    // correction taken per sample moves the count by the whole vertical sync
    // whenever a reading is refused, which reads as the source moving.
    void holdVerticalSync(uint16_t lines);


    // Whether a new measurement is consistent with the one already held, and
    // whether what is held can judge it at all.
    static bool rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                 uint16_t heldLines, uint32_t heldLineRateHz);
    static bool heldRateJudges(uint16_t lines, uint16_t heldLines,
                               uint32_t heldLineRateHz);
    static const uint16_t HPeriodAgreement = 2;
    static const uint8_t HPeriodSamples = 8;

    // --- the sampling clock ---------------------------------------------------

    // How many source lines the sync processor is counting as one, or 0 when
    // the count is not a multiple. A PLL asked for a frequency under its lock
    // range locks to every kth hsync instead.
    static uint8_t linesPerCount(uint16_t lineSamples, uint16_t divider);

    // --- bounds nothing outside this class states ----------------------------

    static const uint16_t RateAgreementPerMille = 1;


    // How far settledLinePeriod() must move to count as movement. A different
    // quantity from the one above: HPERIOD_IF is a change detector read in its
    // own units, compared only against its own earlier value.
    static const uint16_t LinePeriodMovedPerMille = 50;

    // The 15.7 kHz broadcast line, split from the 31.5 kHz VGA one clear of
    // both and of the ~21.8 kHz a programmable source reaches between them.
    static const uint32_t LowLineRateBelowHz = 20000;
    static const uint8_t LinesPerCountMax = 4;

    InputFormatter &inputFormatter_;
    uint32_t lineRateHz_;
    uint16_t sourceLines_;
    uint16_t verticalSyncLines_;          // held; what the count is short by
    uint16_t offeredVerticalSyncLines_;   // the reading it has to repeat
    float fieldRateHz_;
    float agreedRateHz_;
    // The settled pair a later reading is judged against, and the last rate
    // accepted, which is what lineRateHz() reports.
    uint16_t judgedLines_;
    uint32_t judgedRateHz_;
    uint32_t goodLineRateHz_;
    uint8_t rateRejections_;

    HsyncPulse hsync_;
    SourceKey::Polarity hsyncPolarity_;
    SourceKey::Polarity vsyncPolarity_;
    uint16_t verticalPeriod_;
    bool dutyMeasured_;
    uint8_t settlePasses_;
    SteadyRun steady_;

    // The scan decision's own run. steady_ is the solve's, and it stops being
    // fed the moment a source settles -- which is exactly when a source that
    // starts alternating has to be noticed, and a source going interlaced moves
    // the count by one, which SteadyRun::agree() calls the same measurement, so
    // nothing re-measures. Sampled on the maintenance cadence instead, by
    // measureScanType().
    SteadyRun scanSteady_;
    uint8_t rateAttempts_;
    bool serrationsSeen_;  // the last completed steadiness run read the serrations
    int8_t scanReported_;  // the last answer measureScanType() logged, so it logs changes
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
