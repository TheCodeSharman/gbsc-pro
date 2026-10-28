#include "Adc.h"

#include "SyncProcessor.h"
#include "Tv5725Log.h"

#include <Arduino.h>
#include <stdio.h>

namespace Tv5725 {

const uint32_t Adc::MaxSampleRateHz;
const uint16_t Adc::DividerMax;
const uint16_t Adc::BringUpDivider;
const uint32_t Adc::BringUpLineRateHz;
const uint16_t Adc::LatchedSamplesTolerance;
const uint8_t Adc::NeutralOffset;
const uint8_t Adc::MidField;
const uint8_t Adc::SetupPhaseSyncProcessor;
const uint8_t Adc::AutoGainInitial;

namespace {
uint8_t atLeastOneOversample(uint8_t oversample)
{
    return oversample == 0 ? 1 : oversample;
}

// The CKO at which the VCO post divider drops a step, off RD-5725-1.1's
// crossover table. postDividerFor() reads down it and maxCkoFor() reads back up
// it, so the rows are written once.
const uint32_t PostDividerRowHz[] = {80000000u, 40000000u, 20000000u};
const uint8_t PostDividerRows = 3;

// The reference clocks, tried in turn until one measures the arriving source.
// A clock is a DIVIDER AND THE LINE RATE ITS CROSSOVER ROW IS READ AGAINST,
// because the row is what the PLL locks or fails to lock on: measured on the
// Wii at 480p, divider 1446 at row 1 reads STATUS_SYNC_PROC_HTOTAL 1446 and the
// same divider at row 2 reads 412. So a divider alone does not name a clock, and
// a list of dividers read against one rate lands every entry on one row.
//
// One entry per line-rate band, each putting the VCO near 127.5 MHz for a source
// IN that band. A source outside the band of the row in force asks for the VCO
// shifted by the bands between them -- 257 MHz for 480p at the 15.6 kHz row,
// against a MaxSampleRateHz of 162.
//
//   2040 @ 15.6 kHz   row 2   VCO 127.5 MHz   counts both bench sources
//   2040 @ 31.2 kHz   row 1   VCO 127.5 MHz
//   1446 @ 15.6 kHz   row 2   VCO  90.4 MHz   latches on vga, not on ypbpr
//   1446 @ 31.2 kHz   row 1   VCO  90.4 MHz
//
// **NO ENTRY LATCHES ON BOTH BENCH SOURCES, WHICH IS WHY THERE IS A LIST.**
// Measured: 2040 reads STATUS_SYNC_PROC_HTOTAL exactly 2040 on vga and 1670 on
// ypbpr at 480p, and 1446 reads 1446 on vga and 871..1201 on ypbpr with the
// count collapsing from 524 to 213. The VCO gain crossover does not separate
// them -- 2040's 127.5 MHz sits 2.5 MHz under HighVcoGainAboveHz and runs the
// low gain at the top of its range on BOTH -- so which entry suits a source is
// not derivable and is searched.
// ../../../docs/investigations/the-reference-clock-latches-on-one-bench-source.md
//
// Both bench bands are covered and nothing here runs faster, so a third band is
// unmeasured rather than ruled out. Each divider fits the input formatter's
// eleven-bit line counter undoubled.
//
// A selection installs the first. **A SECOND ENTRY IS NOT A SPARE FOR THE
// BENCH**: it is there because a reference that cannot be measured through is a
// DEADLOCK rather than a slow start, and no measurement can choose its way out
// of one. A source no entry suits is a source that never acquires, so the list
// is what stops one stated pair being the whole answer.
// ../../../docs/investigations/the-reference-clock-can-deadlock-the-measurement.md
struct ReferenceClock {
    uint16_t divider;
    uint32_t lineRateHz;
};

const ReferenceClock ReferenceClocks[] = {
    {Adc::BringUpDivider, Adc::BringUpLineRateHz},
    {Adc::BringUpDivider, 2 * Adc::BringUpLineRateHz},
    {1446, Adc::BringUpLineRateHz},
    {1446, 2 * Adc::BringUpLineRateHz},
};
const uint8_t ReferenceClockCount =
    sizeof(ReferenceClocks) / sizeof(ReferenceClocks[0]);
}  // namespace

uint32_t Adc::sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                           uint8_t oversample)
{
    return (uint32_t)divider * lineRateHz * atLeastOneOversample(oversample);
}

