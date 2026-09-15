#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

// Measures the timing from the video source.

#include <Arduino.h>   // `boolean`
#include <stdint.h>

#include "SourceReading.h"
#include "SteadyRun.h"
#include "TestBusRateMeasurement.h"
#include "Tv5725Log.h"

namespace Tv5725 {

// Measures the timing from the video source: the line count, the line rate,
// the hsync pulse and the scan type.
//
// Some of the registers it reads are unreliable: HPERIOD_IF rails to a value
// that is wrong and steady, so no caller can judge a reading by looking at it.
// Returning one that can be trusted is this class's job, by the cheapest route
// that works -- a register read where a run of them stands up, a vsync spin
// where it does not.
//
// Measuring needs a sampling clock of its own: every measurement is counted
// through the ADC clock, so applyReferenceSampling() puts the chip on a divider
// this class chose before the source is measured -- a reading taken through the
// previous mode's is not the source's. solve() then chooses the divider the
// source is captured at and hands it out, for VideoPath to install.
class SourceMeasurement {
public:

    // The source's line rate in Hz, or 0 when neither the count nor the rate is
    // something video runs at. Both bounds are gross-error nets: 50 and 60 are
    // not special and a rate is whatever the source sends. What rejects a
    // reading taken mid-settle is rateFollowsCount(). Returning 0 rather than a
    // guess lets solve() decline.
    // The field rate times the frame, where the frame is sourceLines + 1:
    // STATUS_SYNC_PROC_VTOTAL is zero based. Reading it as the frame makes this
    // 1/312 low on the bench source, which is the whole of the accuracy
    // HPERIOD_IF has over it.
    static uint32_t lineRateFrom(uint16_t sourceLines, float fieldRateHz);

    // Nothing chosen yet. A caller must be able to SEE that rather than get a
    // zero it would go on to write.
    SourceMeasurement();

    enum Reading {
        NotSteady,     // the count is still gathering samples
        Serrations,    // the settled count read the serrations, not the source
        Unmeasurable,  // nothing could speak for a line rate
        Settling,      // a rate, which has not repeated yet
        Measured,
    };

    // Every quantity a solve needs, in ONE pass: the line count, the line rate
    // and the hsync pulse, held as state for the caller to read back. A capture
    // taken on one solve against a window taken on another describes two states
    // and the difference between them reads as a fault in the arithmetic.
    //
    // Asked on every pass. The cheap gate is inside it, so the field rate's
    // vsync spin -- up to 250 ms a pulse -- is not paid for until the count has
    // settled.
    //
    // **THE REFERENCE SAMPLING CLOCK MUST ALREADY BE IN FORCE.** Every reading
    // here is counted through the ADC clock, and one taken through the previous
    // mode's divider is not this source's. The count that CHOOSES that clock is
    // the one reading that cannot come from this pass -- readSourceLines().
    Reading measure();

    // The hsync pulse from the last measure(). The engine solves every window
    // from this and reads nothing back.
    SourceReading hsync() const;

    // Read the line rate off the chip and hold both inputs, because the
    // cross-check inside can say the two disagree but never which of them was
    // wrong. False when it is unmeasurable. HPERIOD_IF answers when a run of it
    // is believable, the field rate spin when it is not.
    // How many consecutive agreeing samples make the source worth measuring.
    static const uint8_t SteadySamples = 4;

    // Whether a line count is something video runs at. Both bounds are
    // gross-error nets rather than a classification.
    static bool countIsSource(uint16_t lines);

