#ifndef TV5725_ADC_H
#define TV5725_ADC_H

#include "Tv5725.h"

namespace Tv5725 {

// The ADC and its PLL: power, trim, test paths and the auto-offset that is
// switched off.
//
// The PLL's rate is written here but not decided here: SourceMeasurement holds
// the divider and offers it, and applySampleRate() is the only way in, so the
// write and the latch that loads it cannot be separated by a caller.
// PLLAD_ND is here at 0 and is a different thing: latched by the same edge,
// but not rate.
//
// ADC_AUTO_OFST_EN = 0 leaves the offsets to setAdcParametersGainAndOffset() and
// the auto-gain loop, which are per source and belong in the sketch.
// ADC_TR_RSEL, ADC_TR_ISEL, ADC_TA_CTRL and ADC_TEST are analog trim and test
// selects with no derivation available -- what all twelve tables shipped.
class Adc {
public:

    typedef UReg<0x05, 0x00, 0, 2> ADC_CLK_PA;                        // Clock selection for PA_ADC When = 00, PA_ADC input clock
                                                                      // is from PLLAD’s CLKO2 When = 01, PA_ADC input clock is
                                                                      // from PCLKIN When = 10, PA_ADC input clock is from V4CLK

    typedef UReg<0x05, 0x00, 2, 1> ADC_CLK_PLLAD;                     // When = 11, reserved Clock selection for PLLAD When = 0,
                                                                      // PLLAD input clock is from sync processor When = 1, PLLAD
                                                                      // input clock is from OSC

    typedef UReg<0x05, 0x00, 3, 1> ADC_CLK_ICLK2X;                    // ICLK2X control When = 0, ICLK2X = ADC output clock

    typedef UReg<0x05, 0x00, 4, 1> ADC_CLK_ICLK1X;                    // When = 1, ICLK2X = ADC output clock / 2 ICLK1X control
                                                                      // When = 0, ICLK1X = ICLK2X When = 1, ICLK1X = ICLK2X /2

    typedef UReg<0x05, 0x02, 0, 1> ADC_SOGEN;                         // ADC SOG enable When = 0, ADC disable SOG mode

                                                                      // input selection When = 00, R0/G0/B0/SOG0 as input

    typedef UReg<0x05, 0x02, 6, 2> ADC_INPUT_SEL;                     // When = 01, R1/G1/B1/SOG1 as input When = 10, R2/G2/B2 as
                                                                      // input When = 11, reserved


    typedef UReg<0x05, 0x03, 0, 1> ADC_POWDZ;                         // ADC power down control When = 0, ADC in power down mode

    typedef UReg<0x05, 0x03, 1, 1> ADC_RYSEL_R;                       // When = 1, ADC work normally Clamp to ground or midscale
                                                                      // for R ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 2, 1> ADC_RYSEL_G;                       // Clamp to ground or midscale for G ADC When = 0, clamp to
                                                                      // GND

    typedef UReg<0x05, 0x03, 3, 1> ADC_RYSEL_B;                       // When = 1, clamp to midscale Clamp to ground or midscale
                                                                      // for B ADC When = 0, clamp to GND When = 1, clamp to
                                                                      // midscale

    typedef UReg<0x05, 0x03, 4, 2> ADC_FLTR;                          // ADC internal filter control When = 00, 150MHz When = 01,
                                                                      // 110MHz When = 10, 70MHz


    typedef UReg<0x05, 0x04, 0, 2> ADC_TR_RSEL;                       // REF test resistor selection

    typedef UReg<0x05, 0x04, 2, 3> ADC_TR_ISEL;                       // REF test currents selection


    typedef UReg<0x05, 0x05, 0, 1> ADC_TA_EN;                         // ADC test enable When = 0, ADC work normally

    typedef UReg<0x05, 0x05, 1, 4> ADC_TA_CTRL;                       // When = 1, ADC is in test mode ADC test bus control bit

    // The decimator's test path, which is the only instrument the analog gain
    // and offset have: the auto-gain loop reads a channel back through it.
    // ENABLE is SEL's low bit, so clearing one clears part of the other.
    typedef UReg<0x05, 0x1F, 3, 4> DEC_TEST_SEL;
    typedef UReg<0x05, 0x1F, 3, 1> DEC_TEST_ENABLE;