bool Adc::withinLimit(uint16_t divider, uint32_t lineRateHz, uint8_t oversample)
{
    if (lineRateHz == 0)
        return false;

    // At the oversampling the crossover row will actually install for this
    // divider, which is what the part converts at. Asking at the requested
    // ratio answers a question about a load the chip refuses to take.
    const uint32_t cko = (uint32_t)divider * lineRateHz;
    return sampleRateHz(divider, lineRateHz,
                        oversampleFor(postDividerFor(cko), oversample))
           <= MaxSampleRateHz;
}

uint16_t Adc::maxDivider(uint32_t lineRateHz, uint8_t oversample)
{
    if (lineRateHz == 0)
        return 0;

    // The crossover row bounds this, not the conversion rating: the row is read
    // against CKO, which is the divider alone, and a divider past the row's top
    // installs a lower ratio than the one asked about. Answering at the rating
    // instead describes a configuration the part will not be in.
    //
    // Integer throughout: the ESP8266 has no FPU and this runs on every solve.
    const uint32_t largest = maxCkoFor(oversample) / lineRateHz;

    if (largest > DividerMax)
        return DividerMax;
    return (uint16_t)largest;
}

namespace {

// ADC_INPUT_SEL 0 is the pair carrying Pb and Pr beside Y.
const uint8_t ComponentInputSel = 0;

// ADC_FLTR 0 is the 150 MHz corner, the widest RD-5725-1.1 offers.
const uint8_t WidestFilter = 0;

}  // namespace

namespace {

// The ADC PLL's charge pump. One value for every path: measured in
// pass-through at a 128 MHz VCO, no value the field can hold restores lock, so
// there is nothing for a path to choose between.
const uint8_t PllChargePump = 6;

}  // namespace

const uint8_t Adc::OversampleAsClockAllows;

void Adc::selectInput(uint8_t inputSel)
{
    inputSel_ = inputSel;
    ADC_INPUT_SEL::write(inputSel);
}

bool Adc::inputIsComponent() { return inputSel_ == ComponentInputSel; }

void Adc::enableSyncOnGreen(uint8_t enable)
{
    ADC_SOGEN::write(enable);
}

void Adc::init()
{
    ADC_CLK_PA::write(0x0);                      // s5_00[1:0]
    ADC_CLK_PLLAD::write(0x0);                   // s5_00[2:2]
    ADC_POWDZ::write(0x1);                       // s5_03[0:0]

    // The anti-alias low-pass in front of the sampler, widest corner. It only
    // does work below Nyquist, and the narrowest the part offers is above it at
    // every sample clock this board reaches. Swept on the picture at 34.5 MHz
    // and 77.2 MHz: all four corners indistinguishable.
    ADC_FLTR::write(WidestFilter);                 // s5_03[5:4]
    ADC_TR_RSEL::write(0x2);                     // s5_04[1:0]
    ADC_TR_ISEL::write(0x0);                     // s5_04[4:2]
    ADC_TA_EN::write(0x0);                       // s5_05[0:0]
    ADC_TA_CTRL::write(0x1);                     // s5_05[4:1]
    ADC_CKBS::write(0x0);                        // s5_0c[0:0]
    ADC_TEST::write(0x9);                        // s5_0c[4:1]
    ADC_AUTO_OFST_EN::write(0x0);                // s5_0e[0:0]
    ADC_AUTO_OFST_U_RANGE::write(0x0);           // s5_0f[3:0]
    ADC_AUTO_OFST_PRD::write(0x1);               // s5_0e[1:1]
    ADC_AUTO_OFST_DELAY::write(0x0);             // s5_0e[3:2]
    ADC_AUTO_OFST_STEP::write(0x0);              // s5_0e[5:4]
    ADC_AUTO_OFST_TEST::write(0x1);              // s5_0e[7:7]
    ADC_AUTO_OFST_U_RANGE::write(0x0);           // s5_0f[3:0]
    ADC_AUTO_OFST_V_RANGE::write(0x0);           // s5_0f[7:4]
    ADC_AUTO_OFST_V_RANGE::write(0x0);           // s5_0f[7:4]
    PLLAD_TEST::write(0x0);                      // s5_11[2:2]
    PLLAD_TS::write(0x0);                        // s5_11[3:3]

    // The ADC PLL's VCO gain and charge pump. applySampleRate() owns both --
    // the gain derived from the VCO, the pump constant -- so these are only
    // what the part holds between bring-up and the first solve. Neither takes
    // effect until PLLAD_LAT sees a rising edge, which restartPll() supplies
    // well after BringUp::init().
    PLLAD_FS::write(0x1);                        // s5_11[5:5]
    PLLAD_BPS::write(0x0);                       // s5_11[6:6]
    PLLAD_ND::write(0x0);                        // s5_14[11:0]
    PLLAD_ICP::write(PllChargePump);             // s5_17[2:0]
    PA_ADC_LOCKOFF::write(0x0);                  // s5_18[6:6]
    PA_SP_LOCKOFF::write(0x0);                   // s5_19[6:6]

    enablePll();
    applyPllLoopFilter();
    applyDecimatorModes();
    clearUnusedRegisters();
}

