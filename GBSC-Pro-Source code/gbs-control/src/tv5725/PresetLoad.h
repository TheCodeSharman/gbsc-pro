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

    // The sentinel writeProgramArrayNew() clears on every load.
    static const uint8_t NoValidMode = 15;

    // The standard that scaling RGBHV runs as.
    static const uint8_t ScalingRgbhvStandard = 3;

private:
    uint8_t videoStandardInput_;
    bool inputIsYpBpR_;
    bool enableScalingRgbhv_;
};

} // namespace Tv5725

#endif
