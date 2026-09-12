#ifndef TV5725_PRESET_LOAD_H
#define TV5725_PRESET_LOAD_H

#include <stdint.h>

namespace Tv5725 {

// The mode state a preset load decides, separated from the bytes it writes.
//
// writeProgramArrayNew() does two unrelated jobs: it copies 432 bytes out of a
// preset table, and it settles mode state the table has no say in. The second
// has to outlive the first, or it goes when the tables do.
//
// Plain integers, no registers and no rto->, so it host-compiles.
//
// Two values of videoStandardInput matter, not one: the sentinel is cleared
// before the table is written because the byte loop reads the value while it
// runs, and scaling RGBHV moves it again afterwards.
class PresetLoad {
public:
    // adcInputSel is GBS::ADC_INPUT_SEL, the TV5725's own input mux.
    // validForScalingRgbhv is rto->isValidForScalingRGBHV; preferScalingRgbhv
    // is the user option.
    PresetLoad(uint8_t videoStandardInput, uint8_t adcInputSel,
               bool preferScalingRgbhv, bool validForScalingRgbhv);

    // What the table write should see: 15, the "no valid mode" sentinel,
    // normalised to 0.
    uint8_t videoStandardInput() const;

    // What to leave behind once the table is written.
    uint8_t videoStandardInputAfterLoad() const;

    // ADC mux 0 is the YPbPr input. Only half the input path -- whether the
    // HC32F460 connected anything to it is ASW_01..04 and unreadable.
    bool inputIsYpBpR() const;

    bool enableScalingRgbhv() const;

    // Which standard's preset a scaled RGBHV source of this many lines wants,
    // against the count the loaded preset was chosen for. 0 keeps that preset.
    // The buckets are measured, not derived: 280 and 380 lines.
    static uint8_t rgbhvPresetStandard(uint16_t sourceLines);

    // The same question for a source nothing has been loaded for yet, which is
    // the one place the field rate is consulted -- and only above 380 lines.
    //
    // **THIS IS A MEASUREMENT, NOT A CLASSIFICATION.** It answers off a line
    // count and a field rate, and the standard byte is only how the answer
    // reaches applyPresets(). The caller sets the byte to what this returns,
    // loads, and sets it straight back to 14, which is why one number carrying
    // both the source and the output is the thing step 10 removes.
    // docs/investigations/two-spellings-of-scaling-rgbhv.md
    static uint8_t rgbhvStandardFor(uint16_t sourceLines, float fieldRateHz);

    // The field-rate window the fourth preset claims, above 380 lines.
    static const uint16_t TallSourceLines = 380;
    static const uint16_t ShortSourceLines = 280;

    // The values of rto->videoStandardInput, named so the facets one byte
    // carries are visible: a source FORMAT in 1..9, a video PATH in 13..15, and
    // nothing-known at 0. Separating them is what retires the byte.
    //
    // **15 DOES DOUBLE DUTY.** It is the sentinel a load clears, and it is what
    // rgbhvBypass() tests -- bypass being the consequence of having no valid
    // scaled mode rather than a second meaning invented for the value.
    static const uint8_t NoValidMode = 15;

    // The byte that names scaling RGBHV, which is what scalingRgbhv() tests.
    static const uint8_t ScalingRgbhv = 14;

    // YPbPr passed through rather than scaled.
    static const uint8_t HdBypassStandard = 13;

    // Nothing has been classified.
    static const uint8_t NoStandard = 0;

    // The SD formats, which are the four Mode Detect names in STATUS_00 and
    // carry that block's vocabulary. **INT AND PRG NAME THE STANDARD, NOT THIS
    // SOURCE'S SCAN**: the bits are vertical-period buckets, and a 15 kHz
    // progressive source lands in an interlaced standard's bucket.
    // docs/video-source-acquisition.md
    static const uint8_t NtscInt = 1;                  // STATUS_00 bit 3
    static const uint8_t PalInt = 2;                   // bit 5
    static const uint8_t NtscPrg = 3;                  // bit 4
    static const uint8_t PalPrg = 4;                   // bit 6
    static const uint8_t SdFirst = NtscInt;
    static const uint8_t SdLast = PalPrg;

    // Everything above SD. The first three run the ADC at their own
    // oversampling.
    static const uint8_t HdFirst = 5;
    static const uint8_t HdOwnOversampleLast = 7;

    // The lowest value that names a PATH rather than a source format.
    static const uint8_t PathFirst = 13;

    // ...and the lowest that names an RGBHV one, which is what sourceIsRgbhv()
    // tests.
    static const uint8_t RgbhvFirst = 14;

    // Whether the output in force is scaling RGBHV, which the load above
    // decides and later steps of the same load ask about. State rather than a
    // chip register: it lived in s1_2c, an address RD-5725-1.1 does not
    // document, and every reader had to be ordered against the load that
    // cleared it.
    static bool scalingRgbhvInForce();

    // A scaling RGBHV preset is loaded, chosen for a source of this many lines.
    // The count travels with the flag because they are one fact: comparing a
    // fresh count against one the loaded preset was not chosen for reloads a
    // preset the source never left.
    static void rememberScalingRgbhv(uint16_t sourceLines);

    // For a load that enables scaling RGBHV without establishing which source
    // it is for. No count can have crossed a bucket away from it.
    static const uint16_t SourceLinesUnknown = 0;

    // A load is starting, and what the last one enabled says nothing about it.
    static void forgetScalingRgbhv();

private:
    uint8_t videoStandardInput_;
    bool inputIsYpBpR_;
    bool enableScalingRgbhv_;
};

} // namespace Tv5725

#endif