void Adc::latch()
{
    PLLAD_LAT::write(0);
    delayMicroseconds(128);
    PLLAD_LAT::write(1);
}

void Adc::holdPllInReset()
{
    PLLAD_VCORST::write(1);
    PLLAD_PDZ::write(0);
    PLLAD_LEN::write(0);
    PLLAD_TEST::write(0);
    PLLAD_TS::write(0);
    PLLAD_FS::write(0);
    PLLAD_BPS::write(0);
    PLLAD_LAT::write(0);
}

void Adc::restartPll()
{
    PLLAD_VCORST::write(1);
    PLLAD_PDZ::write(1);
    latch();
    PLLAD_VCORST::write(0);
    delay(1);
    restartPhaseAdjusters();
    PLLAD_LEN::write(1);
    latch();
}

uint8_t Adc::phaseSyncProcessor_ = 16;
uint8_t Adc::phaseAdc_ = 16;
uint8_t Adc::phaseOverride_ = 16;
bool Adc::phaseOverridden_ = false;
uint8_t Adc::phaseAdcOverride_ = 16;
bool Adc::phaseAdcOverridden_ = false;

// Which of the two the chip comes up on is whatever the last boot left, and no
// read-back can be trusted to mean the source is component, so the engine
// captures RGB until an input is selected.
uint8_t Adc::inputSel_ = 1;

// Nothing has been installed, so the ratio is one sample a clock -- the same
// thing a post divider with no room to give reduces every request to.
uint8_t Adc::oversampleInForce_ = 1;
uint16_t Adc::dividerInForce_ = 0;
uint32_t Adc::rateInForce_ = 0;

uint8_t Adc::referenceStep_ = 0;
bool Adc::referenceInForce_ = false;
bool Adc::phaseFound_ = false;
bool Adc::clockSuspect_ = false;

void Adc::choosePhaseSyncProcessor(uint8_t phase)
{
    if (phaseOverridden_)
        return;
    if (phase <= PhaseMax)
        phaseSyncProcessor_ = phase;
}

void Adc::overridePhaseSyncProcessor(uint8_t phase)
{
    if (phase > PhaseMax)
        return;
    phaseOverridden_ = true;
    phaseOverride_ = phase;
    phaseSyncProcessor_ = phase;
}

void Adc::forgetPhaseOverride() { phaseOverridden_ = false; }

bool Adc::phaseOverridden() { return phaseOverridden_; }

void Adc::choosePhaseAdc(uint8_t phase)
{
    if (phaseAdcOverridden_)
        return;
    if (phase <= PhaseMax)
        phaseAdc_ = phase;
}