    // Whether the sync processor is counting the source's lines or the
    // serration and equalisation pulses either side of its vertical interval.
    // It counts through the coast, so a coast that does not cover them counts
    // them as lines and reports about twice the source. `halfLines` is
    // VPERIOD_IF, which measures the same frame by a route the coast disturbs
    // by a few counts but cannot double.
    //
    // False when the witness is not measuring, which is the separate-sync case
    // and is not a judgement that the count is good.
    //
    // **THE WITNESS ALONE CANNOT SEPARATE THE TWO.** It reports half-lines on
    // the sources this was built from, but not on all of them: 480p on YPbPr
    // measures VPERIOD_IF 524 against SP_VTOTAL 524, so a correct count sits
    // exactly ON the total and reads identically to a doubled one. Judged on
    // the witness alone that source is rejected on every pass, no solve ever
    // runs, and SyncOutput holds the sync pads blanked for ever.
    //
    // `interlaced` is what separates them, and it is measured rather than
    // inferred -- ModeDetect::sourceIsInterlaced(). A progressive source has no
    // field and frame to differ, so nothing can double its count and this can
    // only answer false.
    // docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md
    static bool countIsSerrations(uint16_t lines, uint16_t halfLines,
                                  bool interlaced);

    enum ScanType {
        ScanUnknown,
        ScanProgressive,
        ScanInterlaced,
    };

    // The source's scan type, from the half line an interlaced field carries.
    // VPERIOD_IF is the only count with the resolution to hold one, and only
    // where the input formatter doubles the line -- which is what puts the
    // count in half lines and INVERTS the parity that means interlaced. A Wii
    // reads 524 at 480i and at 480p, so neither parity nor a table of
    // broadcast totals answers without the doubling.
    //
    // Ask STATUS_IF_VT_OK whether there is a measurement at all before this:
    // the separate-sync path leaves debris in VPERIOD_IF rather than a period.
    // docs/investigations/interlaced-source-measurement.md
    static ScanType scanTypeFor(uint16_t verticalPeriod, bool lineDoubled);

    // The held source's scan type. The period answers wherever it is a
    // measurement; where it is not -- separate sync, which leaves debris in
    // VPERIOD_IF -- the steadiness run's alternation is all there is, and a
    // count that does not alternate claims nothing, because a Wii at PAL 576i
    // holds a steady 310 while genuinely interlaced.
    ScanType scanType(uint16_t verticalPeriod) const;

    // Whether the settled count alternates by one, which only an interlaced
    // field can make it do.
    bool countAlternated() const;

    // The source's line count, corrected for a divider the ADC PLL cannot lock
    // to. The sync processor counts in ADC clocks, so on too small a divider the
    // PLL locks to every Nth hsync: it reports a count N times too low and N
    // times the samples per line. STATUS_SYNC_PROC_HTOTAL against the divider
    // gives N.
    //
    // **It needs no field rate**, which is the point -- the scan mode, and the
    // reference divider that follows it, are needed BEFORE anything can be
    // measured, and an uncorrected count leaves both stuck on the value that
    // corrupted them.
    static uint16_t measureSourceLinesCorrected(uint16_t divider);

    // The count the steadiness gate has settled on. Meaningful only when
    // sampleSteady() has returned true; before that it is whatever arrived last.
    uint16_t steadyLines() const;

    // A mode change is about to move the count and the rate, so the run so far
    // and the rate agreed on mean nothing.
    void resetSteadiness();

    // A gross sanity net on the measured field rate, not a classification: the
    // rate is whatever the source runs at, and 50 and 60 are not special.
    // Outside this nothing being fed to the part is video.
    static constexpr float FieldRateMinHz = 15.0f;
    static constexpr float FieldRateMaxHz = 150.0f;

    // How far a line rate may move while the source line count does not, in
    // parts per thousand. A gross-error net: the bench transient is 15.6% out
    // (57.9 Hz against a real 50.08) and a settled source drifts by tenths of
    // one, so anything between separates them. rateSettled() is what holds the
    // fine tolerance.
    static const uint16_t HeldRateTolerancePerMille = 50;

    // How many consecutive readings may be refused before one is taken as it
    // stands. A source that genuinely changes rate without changing its line
    // count would otherwise hold the mode change open for ever, and the capture
    // stays frozen for as long as it is open. Each refusal costs a measurement,
    // so this is seconds rather than milliseconds.
    static const uint8_t HeldRateRejectionLimit = 60;

