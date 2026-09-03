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
void tv5725Log(const char *message);

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

    // The divider to write at a MODE CHANGE: under the ADC rating by
    // RecommendedPercent, and even, so ifLineFor() divides exactly rather than
    // truncating half a sample away. A zoom must never move it -- that would
    // resample the picture the user is watching.
    //
    // Returns 0 for an unmeasurable line rate rather than a default. A divider
    // written from a measurement that did not happen takes the sync processor
    // with it, leaving no picture to diagnose from.
    static uint16_t recommendedDivider(uint32_t lineRateHz, uint8_t oversample,
                                       bool lineDoubled);

    // Nothing chosen yet. A caller must be able to SEE that rather than get a
    // zero it would go on to write.
    SourceMeasurement();

    // Read the line rate off the chip -- field rate x source lines -- and hold
    // both inputs, because the cross-check inside can say the two disagree but
    // never which of them was wrong. False when it is unmeasurable.
    // How many consecutive agreeing samples make the source worth measuring.
    static const uint8_t SteadySamples = 4;

    // Whether a line count is something video runs at. Both bounds are
    // gross-error nets rather than a classification.
    static bool countIsSource(uint16_t lines);

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

    // The line rate one HPERIOD_IF reading states: 27 MHz / ((hperiod + 1) * 4),
    // measured across ten modes to a mean 29 ns. Counted against the chip's own
    // 27 MHz, so it does not move with PLLAD_MD.
    static uint32_t lineRateForHPeriod(uint16_t hperiod);

    // How far two HPERIOD_IF readings may differ and still be the same reading.
    // A settled source moves by one count; a railed one by hundreds.
    static const uint16_t HPeriodAgreement = 2;

    // The line rate a RUN of HPERIOD_IF readings implies, or 0 when the run
    // cannot be believed. One register read against getSourceFieldRate()'s
    // vsync spin, but it rails with nothing to say so -- STATUS_IF_HT_OK reads
    // 1 either way -- so the run and the line count are the judgement: readings
    // that disagree are railing, and a steady one implying a field rate no
    // source runs at is the railing's stable form.
    // docs/investigations/hperiod-if-railing.md
    static uint32_t lineRateFromHPeriod(const uint16_t *samples, uint8_t count,
                                        uint16_t lines);

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
    bool solve(uint32_t lineRateHz, uint8_t oversample);

    // The source, as the sync processor counts it. These are the only reads of
    // STATUS_SYNC_PROC_* anywhere: nothing else on the board can supply them,
    // and every other quantity the engine needs it computed itself.
    static uint16_t measureSourceLines();
    static uint16_t measureHsyncLow();

    // How long the sync processor is given to reacquire V after the path moves.
    static const uint16_t OwnVsyncSettleMs = 240;

    // How long after that V may still arrive and count.
    //
    // **SIZED FOR THE TAIL, NOT FOR THE TYPICAL CASE.** Reacquisition is 2-3 ms
    // most of the time and runs to at least 242 ms, so a window near the
    // measured maximum times out on a source that does have its own V sync --
    // and the error is one-directional: a false no latches composite separation
    // onto a separate-sync source, collapsing SP_VTOTAL and the picture with it,
    // while a false yes cannot happen, because the bit only rises when V
    // arrives. Waiting longer costs this once per SOURCE and only where the
    // timeout is the right answer.
    static const uint16_t OwnVsyncWindowMs = 1000;

    // Whether the source brings its own V sync, which is what separates it from
    // composite. Answering it by reading STATUS_SYNC_PROC_VSACT alone is
    // circular -- the bit reports the path already configured -- so this
    // *changes* the path first and restores it afterwards.
    // docs/sync-type-selection.md
    static bool sourceHasOwnVsync(uint32_t (*nowMs)());

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

    // A rate good enough to pick the ADC's crossover row on a pass that has
    // measured none. From the line count, which the divider does not touch --
    // never from the held rate, which is the previous mode's.
    uint32_t estimatedLineRateHz() const;

    // The 15.7 kHz broadcast line, split from the 31.5 kHz VGA one clear of
    // both and of the ~21.8 kHz a programmable source reaches between them.
    static const uint32_t LowLineRateBelowHz = 20000;

    // Forget the source: bypass measures nothing, and the held rate would
    // otherwise outlive the mode that produced it.
    void forgetSource();

    // The last rate that passed the cross-check against the line count.
    // lineRateHz() is the last one MEASURED and a refusal clears it, so a
    // reader that has to survive a sync loss asks this one.
    uint32_t heldLineRateHz() const;

    // Whether the source runs that line. Not on its own whether the vertical
    // interval is serrated -- a separate-sync source of the same rate is not.
    // docs/investigations/serrated-sync-is-not-line-rate.md
    bool lowLineRate() const;

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
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_MEASUREMENT_H_