void Adc::overridePhaseAdc(uint8_t phase)
{
    if (phase > PhaseMax)
        return;
    phaseAdcOverridden_ = true;
    phaseAdcOverride_ = phase;
    phaseAdc_ = phase;
}

void Adc::forgetPhaseAdcOverride() { phaseAdcOverridden_ = false; }

bool Adc::phaseAdcOverridden() { return phaseAdcOverridden_; }

uint8_t Adc::phaseSyncProcessor() { return phaseSyncProcessor_; }

uint8_t Adc::phaseAdc() { return phaseAdc_; }

namespace {

// Half a sample of phase, which is half the five-bit field.
uint8_t halfSampleOn(uint8_t phase)
{
    return (uint8_t)((phase + 16) & Adc::PhaseMax);
}

// The middle of the widest run of phases the sweep counted clean, wrapping.
//
// **A MAXIMUM IS DECIDED BY ONE OUTLIER.** Scoring the worst window and taking
// half a field from it is only right where the single worst reading belongs to
// the genuine band: a phase disturbed while the walk is on it scores all
// SamplesPerPhase of its samples, where a real band scores a fraction of them,
// so the outlier wins and half a field from it is the band itself. Measured on
// the bench, a band at 10..16 with the engine holding 9 and the picture
// shimmering there. A run's middle moves by one phase when an outlier splits
// it, and by the width of the band when the band moves, which is the
// sensitivity wanted.
// ../../../docs/investigations/the-sampling-phase-cannot-reach-the-shimmer.md
void widestCleanRun(const uint8_t *dither, uint8_t *atStart, uint8_t *runLength)
{
    uint8_t bestStart = 0, bestLength = 0, start = 0, length = 0;
    for (uint16_t step = 0; step < 2 * (Adc::PhaseMax + 1); ++step) {
        const uint8_t phase = (uint8_t)(step & Adc::PhaseMax);
        if (dither[phase] != 0) {
            length = 0;
            continue;
        }
        if (length == 0)
            start = phase;
        ++length;
        if (length > bestLength && length <= Adc::PhaseMax + 1) {
            bestLength = length;
            bestStart = start;
        }
    }
    *atStart = bestStart;
    *runLength = bestLength;
}

uint8_t middleOfWidestCleanRun(const uint8_t *dither)
{
    uint8_t bestStart = 0, bestLength = 0;
    widestCleanRun(dither, &bestStart, &bestLength);
    if (bestLength == 0)
        return Adc::MidField;
    return (uint8_t)((bestStart + bestLength / 2) & Adc::PhaseMax);
}

// How far the search walks. Two more than the field, so the first phases
// reached are measured again and the second reading is the one kept: the walk
// starts by moving off whatever phase was in force, and a phase measured
// immediately after that move has not settled the way the rest have.
const uint8_t SweepSteps = 34;

// Samples taken at each phase before it is scored.
const uint8_t SamplesPerPhase = 20;

// The shortest contiguous run of clean phases still worth calling a window.
// Below it the clean phases are SCATTER, which is what an unlocked sampling
// clock leaves -- the count misses the divider at every phase and whichever
// ones happen to read it are isolated. A wide band on a healthy source leaves
// a dozen or more contiguous instead, so nothing sits near this.
const uint8_t CleanRunIsAWindow = 4;

// One character per phase, carrying BOTH of the walk's metrics: '.' for a phase
// whose count read the divider exactly, a digit or lowercase letter for one
// that was a single count out, and an UPPERCASE letter for one further off
// than that. One field rather than two, because reported as two adjacent lines
// the second never arrived on the bench.
char scanDigit(uint8_t dither, uint8_t far)
{
    if (far != 0)
        return (char)('A' + (far > 26 ? 25 : far - 1));
    if (dither == 0)
        return '.';
    return dither < 10 ? (char)('0' + dither)
                       : (char)('a' + (dither > 35 ? 25 : dither - 10));
}

// The scoreboard the walk built, indexed by phase rather than by the step it
// was reached on. Reported whether or not the search then accepts it: a refusal
// is the case the counts are wanted for.
void reportScan(const uint8_t *dither, const uint8_t *far)
{
    char line[Adc::PhaseMax + 64];
    int at = snprintf(line, sizeof(line), "phase scan: ");
    for (uint8_t phase = 0; phase <= Adc::PhaseMax; ++phase)
        line[at++] = scanDigit(dither[phase], far[phase]);

    // WHAT THE BOARD IS MEASURED AGAINST, so a shifted band can be attributed
    // without a second capture. The walk scores each phase by how steadily the
    // sync processor counts the line, and where the counter STARTS is
    // SP_RT_HS_SP -- so a board whose dirty band has moved along the phase axis
    // is read beside the origin that places it.
    // ../../../docs/investigations/a-failed-phase-sweep-falls-back-onto-the-bands-middle.md
    snprintf(line + at, sizeof(line) - at, " md %u rt %u",
             (unsigned)Adc::PLLAD_MD::read(),
             (unsigned)SyncProcessor::SP_RT_HS_SP::read());
    tv5725Log(line);
}

}  // namespace