    typedef UReg<0x05, 0x06, 0, 7> ADC_ROFCTRL;                       // Offset control for R channel of ADC

    typedef UReg<0x05, 0x07, 0, 7> ADC_GOFCTRL;                       // Offset control for G channel of ADC

    typedef UReg<0x05, 0x08, 0, 7> ADC_BOFCTRL;                       // Offset control for B channel of ADC

    typedef UReg<0x05, 0x09, 0, 8> ADC_RGCTRL;                        // Gain control for R channel of ADC

    typedef UReg<0x05, 0x0A, 0, 8> ADC_GGCTRL;                        // Gain control for G channel of ADC

    typedef UReg<0x05, 0x0B, 0, 8> ADC_BGCTRL;                        // Gain control for B channel of ADC


    typedef UReg<0x05, 0x0C, 0, 1> ADC_CKBS;                          // ADC output clock invert control When = 0, default

    typedef UReg<0x05, 0x0C, 1, 4> ADC_TEST;                          // When = 1, ADC output clock will be invert For ADC test
                                                                      // reserved

    typedef UReg<0x05, 0x0E, 0, 1> ADC_AUTO_OFST_EN;                  // Auto offset adjustment enable When = 0, auto offset
                                                                      // adjustment disable

    typedef UReg<0x05, 0x0E, 1, 1> ADC_AUTO_OFST_PRD;                 // When = 1, auto offset adjustment enable Offset adjustment
                                                                      // by frame When = 0, offset adjustment by frame When = 1,
                                                                      // offset adjustment by line

    typedef UReg<0x05, 0x0E, 2, 2> ADC_AUTO_OFST_DELAY;               // Horizontal sample delay control When = 00, offset
                                                                      // adjustment horizontal sample delay 1 pipe When = 01,
                                                                      // offset adjustment horizontal sample delay 2 pipe When =
                                                                      // 10, offset adjustment horizontal sample delay 3 pipe

    typedef UReg<0x05, 0x0E, 4, 2> ADC_AUTO_OFST_STEP;                // When = 11, offset adjustment horizontal sample delay 4
                                                                      // pipe Offset adjustment step control When = 00, offset
                                                                      // adjustment by absolute difference. When = 01, offset
                                                                      // adjustment by 1 When = 10, offset adjustment by 2 When =
                                                                      // 11, offset adjustment by 3

    typedef UReg<0x05, 0x0E, 7, 1> ADC_AUTO_OFST_TEST;                // Auto offset adjustment test control


    typedef UReg<0x05, 0x0F, 0, 4> ADC_AUTO_OFST_U_RANGE;             // U channel offset detection range Define U channel offset
                                                                      // detection range 0~15

    typedef UReg<0x05, 0x0F, 4, 4> ADC_AUTO_OFST_V_RANGE;             // V channel offset detection range Define V channel offset
                                                                      // detection range 0~15

    typedef UReg<0x05, 0x11, 0, 8> PLLAD_CONTROL_00_5x11;

    typedef UReg<0x05, 0x11, 0, 1> PLLAD_VCORST;                      // VCORST Initial VCO control voltage

    typedef UReg<0x05, 0x11, 1, 1> PLLAD_LEN;                         // LEN Enable signal for clock

    typedef UReg<0x05, 0x11, 2, 1> PLLAD_TEST;                        // TEST Test clock selection

    typedef UReg<0x05, 0x11, 3, 1> PLLAD_TS;                          // TS Test clock selection and HSL clock selection

    typedef UReg<0x05, 0x11, 4, 1> PLLAD_PDZ;                         // PDZ When = 0, PLLAD is power down mode

    typedef UReg<0x05, 0x11, 5, 1> PLLAD_FS;                          // When = 1, PLLAD work normally FS, VCO gain selection When
                                                                      // = 0, default When = 1, high gain selected

    typedef UReg<0x05, 0x11, 6, 1> PLLAD_BPS;                         // BPS When = 0, default

    typedef UReg<0x05, 0x11, 7, 1> PLLAD_LAT;                         // When = 1, bypass input clock to CKO1 and CKO2 Latch
                                                                      // control for PLLAD control This bit’s rising edge is used
                                                                      // to trigger PLLAD control bit: ND, MD, KS, CKOS, ICP

