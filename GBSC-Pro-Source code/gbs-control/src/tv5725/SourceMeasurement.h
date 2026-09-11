#ifndef TV5725_SOURCE_MEASUREMENT_H_
#define TV5725_SOURCE_MEASUREMENT_H_

// How finely the incoming line is sampled, and what the input formatter's line
// counter must be set to as a result.
//
// **PLLAD_MD, IF_HSYNC_RST and SP_RT_HS_SP are ONE quantity in THREE
// registers.** All three come off one held value; moving one without the others
// is what a fault here looks like.
//
// **THE REGISTER IS NOT THE SOURCE OF TRUTH.** PLLAD_MD is loaded into the ADC
// PLL by a rising edge on PLLAD_LAT, so between the write and the latch read()
// returns the NEW value while the ADC still clocks at the OLD one -- a solid
// green screen behind self-consistent registers, with nothing to diagnose from.
// STATUS_SYNC_PROC_HTOTAL is the one witness, counting real ADC clocks per line.
//
// So the divider is HELD and handed out, never read back. Beating against the
// source's pixel clock is not decidable here -- the chip sees sync edges, not
// pixels. This computes a ceiling and a starting point; the last word is the
// user's, from the screen.

#include <Arduino.h>   // `boolean`
#include <stdint.h>

// Declared here and defined outside this layer, which can reach neither: the
// field rate is counted off the debug pin through FrameSync at the ESP's clock,
// and the log goes to the web console.
//
// getSourceFieldRate() spins for vsync edges with no yield() -- framesync.h
// says why -- so it costs up to FS_SAMPLE_TIMEOUT_MS a pulse. measureLineRate()
// below is its one call site here, and everything downstream takes the rate
// from what that held rather than measuring again.
float getSourceFieldRate(boolean useSPBus);

#include "Tv5725Log.h"

namespace Tv5725 {

class SourceMeasurement {
public:
    // DS-5725-3.2, front page: "Maximum analog sampling rate up to 162MSPS".
    static const uint32_t MaxSampleRateHz = 162000000u;

    // PLLAD_MD is twelve bits.
    static const uint16_t DividerMax = 4095;

    // How far under the rating to sit, in percent. The margin is for
    // measurement jitter, not for the part: the line rate comes from a measured
    // field rate, so a low reading puts a ceiling divider above the rating. If
    // an unlock is reproduced at 98%, this is the one constant to move.
    static const uint16_t RecommendedPercent = 98;

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

    // How far along the line the sync processor stops retiming hsync, in
    // percent. Upstream's, carried verbatim -- see retimeStopFor().
    static const uint16_t RetimeStopPercent = 93;

    // **A TEARING CEILING MUST NOT BE REINSTATED.** The band it would keep the
    // divider below does not exist: HSCALE was swept across the corrupted state
    // and no value cleared it.
    // docs/investigations/hscale-tearing-characterisation.md has the measurement.
    //
    // The ceiling recommendedDivider() does apply is the capture write limit,
    // which bounds the LINE rather than the tearing. docs/capture-limits.md

    // ADC samples to IF units. The input formatter's horizontal decimation is
    // what relates them, and only the line-doubled scan mode applies it:
    // PLLAD_MD 2553 against IF_HSYNC_RST 1276 line-doubled, 2553 against 2553 not.
    // A counter wrapping at half the samples arriving repeats the picture.
    static uint16_t ifLineFor(uint16_t divider, bool lineDoubled);

    // The same quantity in a THIRD register: SP_RT_HS_SP counts in ADC samples,
    // so a divider that moves without it leaves the sync processor retiming a
    // line that is not arriving. The 93% is upstream's, unexplained and
    // unmeasured here; what matters is that it follows the divider.
    static uint16_t retimeStopFor(uint16_t divider);

    // What the ADC is actually asked to do, in samples per second.
    static uint32_t sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                 uint8_t oversample);

    static bool withinLimit(uint16_t divider, uint32_t lineRateHz,
                            uint8_t oversample);

    // The largest divider this line rate can carry, or 0 if none can -- which
    // is a case the caller must handle rather than a value it can use. A line
    // rate of 0 (no lock) is also 0.
    static uint16_t maxDivider(uint32_t lineRateHz, uint8_t oversample);