    // Whether a new measurement is consistent with the one already held. False
    // means the rate moved without the count, which is a reading taken while
    // the source was still settling rather than a new mode.
    static bool rateFollowsCount(uint16_t lines, uint32_t lineRateHz,
                                 uint16_t heldLines, uint32_t heldLineRateHz);

    // Whether the held rate is in a position to judge a reading at all. Nothing
    // held and a count that moved both leave rateFollowsCount() accepting
    // whatever it is given, which is where a railed HPERIOD_IF gets in.
    static bool heldRateJudges(uint16_t lines, uint16_t heldLines,
                               uint32_t heldLineRateHz);

    // Whether two line rates are the same measurement. HeldRateTolerancePerMille
    // apart, which is what separates a source that moved from one being read
    // through a settling PLL.
    static bool ratesAgree(uint32_t a, uint32_t b);

    // The line rate one HPERIOD_IF reading states: 27 MHz / ((hperiod + 1) * 4),
    // measured across ten modes to a mean 29 ns. Counted against the chip's own
    // 27 MHz, so it does not move with PLLAD_MD.
    static uint32_t lineRateForHPeriod(uint16_t hperiod);

    // How far two HPERIOD_IF readings may differ and still be the same reading.
    // A settled source moves by one count; a railed one by hundreds.
    static const uint16_t HPeriodAgreement = 2;

    // No television generates a line slower than this, so a reading implying
    // one is the counter rather than the source. It is what rejects the railed
    // form: HPERIOD_IF 511 implies 13.2 kHz, 510 implies 13.2, and the 431 this
    // bench is due is 15625 with headroom to spare.
    static const uint32_t LineRateFloorHz = 15000;

    // The line rate a RUN of HPERIOD_IF readings implies, or 0 when the run
    // cannot be believed. One register read against the field rate's
    // vsync spin, but it rails with nothing to say so -- STATUS_IF_HT_OK reads
    // 1 either way -- so the run and the line count are the judgement: readings
    // that disagree are railing, and a steady one implying a field rate no
    // source runs at is the railing's stable form.
    // docs/investigations/hperiod-if-railing.md
    // htBadSeen is STATUS_IF_HT_BAD anywhere in the window. It is a ONE-SIDED
    // gate: it never sets on a healthy reading, so set means refuse -- but
    // clear does not mean good, and 5 of 20 samples on a live instance had it
    // clear. STATUS_IF_HT_OK is not a gate in either direction: through 5302
    // reads of a railed state it read 1 for 146 of them, none within 2% of the
    // value the mode was due.
    static uint32_t lineRateFromHPeriod(const uint16_t *samples, uint8_t count,
                                        uint16_t lines, bool htBadSeen);

    // How many HPERIOD_IF readings make a run. Register reads, so the cost is
    // nothing beside the vsync spin this is there to avoid.
    //
    // **WIDE ENOUGH THAT GARBAGE CANNOT LOOK LIKE AGREEMENT.** The railed form
    // is noisy rather than stuck -- 511 in 6 of 20 samples, the rest scattered
    // -- so a short window lands three consecutive 511s often enough to matter,
    // and the run test then reads the fault as a settled source.
    static const uint8_t HPeriodSamples = 8;

    // How far two field-rate readings may differ and still be the same rate, in
    // parts per thousand. One reading of one field period at the ESP's clock,
    // so settled readings differ in the last place; 0.1% is ten times that and
    // a third of the smallest settling error measured.
    static const uint16_t RateAgreementPerMille = 1;

    // How many readings may disagree before the rate is taken as it stands. A
    // source whose period genuinely wanders would otherwise hold the mode
    // change open for ever, and the capture stays frozen for as long as it is
    // open -- a raster a fraction of a percent wrong is the better failure.
    static const uint8_t RateAgreementAttempts = 8;

    // The source's line count, against the divider held. Read BEFORE the
    // reference sampling clock, because the scan mode is judged from it and the
    // clock follows the scan mode.
    uint16_t readSourceLines() const;