    typedef UReg<0x05, 0x12, 0, 12> PLLAD_MD;                         // MD[11:8] PLLAD feedback divider control

    typedef UReg<0x05, 0x14, 0, 12> PLLAD_ND;                         // ND[11:8] PLLAD input divider control

    typedef UReg<0x05, 0x16, 0, 8> PLLAD_5_16;

    typedef UReg<0x05, 0x16, 0, 2> PLLAD_R;                           // R Skew control for testing

    typedef UReg<0x05, 0x16, 2, 2> PLLAD_S;                           // S Skew control for testing

    typedef UReg<0x05, 0x16, 4, 2> PLLAD_KS;                          // KS VCO post divider control, it is determined by CKO
                                                                      // frequency When = 00, divide by 1 (162MHz~80MHz) When =
                                                                      // 01, divide by 2 (80MHz~40MHz) When = 10, divide by 4
                                                                      // (40MHz~20MHz) When = 11, divide by 8 (20MHz~min MHz)

    typedef UReg<0x05, 0x16, 6, 2> PLLAD_CKOS;                        // Part of PLLAD_CKOS, which RD-5725-1.1 documents as one
                                                                      // 2-bit block at s5_16 rather than field by field.

    typedef UReg<0x05, 0x17, 0, 3> PLLAD_ICP;                         // ICP Charge pump current selection When = 000, Icp = 50uA
                                                                      // When = 001, Icp = 100uA When = 010, Icp = 150uA When =
                                                                      // 011, Icp = 250uA When = 100, Icp = 350uA When = 101, Icp
                                                                      // = 500uA When = 110, Icp = 750uA When = 111, Icp = 1mA

    typedef UReg<0x05, 0x18, 0, 1> PA_ADC_BYPSZ;                      // BYPSZ, PA for ADC bypass control When = 0, PA_ADC is
                                                                      // bypass

    typedef UReg<0x05, 0x18, 1, 5> PA_ADC_S;                          // When = 1, PA_ADC work normally PA_ADC phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x18, 6, 1> PA_ADC_LOCKOFF;                    // When = 0, default

    typedef UReg<0x05, 0x18, 7, 1> PA_ADC_LAT;                        // When = 1, PA_ADC lock circuit disable PA_ADC latch signal
                                                                      // This bit’s rising edge is used to trigger
                                                                      // PA_ADC_CNTRL_[5:1]

    typedef UReg<0x05, 0x19, 0, 1> PA_SP_BYPSZ;                       // BYPSZ, PA for PLLAD bypass control When = 0, PA_PLLAD is
                                                                      // bypass

    typedef UReg<0x05, 0x19, 1, 5> PA_SP_S;                           // When = 1, PA_PLLAD work normally PA_PLLAD phase control
                                                                      // LOCKOFF

    typedef UReg<0x05, 0x19, 6, 1> PA_SP_LOCKOFF;                     // When = 0, default

    typedef UReg<0x05, 0x19, 7, 1> PA_SP_LAT;                         // When = 1, PA_PLLAD lock circuit disable PA_PLLAD latch
                                                                      // signal This bit’s rising edge is used to trigger
                                                                      // PA_PLLAD_CNTRL_[5:1]

    // Every static register of this subsystem, in address order.
    typedef UReg<0x05, 0x1F, 0, 1> DEC1_BYPS;                         // The 4x to 2x decimator bypass enable When 1, the 4x to 2x
                                                                      // decimator bypass

    typedef UReg<0x05, 0x1F, 1, 1> DEC2_BYPS;                         // The 2x to 1x decimator bypass enable When 1, the 2x to 1x
                                                                      // decimator hypass

    // The analog input mux, and whether sync-on-green is extracted. Separate
    // because the mux is written LAST of the three registers an input choice
    // decides: the sync path is configured before the input is connected to it.
    static void selectInput(uint8_t inputSel);

    // Move to the other of the two RGB inputs and report the one that was in
    // force, so a caller that does not lock on the new one can put it back. The
    // escalation a source that will not lock reaches last, where the guess left
    // is that it is arriving on the other pins.
    static uint8_t selectOtherInput();
    static void enableSyncOnGreen(uint8_t enable);