bool Adc::phaseFound() { return phaseFound_; }

bool Adc::samplingClockSuspect() { return clockSuspect_; }

void Adc::forgetPhase() { phaseFound_ = false; }

bool Adc::acquirePhase(uint8_t oversample, bool sweep,
                       uint16_t (*lineSamples)(), void (*feedWatchdog)())
{
    phaseFound_ = false;
    clockSuspect_ = false;

    if (phaseOverridden_) {
        applyPhaseSyncProcessor(phaseOverride_);
        applyPhaseAdc(phaseAdc());
        phaseFound_ = true;
        return true;
    }

    // What the sync processor should be counting, whoever wrote it: bypass puts
    // its own divider here without going through a measurement.
    const uint16_t perLine = PLLAD_MD::read();

    if (!sweep) {
        choosePhaseSyncProcessor(MidField);
        choosePhaseAdc(oversample == 4 ? halfSampleOn(MidField) : MidField);
        delay(8);
        applyPhases();
        phaseFound_ = true;
        return true;
    }

    uint8_t phase = phaseSyncProcessor();
    const uint8_t enteredAt = phase;

    // Two phases are walked twice -- the sweep is two steps longer than the
    // field -- and the second visit is the one kept.
    uint8_t dither[PhaseMax + 1], far[PhaseMax + 1];
    for (uint8_t i = 0; i <= PhaseMax; ++i) {
        dither[i] = 0;
        far[i] = 0;
    }

    for (uint8_t step = 0; step < SweepSteps; ++step) {
        phase = (uint8_t)((phase + 1) & PhaseMax);
        choosePhaseSyncProcessor(phase);
        applyPhaseSyncProcessor(phase);

        uint8_t badHere = 0;
        uint8_t farHere = 0;
        feedWatchdog();
        delayMicroseconds(256);
        feedWatchdog();
        for (uint8_t i = 0; i < SamplesPerPhase; ++i) {
            const uint16_t at = lineSamples();
            const uint16_t off = at > perLine ? at - perLine : perLine - at;
            if (off > 1)
                ++farHere;
            if (off != 0) {
                ++badHere;
                feedWatchdog();
                delayMicroseconds(384);
            }
        }
        dither[phase] = badHere;
        far[phase] = farHere;
    }

    reportScan(dither, far);

    // THE RUN DECIDES, NOT THE CLEAN COUNT. Scatter is the clock and a window
    // is a band, and the count cannot separate them: a healthy pass-through
    // source scores about half the field clean while leaving a contiguous
    // window of a dozen or more, so a gate at half the field refuses it and
    // throws away the answer the profile holds.
    uint8_t runStart = 0, runLength = 0;
    widestCleanRun(dither, &runStart, &runLength);

    if (runLength < CleanRunIsAWindow) {
        clockSuspect_ = true;

        // NOTHING TO CHOOSE FROM. No phase read the divider, so the profile is
        // noise and middleOfWidestCleanRun() answers MidField out of it --
        // picking that is worse than leaving the phase something chose. The
        // walk latched every phase it scored, so the entry phase has to be put
        // back explicitly, and a refusal re-runs on the maintenance cadence.
        choosePhaseSyncProcessor(enteredAt);
        applyPhaseSyncProcessor(enteredAt);
        return false;
    }

    // AS FAR FROM THE BAND AS THE SCAN CAN PUT IT. Keeping MidField wherever
    // the scan scored it clean put the picture on the band's SHOULDER: at
    // 800x600@60 separate the band is 7..15 and MidField is the first phase
    // after it, measured five to ten times worse by displacement than the
    // run's middle, which the metric cannot see because it scores the count
    // rather than the picture. ../../../docs/known-issues.md
    choosePhaseSyncProcessor(middleOfWidestCleanRun(dither));
    choosePhaseAdc(oversample == 4 ? halfSampleOn(MidField) : MidField);

    applyPhaseSyncProcessor(phaseSyncProcessor());
    delay(1);
    applyPhaseAdc(phaseAdc());
    phaseFound_ = true;
    return true;
}