    // The source, as the sync processor counts it. These are the only reads of
    // STATUS_SYNC_PROC_* anywhere: nothing else on the board can supply them,
    // and every other quantity the engine needs it computed itself.
    static uint16_t measureSourceLines();

    // The frame in half-lines, from the input formatter rather than the sync
    // processor, so the coast cannot double it. Zero when STATUS_IF_VT_OK says
    // the measurement did not complete, which is the separate-sync case.
    static uint16_t measureSourceHalfLines();

    // How long the count must hold before a preset load, and how far two
    // readings may differ and still count as holding. Much longer than the
    // idle pass's SteadySamples: a load is expensive and a source mid-change
    // gives a count that is wrong AND steady for a few samples.
    static const uint8_t HoldSamples = 30;
    static const uint16_t HoldAgreement = 3;
    static const uint8_t HoldIntervalMs = 10;

    // The count as it reads after that run, or 0 if it moved. Zero rather than
    // a flag because no count is a count no source runs at, so a caller cannot
    // use the answer by accident.
    static uint16_t countHeldStill(uint16_t lines);
    static uint16_t measureHsyncLow();

    // Whether the hsync pulse is positive-going, which decides where in the
    // line the sync interval sits. STATUS_SYNC_PROC_HSPOL, and the one thing
    // that tells the capture window which end of the pulse it is counting from.
    static bool measureHsyncPositive();

    // The line rate from HPERIOD_IF alone, or 0 where the run does not stand up
    // to the line count. One register read, no vsync spin, so it is affordable
    // on the idle path -- which is what lets a rate change at an unchanged
    // count be seen at all.
    static uint32_t measureLineRateFromHPeriod(uint16_t lines);

    // The line in ADC samples, which is what STATUS_SYNC_PROC_HTOTAL counts.
    // Read to decide whether the other two can be believed at all, never to
    // derive a register from.
    static uint16_t measureLineSamples();

    // How far the count may sit from the divider and still be the same
    // quantity. The register wobbles by a sample either way when locked.
    static const uint16_t LatchedSamplesTolerance = 2;

    // Whether the ADC PLL is running at the ratio the divider asked for. A
    // divider of zero was never latched whatever the count reads.
    static bool dividerLatched(uint16_t lineSamples, uint16_t divider,
                               uint16_t tolerance = LatchedSamplesTolerance);
    bool dividerLatched() const;

    // How many source lines the sync processor is counting as one. A PLL asked
    // for a frequency under its lock range locks to every kth hsync instead,
    // which halves the line count and multiplies the sample count by k. Beyond
    // this any offset can be made to fit some multiple, so the answer stops
    // being evidence.
    static const uint8_t LinesPerCountMax = 4;

    // The multiple, or 0 when the count is not one -- which is the answer for a
    // latched divider and for a reading unrelated to it alike. One counted line
    // carries the jitter of every source line inside it, so the window scales
    // with the multiple rather than being the latch check's fixed two samples.
    static uint8_t linesPerCount(uint16_t lineSamples, uint16_t divider);

    bool usable() const;
    uint16_t divider() const;
    uint32_t lineRateHz() const;
    uint16_t sourceLines() const;
    float fieldRateHz() const;
    uint16_t ifLine() const;

    // The field rate the ADC's crossover row is picked against before one has
    // been measured. The top of the band TestBusRateMeasurement accepts a field rate in.
    static const uint8_t NominalFieldRateHz = 60;

    // The sampling state every measurement is taken from. The field rate is
    // timed off the input formatter's test bus and the IF line counter is the
    // divider's, so measuring through the previous mode's divider corrupts the
    // reading that would correct it. This is the divider the capture write
    // limit allows, which puts the line counter on WriteLimitUnits whatever the
    // scan mode -- one state, every source.
    static uint16_t referenceDivider(bool lineDoubled);

    // Take a divider that was chosen rather than solved.
    void holdDivider(uint16_t divider);