    // Whether the selected input carries luma and chroma on separate pins, so
    // the capture path has to realign them. Answered from what selectInput()
    // wrote, never read back: a register is where a value is written to, not
    // where it is kept.
    static bool inputIsComponent();

    static void init();

    // A rising edge on PLLAD_LAT loads MD, ND, KS, CKOS and ICP together. The
    // bit is driven low first, because an edge is what loads them and writing 1
    // over a 1 loads nothing.
    static void latch();

    // Reset the VCO under the group and reload it afterwards. Applying the
    // group alone leaves the PLL unlocked at the value written, measured even
    // where that is the value it already held -- and an unlocked ADC PLL leaves
    // the sync processor counting nothing, because it counts in ADC clocks.
    // ../../../docs/investigations/the-ladder-never-restarts-the-adc-pll.md
    static void restartPll();

    // The widest phase the five-bit field carries.
    static const uint8_t PhaseMax = 31;

    // Where in the ADC clock the sample is taken, in 32 steps. Two adjusters,
    // and they are not interchangeable: PA_ADC moves the sample, PA_SP moves
    // what the sync processor retimes against.
    //
    // Each is LATCHED, so the value only reaches the adjuster on a rising edge
    // of its own latch bit -- the same trap PLLAD_MD has, and the reason these
    // are one operation rather than a write the caller follows with a latch.
    //
    // A phase past the field is refused rather than truncated: masking 32 in
    // puts 0 there, which is a phase nobody chose.
    static void applyPhaseSyncProcessor(uint8_t phase);
    static void applyPhaseAdc(uint8_t phase);

    // The phase in force for each adjuster, HELD. Nothing reads it back: the
    // value is one this class chose, and PA_ADC_S reports it only once its own
    // latch has loaded it. docs/video-source-acquisition.md
    static void choosePhaseSyncProcessor(uint8_t phase);
    static void choosePhaseAdc(uint8_t phase);
    static uint8_t phaseSyncProcessor();
    static uint8_t phaseAdc();

    // Both, in one call, because a caller that moves one usually moves the
    // other and the two latches are separate edges.
    static void applyPhases();

    // One step round the ADC's field, for a caller walking it by hand.
    static void nudgePhaseAdc();

    // Choose both phases for the source now arriving, and put them in force.
    // False means nothing was worth choosing and neither phase moved.
    //
    // `sweep` is whether the sync separator is delivering edges clean enough
    // for the search to mean anything; a starved one makes every score noise,
    // and the mid of the field is then the whole of the answer.
    //
    // The ADC's phase follows the oversampling alone, at every exit. Only the
    // sync processor's is searched.
    //
    // `lineSamples` is the sync processor's count per line and `feedWatchdog`
    // is the platform's. Both are handed IN: the count is another block's
    // register, and a file here reaching for ESP.wdtFeed() is a design signal
    // rather than a dependency to admit. docs/video-source-acquisition.md
    static bool acquirePhase(uint8_t oversample, bool sweep,
                             uint16_t (*lineSamples)(),
                             void (*feedWatchdog)());

    // Whether the search above found a phase worth having. Recorded by
    // acquirePhase() rather than by its caller: the answer is about the two
    // adjusters this class owns, and a second copy of it goes stale the moment
    // anything reloads them.
    static bool phaseFound();

    // A different source, or a load that moved the sampling: whatever was found
    // was found against something else.
    static void forgetPhase();

    // Take both adjusters through their bypass and back, which is what makes a
    // newly latched phase take effect.
    static void restartPhaseAdjusters();

    // Take the ADC's input away and give it back, so the input formatter
    // re-acquires the line. The one action measured to clear a railed
    // HPERIOD_IF without touching the source -- 0/16 correct before, 16/16 at
    // the value the mode is due after, twice.
    //
    // **IT ALSO CAUSES THE FAULT**, railing a mode that had read correctly six
    // times beforehand. It is a recovery for a counter already known bad, never
    // something to run in front of a measurement.
    // docs/investigations/hperiod-if-railing.md
    static void bounceInput();

    // How long the input stays away. Shorter has not been tried; 400 ms is what
    // the clearance was measured at.
    static const uint16_t BounceMs = 400;

    // The divider and the latch that loads it. Separating them leaves the PLL
    // running the old value with every register reading back correct.
    // The VCO post divider for a CKO frequency, off RD-5725-1.1's own crossover
    // table. Read against CKO -- what the divider alone produces -- not against
    // the oversampled rate the ADC then runs at.
    static uint8_t postDividerFor(uint32_t ckoHz);

