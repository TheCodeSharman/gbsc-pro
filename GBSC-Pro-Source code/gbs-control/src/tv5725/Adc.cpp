#include "Adc.h"

#include <Arduino.h>

namespace Tv5725 {

namespace {

// ADC_INPUT_SEL 0 is the pair carrying Pb and Pr beside Y.
const uint8_t ComponentInputSel = 0;

// ADC_FLTR 0 is the 150 MHz corner, the widest RD-5725-1.1 offers.
const uint8_t WidestFilter = 0;

}  // namespace

namespace {

const uint8_t PllChargePump = 6;
const uint8_t ScalingChargePump = 5;

// How long the PLL is given to settle after the charge pump moves under it.
const unsigned int ChargePumpSettleMs = 40;

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
    ADC_AUTO_OFST_RANGE_REG::write(0x0);         // s5_0f[7:0]
    ADC_AUTO_OFST_V_RANGE::write(0x0);           // s5_0f[7:4]
    PLLAD_TEST::write(0x0);                      // s5_11[2:2]
    PLLAD_TS::write(0x0);                        // s5_11[3:3]

    // The ADC PLL's VCO gain and charge pump current, which only bypass ever
    // wrote: without these the PLL ran on whatever loop current the last bypass
    // excursion chose. 6/1 is what a preset load installed, and it stays
    // steppable -- updateCoastPosition() reads ICP >= 5 && FS == 1 before
    // dropping to 5/0. Neither takes effect until PLLAD_LAT sees a rising edge,
    // which resetPLLAD() supplies well after BringUp::init().
    PLLAD_FS::write(0x1);                        // s5_11[5:5]
    PLLAD_BPS::write(0x0);                       // s5_11[6:6]
    PLLAD_ND::write(0x0);                        // s5_14[11:0]
    PLLAD_ICP::write(0x6);                       // s5_17[2:0]
    PA_ADC_LOCKOFF::write(0x0);                  // s5_18[6:6]
    PA_SP_LOCKOFF::write(0x0);                   // s5_19[6:6]
}

void Adc::latch()
{
    PLLAD_LAT::write(0);
    delayMicroseconds(128);
    PLLAD_LAT::write(1);
}

uint8_t Adc::phaseSyncProcessor_ = 16;
uint8_t Adc::phaseAdc_ = 16;

// Which of the two the chip comes up on is whatever the last boot left, and no
// read-back can be trusted to mean the source is component, so the engine
// captures RGB until an input is selected.
uint8_t Adc::inputSel_ = 1;

void Adc::choosePhaseSyncProcessor(uint8_t phase)
{
    if (phase <= PhaseMax)
        phaseSyncProcessor_ = phase;
}

void Adc::choosePhaseAdc(uint8_t phase)
{
    if (phase <= PhaseMax)
        phaseAdc_ = phase;
}

uint8_t Adc::phaseSyncProcessor() { return phaseSyncProcessor_; }

uint8_t Adc::phaseAdc() { return phaseAdc_; }

namespace {

// Half a sample of phase, which is half the five-bit field.
uint8_t halfSampleOn(uint8_t phase)
{
    return (uint8_t)((phase + 16) & Adc::PhaseMax);
}

// The mid of the field, which is what a caller with nothing to search gets.
const uint8_t MidField = 16;

// How far the search walks. Two more than the field, so the window either side
// of the phase it started on is scored with both its neighbours.
const uint8_t SweepSteps = 34;

// How many of the sweep's samples must be clean before its answer is believed.
// Half the field plus one: below that the good phases are not a window, they
// are scatter.
const uint8_t SweepGoodEnough = 17;

// Samples taken at each phase before it is scored.
const uint8_t SamplesPerPhase = 20;

}  // namespace

bool Adc::acquirePhase(uint8_t oversample, bool sweep,
                       bool halfSampleAtOversampleTwo,
                       uint16_t (*lineSamples)(), void (*feedWatchdog)())
{
    // What the sync processor should be counting, whoever wrote it: bypass puts
    // its own divider here without going through a measurement.
    const uint16_t perLine = PLLAD_MD::read();

    if (!sweep) {
        choosePhaseSyncProcessor(MidField);
        choosePhaseAdc(oversample == 4 ? halfSampleOn(MidField) : MidField);
        delay(8);
        applyPhases();
        return true;
    }

    uint8_t worstScore = 0, worstPhase = 0, clean = 0;
    uint8_t badHere = 0, badBefore = 0, badBeforeThat = 0;
    uint8_t phase = phaseSyncProcessor();

    for (uint8_t step = 0; step < SweepSteps; ++step) {
        phase = (uint8_t)((phase + 1) & PhaseMax);
        choosePhaseSyncProcessor(phase);
        applyPhaseSyncProcessor(phase);

        badHere = 0;
        feedWatchdog();
        delayMicroseconds(256);
        feedWatchdog();
        for (uint8_t i = 0; i < SamplesPerPhase; ++i) {
            if (lineSamples() != perLine) {
                ++badHere;
                feedWatchdog();
                delayMicroseconds(384);
            }
        }

        // Scored over three neighbours, so one bad phase beside two clean ones
        // does not out-vote a run of three.
        const uint8_t window = (uint8_t)(badHere + badBefore + badBeforeThat);
        if (window > worstScore) {
            worstScore = window;
            worstPhase = (uint8_t)((phase - 1) & PhaseMax);
        }
        if (badHere == 0)
            ++clean;

        badBeforeThat = badBefore;
        badBefore = badHere;
    }

    if (clean < SweepGoodEnough)
        return false;

    if (worstScore != 0) {
        choosePhaseSyncProcessor(halfSampleOn(worstPhase));
        choosePhaseAdc(oversample == 4 || halfSampleAtOversampleTwo
                           ? halfSampleOn(MidField) : MidField);
    } else {
        choosePhaseSyncProcessor(MidField);
        choosePhaseAdc(oversample == 4 ? halfSampleOn(MidField) : MidField);
    }

    applyPhaseSyncProcessor(phaseSyncProcessor());
    delay(1);
    applyPhaseAdc(phaseAdc());
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
}

void Adc::applyPhaseAdc(uint8_t phase)
{
    if (phase > PhaseMax)
        return;
    PA_ADC_LAT::write(0);
    PA_ADC_S::write(phase);
    PA_ADC_LAT::write(1);
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
    if (ckoHz >= 80000000u)
        return 0;
    if (ckoHz >= 40000000u)
        return 1;
    if (ckoHz >= 20000000u)
        return 2;
    return 3;
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

void Adc::enableGainMeasurement(bool on)
{
    DEC_TEST_ENABLE::write(on ? 1 : 0);
}

void Adc::applyForBypassRgbhv()
{
    ADC_FLTR::write(0);
    PLLAD_ICP::write(4);

    ADC_TA_05_CTRL::write(0x02);
    ADC_TEST_04::write(0x02);
    ADC_TEST_0C::write(0x12);
}

void Adc::applyScalingChargePump()
{
    if (PLLAD_ICP::read() < PllChargePump)
        return;

    PLLAD_ICP::write(ScalingChargePump);
    latch();
    delay(ChargePumpSettleMs);
}

uint8_t Adc::applySampleRate(uint16_t divider, uint32_t lineRateHz,
                             uint8_t oversample)
{
    if (lineRateHz == 0) {
        // No CKO, so no row to read the crossover table against. The divider is
        // the caller's own and still goes in; picking a row by arithmetic on a
        // zero would be a guess wearing a calculation's clothes.
        PLLAD_MD::write(divider);
        latch();
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

    latch();
    return ratio;
}

}  // namespace Tv5725