void Adc::applyPhases()
{
    applyPhaseSyncProcessor(phaseSyncProcessor_);
    applyPhaseAdc(phaseAdc_);
}

void Adc::nudgePhaseAdc()
{
    phaseAdc_ = (uint8_t)((phaseAdc_ + 1) & PhaseMax);
}

void Adc::applyPhaseSyncProcessor(uint8_t phase)
{
    if (phase > PhaseMax)
        return;
    PA_SP_LAT::write(0);
    PA_SP_S::write(phase);
    PA_SP_LAT::write(1);
    PA_SP_BYPSZ::write(0);
    PA_SP_BYPSZ::write(1);
    delay(2);
}

void Adc::applyPhaseAdc(uint8_t phase)
{
    if (phase > PhaseMax)
        return;
    PA_ADC_LAT::write(0);
    PA_ADC_S::write(phase);
    PA_ADC_LAT::write(1);
    PA_ADC_BYPSZ::write(0);
    PA_ADC_BYPSZ::write(1);
    delay(2);
}

void Adc::restartPhaseAdjusters()
{
    PA_SP_BYPSZ::write(0);
    PA_SP_BYPSZ::write(1);
    delay(2);
    PA_ADC_BYPSZ::write(0);
    PA_ADC_BYPSZ::write(1);
    delay(2);
}

uint8_t Adc::selectOtherInput()
{
    const uint8_t selected = inputSel_;
    selectInput(selected == 1 ? 0 : 1);
    return selected;
}

void Adc::bounceInput()
{
    const uint8_t selected = inputSel_;

    ADC_INPUT_SEL::write(0);
    delay(BounceMs);
    selectInput(selected);
}

uint8_t Adc::postDividerFor(uint32_t ckoHz)
{
    for (uint8_t row = 0; row < PostDividerRows; ++row)
        if (ckoHz >= PostDividerRowHz[row])
            return row;
    return PostDividerRows;
}

uint32_t Adc::maxCkoFor(uint8_t oversample)
{
    const uint8_t steps = stepsFor(atLeastOneOversample(oversample));
    if (steps == 0)
        return MaxSampleRateHz;
    if (steps > PostDividerRows)
        return 0;
    return PostDividerRowHz[steps - 1] - 1;
}

uint8_t Adc::vcoGainFor(uint32_t vcoHz)
{
    return vcoHz >= HighVcoGainAboveHz ? 1 : 0;
}

uint8_t Adc::oversampleFor(uint8_t postDivider, uint8_t wanted)
{
    uint8_t ratio = wanted < 1 ? 1 : wanted;
    while (ratio > 1 && stepsFor(ratio) > postDivider)
        ratio /= 2;
    return ratio;
}

uint8_t Adc::oversampleInForce() { return oversampleInForce_; }

uint16_t Adc::dividerInForce() { return dividerInForce_; }

