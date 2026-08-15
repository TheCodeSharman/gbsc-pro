#ifndef TV5725_INPUT_FORMATTER_H
#define TV5725_INPUT_FORMATTER_H

#include <stdint.h>

namespace Tv5725 {

// The input formatter: what the chip does to a captured line before the scaler
// sees it.
//
// The static half: IF_SEL24BIT = 1 takes the 24-bit input path, IF_SEL_HSCALE =
// 1 puts the horizontal scaler in circuit, IF_SEL_ADC_SYNC = 1 takes sync from
// the ADC rather than the digital port, and the piecewise H-sync rate correction
// is off. What moves per mode -- IF_HB_*, IF_HBIN_SP, IF_LINE_SP -- is the
// engine's, which computes the capture window rather than transcribing it.
//
// IF_LD_ST shares s1_0c with IF_LD_RAM_BYPS (bit 0) and IF_INI_ST (bits 7-5),
// both written by doPostPresetLoadSteps(). Three owners in one byte is safe only
// because every access is read-modify-write; test_input_formatter.cpp asserts
// the neighbours survive rather than assuming it.
class InputFormatter {
public:
    // Every static register of this subsystem, in address order. Called from
    // the bring-up where a preset table would have been loaded.
    static void init();
};

}  // namespace Tv5725

#endif