    // The VCO gain PLLAD_FS selects, for a VCO frequency. RD-5725-1.1 calls the
    // bit "0 default, 1 high gain" and gives no band for it, so this is
    // measured rather than derived: swept at 800x600@60, the PLL locks with
    // gain 0 up to 136 MHz and fails by 144, and with gain 1 down to 121 MHz
    // and fails by 106. The threshold sits in that overlap.
    //
    // **IT FOLLOWS THE VCO, NOT CKO AND NOT THE DIVIDER.** 143.9 MHz was
    // reached at two different post dividers -- CKO 72.0 MHz over 2 and 36.0
    // MHz over 4 -- and both need gain 1.
    // ../../../docs/investigations/the-vco-gain-follows-the-vco.md
    static uint8_t vcoGainFor(uint32_t vcoHz);

    static const uint32_t HighVcoGainAboveHz = 130000000;

    // The oversampling that post divider can carry. Each doubling takes an
    // output tap one step faster, and there is none above the top, so a ratio
    // the clock cannot give comes back reduced.
    static uint8_t oversampleFor(uint8_t postDivider, uint8_t wanted);

    // Ask for this and get the most the clock can carry, which is 2^postDivider
    // -- oversampleFor() halves whatever it cannot reach, and RD-5725-1.1's
    // crossover table has no row below /8, so 8 always lands on the maximum.
    //
    // **MORE IS BETTER AND IT IS FREE.** The tap is faster and the decimators
    // undo it, so the same PLLAD_MD samples a line reach the pipeline either
    // way, and the decimators FILTER: measured at 1600x600@60 passed through,
    // doubling the ratio cuts the alias beat on the PM5544 wedge by 17 to 41%
    // on the blocks where the scaler's sampling is the limit, and changes
    // nothing on the blocks where the panel is.
    // ../../../docs/investigations/the-decimators-filter.md
    static const uint8_t OversampleAsClockAllows = 8;

    // The clock tap and the decimators, against a post divider the caller
    // holds. Returns the oversampling actually installed. The latch is not
    // fired: PLLAD_LAT loads MD, ND, KS, CKOS and ICP together, so a caller
    // still assembling that group owns the edge.
    static uint8_t applyOversample(uint8_t postDivider, uint8_t oversample);

    // Everything PLLAD_LAT loads, and the decimators that follow the tap it
    // selects, from the rate the caller holds. Returns the oversampling
    // actually installed.
    //
    // ALL OF IT, in one call, because the latch loads MD, KS and CKOS together:
    // a group assembled across two calls latches whatever the chip was holding
    // for the rest. The sync processor counts in ADC clocks, so a KS left on
    // the wrong crossover row makes every source measurement garbage.
    static uint8_t applySampleRate(uint16_t divider, uint32_t lineRateHz,
                                   uint8_t oversample);

    // DS-5725-3.2, front page: "Maximum analog sampling rate up to 162MSPS".
    static const uint32_t MaxSampleRateHz = 162000000u;

    // PLLAD_MD is twelve bits.
    static const uint16_t DividerMax = 4095;

    // What the ADC is actually asked to do, in samples per second.
    static uint32_t sampleRateHz(uint16_t divider, uint32_t lineRateHz,
                                 uint8_t oversample);

    static bool withinLimit(uint16_t divider, uint32_t lineRateHz,
                            uint8_t oversample);

    // The largest divider this line rate can carry AT THIS OVERSAMPLING, or 0
    // if none can -- which is a case the caller must handle rather than a value
    // it can use. A line rate of 0 (no lock) is also 0.
    static uint16_t maxDivider(uint32_t lineRateHz, uint8_t oversample);

    // The highest CKO whose crossover row still installs `oversample`. Above it
    // the row halves the ratio instead, so density and oversampling exchange at
    // a rate this fixes -- each row ceiling is half the one above it, and every
    // row therefore tops out at the same conversion rate.
    static uint32_t maxCkoFor(uint8_t oversample);