uint32_t Adc::rateInForce() { return rateInForce_; }

// PA_SP_S's quiet band is a property of the divider, so a phase searched at one
// is not an answer at another. ../../../../docs/known-issues.md
void Adc::holdDivider(uint16_t divider)
{
    if (divider != dividerInForce_)
        phaseFound_ = false;
    dividerInForce_ = divider;
}

void Adc::applyDivider(uint16_t divider)
{
    holdDivider(divider);
    PLLAD_MD::write(divider);
    latch();
}

void Adc::clearUnusedRegisters()
{
    ADC_UNUSED_64::write(0);
    ADC_UNUSED_65::write(0);
    ADC_UNUSED_66::write(0);
    ADC_UNUSED_67::write(0);
}

void Adc::enablePll() { PLLAD_LEN::write(1); }

void Adc::applyPllLoopFilter()
{
    PLLAD_R::write(3);
    PLLAD_S::write(3);
}

void Adc::applyDecimatorModes()
{
    DEC_IDREG_EN::write(1);
    DEC_WEN_MODE::write(1);
}

void Adc::applyResetParameters()
{
    applyPllLoopFilter();
    installReferenceSamplingClock();
}

void Adc::installReferenceSamplingClock()
{
    referenceStep_ = 0;
    installReferenceStep();
}

void Adc::installNextReferenceSamplingClock()
{
    referenceStep_ = (uint8_t)((referenceStep_ + 1) % ReferenceClockCount);
    installReferenceStep();
}

uint8_t Adc::referenceDividerCount() { return ReferenceClockCount; }

bool Adc::referenceSamplingClockInForce() { return referenceInForce_; }

void Adc::forgetReferenceClock() { referenceInForce_ = false; }

void Adc::installReferenceStep()
{
    installReferenceDivider(ReferenceClocks[referenceStep_].divider,
                            ReferenceClocks[referenceStep_].lineRateHz);
}

void Adc::installReferenceDivider(uint16_t divider, uint32_t lineRateHz)
{
    // The whole group, not the divider alone. A divider written without the
    // crossover row and the VCO gain puts the PLL on a frequency the hardware
    // will not run, which is a solid green screen with every register
    // self-consistent.
    applySampleRate(divider, lineRateHz, OversampleAsClockAllows);
    referenceInForce_ = true;
}

bool Adc::dividerLatched(uint16_t lineSamples, uint16_t tolerance)
{
    if (dividerInForce_ == 0)
        return false;

    const uint16_t larger = lineSamples > dividerInForce_ ? lineSamples : dividerInForce_;
    const uint16_t smaller = lineSamples > dividerInForce_ ? dividerInForce_ : lineSamples;
    return (uint16_t)(larger - smaller) <= tolerance;
}

uint8_t Adc::stepsFor(uint8_t oversample)
{
    uint8_t steps = 0;
    for (uint8_t ratio = oversample; ratio > 1; ratio /= 2)
        ++steps;
    return steps;
}

uint8_t Adc::applyOversample(uint8_t postDivider, uint8_t oversample)
{
    const uint8_t ratio = oversampleFor(postDivider, oversample);
    oversampleInForce_ = ratio;

    PLLAD_CKOS::write((uint8_t)(postDivider - stepsFor(ratio)));

    // The decimators undo in the digital domain what the faster tap added, so
    // they follow the ratio rather than the tap.
    ADC_CLK_ICLK1X::write(ratio >= 2 ? 1 : 0);
    ADC_CLK_ICLK2X::write(ratio >= 4 ? 1 : 0);
    DEC1_BYPS::write(ratio >= 4 ? 0 : 1);
    DEC2_BYPS::write(ratio >= 2 ? 0 : 1);

    return ratio;
}

void Adc::applyGain(uint8_t r, uint8_t g, uint8_t b)
{
    ADC_RGCTRL::write(r);
    ADC_GGCTRL::write(g);
    ADC_BGCTRL::write(b);
}