    // The longest IF line the input formatter's geometry registers can hold.
    // IF_HSYNC_RST, IF_HB_ST2 and IF_HB_SP2 are all [10:0], and a line past
    // this wraps rather than failing: PLLAD_MD 2094 was accepted, latched and
    // read back correctly at STATUS_SYNC_PROC_HTOTAL while IF_HSYNC_RST held
    // 46, with the picture destroyed and nothing reporting a fault.
    // docs/investigations/tail-green.md
    static const uint16_t IfLineUnitsMax = 2047;

    // The divider to write at a MODE CHANGE: under the ADC rating by
    // RecommendedPercent, and even, so ifLineFor() divides exactly rather than
    // truncating half a sample away. A zoom must never move it -- that would
    // resample the picture the user is watching.
    //
    // `maxIfLineUnits` is the longest line one window may span end to end --
    // VideoSourceLine::framableIfLine(), from the measured sync duty and
    // polarity. Zero falls back to WriteLimitUnits, for a caller with no
    // measurement to compute it from.
    // docs/capture-limits.md
    //
    // Returns 0 for an unmeasurable line rate rather than a default. A divider
    // written from a measurement that did not happen takes the sync processor
    // with it, leaving no picture to diagnose from.
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                       bool lineDoubled, uint16_t maxIfLineUnits = 0);

    // Nothing chosen yet. A caller must be able to SEE that rather than get a
    // zero it would go on to write.
    SourceMeasurement();

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
    ScanType scanType(uint16_t verticalPeriod) const;

    // Whether the last steadiness run ended on a count that reads as the
    // serrations rather than the source. The return of sampleSteady() cannot
    // say this: a run still gathering samples and a run that gathered them and
    // rejected the result both read false, and only the second is a fault to
    // act on.
    bool countWasSerrations() const;

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

    // The count the steadiness gate has settled on. Meaningful only when
    // sampleSteady() has returned true; before that it is whatever arrived last.
    uint16_t steadyLines() const;

    // A mode change is about to move the count and the rate, so the run so far
    // and the rate agreed on mean nothing.
    void resetSteadiness();

    bool measureLineRate();

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
    // cannot be believed. One register read against getSourceFieldRate()'s
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

    // Whether the rate measureLineRate() just took is worth sizing a raster
    // from -- either because it repeated, or because it has been asked
    // RateAgreementAttempts times. **THE RASTER MUST NOT BE SOLVED FIRST.**
    //
    // lineRateFrom()'s 2% cross-check is a gross-error net and passes a rate
    // read across a preset load, which is out by tenths of a percent -- and
    // horizontalTotal = clock / rate / lines, so the raster is out by the same
    // fraction and nothing re-solves it. docs/firmware-geometry-engine.md
    bool rateSettled();

    // Choose one for this line rate. False and NO state change when the rate is
    // unmeasurable, so the previous choice survives a dropped measurement --
    // the safe direction to be wrong in, since the sync watcher re-solves once
    // the source settles.
    bool solve(uint32_t lineRateHz, uint8_t oversample, uint16_t maxIfLineUnits = 0);

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

    // Whether the last HPERIOD window was refused because the input formatter
    // flagged the counter, rather than because the samples disagreed. A
    // settling source disagrees and wants waiting out; a flagged counter wants
    // the recovery below, and bouncing the first would manufacture the second.
    static bool counterWasFlagged();

    // The recovery for a flagged counter: takes the ADC's input away and gives
    // it back so the input formatter re-acquires the line. Tried once per source
    // event, and only where STATUS_IF_HT_BAD flagged the counter.
    //
    // **NOTHING INSTALLS ONE, AND THE REASON IS THE PICTURE.** Adc::bounceInput()
    // is the only thing measured to clear a railed counter from this end, and
    // taking the input away turns the whole screen green for as long as it is
    // gone -- photographed at 400 ms, on the modes that rail, which is a visible
    // flash rather than a repair. It buys accuracy and nothing else: a refused
    // window already falls back to the field rate and the raster comes out
    // right. Install one only with something better than a blind bounce.
    static void useCounterRecovery(void (*recover)());

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
    // been measured. The top of the band getSourceFieldRate() accepts.
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

    // Put the chip on the divider held, in all three of the registers that
    // carry it. The divider goes first because Adc latches it, and the latch
    // loads KS, CKOS and ICP with it -- so anything setting those must already
    // have run. A measurement that solved nothing writes nothing.
    void applySampling(uint8_t oversample);

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

    // The lowest line the bench display accepts, bracketed by measurement
    // rather than taken from the VGA standard: 26650 Hz locks and 21780 Hz
    // gives no signal, so the floor sits between them and admits every rate
    // proven to work while refusing every rate proven not to. 22..26 kHz is
    // untested and refused, which costs a scaled picture rather than a blank
    // panel. NOT LowLineRateBelowHz -- that sits at 20 kHz to put the ~21 kHz a
    // programmable source reaches on the fast side of the 15 kHz split, and the
    // display takes neither. docs/rgbhv-bypass-trap.md
    static const uint32_t BypassMinLineRateHz = 26000;

    // Whether bypass would reach the panel at all. Bypass hands the source's
    // own timing to the encoder, so an unmeasured or slow source has to stay on
    // the scaling path -- which shows any rate -- rather than put torn content
    // on the panel that reads as a broken scaler.
    //
    // This asks the HELD rate, so it answers only where a measurement has just
    // been taken. Bypass is not such a place -- countCanBypass() is.
    bool rateCanBypass() const;

    // The same question asked of a source counted NOW, for a source that is
    // already bypassed.
    //
    // **THE HELD RATE CANNOT ANSWER THERE.** Bypass measures nothing, so what
    // is held still names the mode bypass was entered on: a source that slows
    // underneath keeps reading as displayable, the branch that would leave
    // never fires, and the panel stays blank for ever. The count is live, and
    // the divider does not touch it.
    //
    // The count is taken against the HELD FIELD RATE rather than the held line
    // rate, because a mode change moves the count and usually leaves the field
    // rate where it was. A source that changes both at once is the one case
    // this cannot see, and it costs no vsync spin to be right about the rest.
    // A count of 0 is nothing measured and decides nothing.
    // docs/rgbhv-bypass-trap.md
    bool countCanBypass(uint16_t lines) const;

    // Below this many total source lines the capture is line-doubled, so the
    // rest of the chain has enough lines to reach the output resolution.
    // Measured rather than derived: 363 lines are doubled and 448 are not, and
    // no hardware limit produces the boundary between them.
    // docs/investigations/hperiod-if-railing.md has the sweep.
    static const uint16_t LineDoubleBelowLines = 400;

    // Whether a source of this many total lines is captured line-doubled.
    //
    // **Line doubling is not deinterlacing.** This asks whether enough lines
    // arrive; whether they arrive as fields is a separate fact with its own
    // register. An interlaced 625-line frame has lines to spare and wants
    // deinterlacing, not doubling.
    //
    // An unmeasured count comes back true: that is what a low-line-count source
    // needs, and it is the one a wrong guess leaves short.
    // Whether to put the line doubler in the path. A short source needs the
    // lines; an output with no room for the doubled frame cannot show them,
    // because the part cannot minify -- so `showableUnits`, what the output can
    // display, refuses a doubling that would only be cropped. Zero asks the
    // source alone, which is bypass and every caller with no raster yet.
    static bool lineDoublingFor(uint16_t sourceLines, uint16_t showableUnits = 0);

    // Whether the line doubler is in the capture path, which the scan mode
    // decides. It runs the IF's line counter at twice the source line rate, so
    // it is the one fact behind both the horizontal decimation and the vertical
    // half-lines. Held rather than read back: InputFormatter::applyScanMode()
    // owns the registers and this owns the arithmetic that has to match them.
    void holdLineDoubling(bool lineDoubled);
    bool lineDoubled() const;
    uint16_t retimeStop() const;

private:
    uint16_t divider_;
    uint32_t lineRateHz_;
    uint16_t sourceLines_;
    float fieldRateHz_;
    float agreedRateHz_;
    uint16_t goodLines_;
    uint32_t goodLineRateHz_;
    uint8_t rateRejections_;
    bool lineDoubled_;

    uint16_t steadyLines_;
    uint8_t steadyRun_;
    uint8_t rateAttempts_;
    bool recoveryTried_;   // the flagged-counter recovery, once per source event
    bool serrationsSeen_;  // the last completed steadiness run read the serrations
    uint32_t referenceRateHz_;  // the estimate the reference sample rate was sized from

    static bool counterFlagged_;
    static void (*counterRecovery_)();
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