    // Take the reference divider for the scan mode and put the chip on it, so
    // what is measured next is counted through a divider this class chose
    // rather than the previous mode's.
    //
    // Idempotent on BOTH the divider and the estimate it was sized from, not on
    // the divider alone: PLLAD_KS is an octave of CKO and CKO is the divider
    // times the rate, so a count caught mid-transition picks the wrong octave
    // while the reference for a scan mode stays put. A return keyed on the
    // divider alone leaves KS wrong with PLLAD_MD right, which is a state
    // nothing can measure its way out of.
    void applyReferenceSampling(uint8_t oversample);

    // A rate good enough to pick the ADC's crossover row on a pass that has
    // measured none. From the line count, which the divider does not touch --
    // never from the held rate, which is the previous mode's.
    uint32_t estimatedLineRateHz() const;

    // The 15.7 kHz broadcast line, split from the 31.5 kHz VGA one clear of
    // both and of the ~21.8 kHz a programmable source reaches between them.
    static const uint32_t LowLineRateBelowHz = 20000;

    // The last rate that passed the cross-check against the line count.
    // lineRateHz() is the last one MEASURED and a refusal clears it, so a
    // reader that has to survive a sync loss asks this one.
    uint32_t heldLineRateHz() const;

    // Drops it, so measureLineRate() has nothing to reject the next rate
    // against. rateFollowsCount() refuses a rate that moved at an unchanged
    // count, which is exactly the source a re-solve was armed for.
    void forgetHeldRate();

    // Whether the source runs that line. Not on its own whether the vertical
    // interval is serrated -- a separate-sync source of the same rate is not.
    // docs/investigations/serrated-sync-is-not-line-rate.md
    bool lowLineRate() const;

    // Whether the line doubler is in the capture path, which the scan mode
    // decides. It runs the IF's line counter at twice the source line rate, so
    // it is the one fact behind both the horizontal decimation and the vertical
    // half-lines. Held rather than read back: InputFormatter::applyScanMode()
    // owns the registers and this owns the arithmetic that has to match them.
    void holdLineDoubling(bool lineDoubled);
    bool lineDoubled() const;
    uint16_t retimeStop() const;

private:
    // Reached only through measure(), which is the one pass every reading comes
    // from.
    // Take ONE line-count sample and say whether enough consecutive ones have
    // agreed. A single register read, so a caller can ask on every pass;
    // measureLineRate() spins for up to 250 ms a vsync pulse and cannot be
    // asked speculatively.
    //
    // An OPTIMISATION rather than the correctness guard. What rejects a
    // settling source is lineRateFrom()'s cross-check of the rate against the
    // count, so letting one through early costs a measurement, not a wrong
    // answer. A count outside what any source runs never settles, because the
    // 97 a preset load leaves behind is perfectly steady.
    bool sampleSteady();

    // Whether the last steadiness run ended on a count that reads as the
    // serrations rather than the source. The return of sampleSteady() cannot
    // say this: a run still gathering samples and a run that gathered them and
    // rejected the result both read false, and only the second is a fault to
    // act on.
    bool countWasSerrations() const;

    bool measureLineRate();

    // Whether the rate measureLineRate() just took is worth sizing a raster
    // from -- either because it repeated, or because it has been asked
    // RateAgreementAttempts times. **THE RASTER MUST NOT BE SOLVED FIRST.**
    //
    // lineRateFrom()'s 2% cross-check is a gross-error net and passes a rate
    // read across a preset load, which is out by tenths of a percent -- and
    // horizontalTotal = clock / rate / lines, so the raster is out by the same
    // fraction and nothing re-solves it. docs/firmware-geometry-engine.md
    bool rateSettled();

    // The hsync pulse, against the divider held. The engine is handed this and
    // calculates from it, reading nothing back.
    SourceReading readSource() const;

    // Whether the rate already held stands behind a new reading. Free, where
    // asking the field rate costs a vsync spin.
    bool heldRateCorroborates(uint32_t lineRateHz) const;

    void takeCounterRate(uint32_t lineRateHz);

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