void Adc::applyOffset(uint8_t r, uint8_t g, uint8_t b)
{
    ADC_ROFCTRL::write(r);
    ADC_GOFCTRL::write(g);
    ADC_BOFCTRL::write(b);
}

uint8_t Adc::gain_[3] = {0, 0, 0};
uint8_t Adc::offset_[3] = {0, 0, 0};

void Adc::holdGain(uint8_t r, uint8_t g, uint8_t b)
{
    gain_[0] = r;
    gain_[1] = g;
    gain_[2] = b;
    applyGain(r, g, b);
}

uint8_t Adc::gain() { return gain_[0] == 0 ? AutoGainInitial : gain_[0]; }

void Adc::applyHeldGain()
{
    if (gain_[0] == 0)
        holdGain(AutoGainInitial, AutoGainInitial, AutoGainInitial);
    else
        applyGain(gain_[0], gain_[1], gain_[2]);
}

void Adc::armGainMeasurement(bool wanted)
{
    if (wanted)
        applyHeldGain();
    enableGainMeasurement(wanted);
}

void Adc::stepGain(int8_t by)
{
    const uint8_t gain = ADC_RGCTRL::read() + by;
    holdGain(gain, gain, gain);
}

void Adc::forgetGain() { gain_[0] = gain_[1] = gain_[2] = 0; }

void Adc::holdOffset(uint8_t r, uint8_t g, uint8_t b)
{
    offset_[0] = r;
    offset_[1] = g;
    offset_[2] = b;
    applyOffset(r, g, b);
}

void Adc::applyHeldOffset()
{
    if (offset_[0] != 0 && offset_[1] != 0 && offset_[2] != 0)
        applyOffset(offset_[0], offset_[1], offset_[2]);
    else
        applyOffset(NeutralOffset, NeutralOffset, NeutralOffset);
}

void Adc::forgetOffset() { offset_[0] = offset_[1] = offset_[2] = 0; }

void Adc::enableGainMeasurement(bool on)
{
    DEC_TEST_ENABLE::write(on ? 1 : 0);
}

void Adc::applyReferenceTrim()
{
    ADC_TA_EN::write(0);
    ADC_TA_CTRL::write(1);
    ADC_TR_RSEL::write(2);
    ADC_TR_ISEL::write(0);
    ADC_CKBS::write(0);
    ADC_TEST::write(9);
}

void Adc::applyForBypassRgbhv()
{
    ADC_FLTR::write(0);

    applyReferenceTrim();
}

void Adc::reapplySampleRate()
{
    if (dividerInForce_ == 0)
        return;

    applySampleRate(dividerInForce_, rateInForce_, oversampleInForce_);
}

uint8_t Adc::applySampleRate(uint16_t divider, uint32_t lineRateHz,
                             uint8_t oversample)
{
    holdDivider(divider);
    rateInForce_ = lineRateHz;

    // The charge pump is part of the loop the latch loads, so it goes in here
    // rather than being left to whichever path ran last. One value serves every
    // path: measured in pass-through at a 128 MHz VCO, no value the field can
    // hold restores lock, so the pump is not what a path gets to choose.
    PLLAD_ICP::write(PllChargePump);

    if (lineRateHz == 0) {
        // No CKO, so no row to read the crossover table against. The divider is
        // the caller's own and still goes in; picking a row by arithmetic on a
        // zero would be a guess wearing a calculation's clothes.
        PLLAD_MD::write(divider);
        restartPll();
        return oversample < 1 ? 1 : oversample;
    }

    const uint32_t ckoHz = (uint32_t)divider * lineRateHz;
    const uint8_t postDivider = postDividerFor(ckoHz);

    PLLAD_MD::write(divider);
    PLLAD_KS::write(postDivider);

    // The post divider sits between the VCO and CKO, so the VCO runs at CKO
    // shifted up by it -- and the gain follows the VCO rather than either of
    // the two frequencies the caller handed in.
    PLLAD_FS::write(vcoGainFor(ckoHz << postDivider));

    uint8_t ratio = applyOversample(postDivider, oversample);

    restartPll();
    return ratio;
}

}  // namespace Tv5725