    // The oversampling the ADC is actually running, which is the request
    // reduced to whatever the crossover row can carry. **A CALLER HOLDING THE
    // REQUEST HOLDS A DIFFERENT NUMBER**: the engine asks for
    // OversampleAsClockAllows on every source, and the answer is 1, 2 or 4.
    static uint8_t oversampleInForce();

    // THE CLOCK THE CHIP IS RESET INTO, and the one every measurement starts
    // from. Every divider the engine installs is sized from a measured line
    // rate, and the source cannot be measured until the ADC is clocking -- so
    // the reset state has to be a clock that can be measured through, or
    // nothing is ever able to measure its way out of it.
    //
    // The pair is a MEASURED WORKING POINT rather than a nominal one: 2506 at
    // 15625 Hz is CKO 39.2 MHz, which the crossover table puts on post divider
    // 2 and so a VCO of 156.6 MHz on high gain -- the state the bench unit
    // locks in. A lower divider would be arithmetically tidier and lands the
    // VCO at 112 MHz on low gain, where nothing has measured whether the PLL
    // holds.
    //
    // The rate is the LOWEST line the part is expected to carry, so that every
    // faster source needs the PLL to divide rather than multiply: asked for a
    // frequency under its lock range it locks to every kth hsync, and
    // measureSourceLinesCorrected() recovers k up to LinesPerCountMax -- which
    // reaches 62.5 kHz. Above that the first count is not the source's and the
    // recovery ladder is what answers.
    // ../../../docs/investigations/the-reference-divider-was-the-bootstrap.md
    static const uint16_t BringUpDivider = 2506;
    static const uint32_t BringUpLineRateHz = 15625;

    // The divider alone, latched. NOT the crossover row -- applySampleRate() is
    // what writes the group, and a caller here is holding the rest itself.
    static void applyDivider(uint16_t divider);

    // The ADC PLL as the chip reset leaves it: no charge pump, the low VCO
    // gain, and the parked divider. The pulse on VCORST/PDZ that follows is the
    // caller's -- this is the state it latches. Leaves no divider in force,
    // because a PLL held in reset is running none.
    static void applyResetParameters();

    // Whether the PLL is running the divider in force, against the sync
    // processor's line total -- which counts in ADC clocks and so reports the
    // LATCHED divider, the one witness on the board that a write reached the
    // PLL. The count is passed in because measuring the source is not the ADC's
    // job. ../../../docs/tv5725-chip.md
    static bool dividerLatched(uint16_t lineSamples,
                               uint16_t tolerance = LatchedSamplesTolerance);

    // How far the sync processor's count may sit from the divider and still be
    // the same quantity. The register wobbles by a sample either way when
    // locked.
    static const uint16_t LatchedSamplesTolerance = 2;

    // The divider PLLAD_MD is holding. Reading the register back cannot answer
    // this: PLLAD_LAT loads it on a rising edge, so between a write and that
    // edge the register reports the new value while the PLL still runs the old
    // one. ../../../docs/investigations/hperiod-if-railing.md
    static uint16_t dividerInForce();

    // The ADC as pass-through wants it: no internal filtering. NOT the divider,
    // NOT the VCO gain and NOT the charge pump -- HdBypass::dividerFor() answers
    // the first against the line rate, and applySampleRate() owns the rest of
    // the group the latch loads.
    // The converter's reference trim, through its test registers. The same
    // six fields are wanted by the reset path, a source mode change and the
    // bypass switch, which each used to write the three bytes by hand.
    static void applyReferenceTrim();

    static void applyForBypassRgbhv();

    // The analog gain and offset, a triple at a time. Six registers that no
    // class owned: every caller wrote them one by one, and "put the stored
    // calibration back" was spelled out at five sites.
    static void applyGain(uint8_t r, uint8_t g, uint8_t b);
    static void applyOffset(uint8_t r, uint8_t g, uint8_t b);

    // Arm the path the gain is measured through.
    static void enableGainMeasurement(bool on);

private:
    // How many taps above the post divider a ratio asks for: one per doubling.
    static uint8_t stepsFor(uint8_t oversample);

    static uint8_t phaseSyncProcessor_;
    static uint8_t phaseAdc_;
    static uint8_t inputSel_;
    static uint8_t oversampleInForce_;
    static uint16_t dividerInForce_;
    static bool phaseFound_;

};

}  // namespace Tv5725

#endif  // TV5725_ADC_H
