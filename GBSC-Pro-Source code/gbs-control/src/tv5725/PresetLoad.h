#ifndef TV5725_PRESET_LOAD_H
#define TV5725_PRESET_LOAD_H

#include <stdint.h>

namespace Tv5725 {

// The standard byte's vocabulary, and one flag saying whether the output in
// force is scaling RGBHV.
//
// **NOTHING HERE READS AN OUTPUT RESOLUTION OR A FRAMING, WHICH THE NAME
// SUGGESTS.** OutputChoice answers the resolution. What is left is the byte
// itself spelled as constants, so the class goes when the byte does.
// docs/video-source-acquisition.md
//
// Plain integers, no registers and no rto->, so it host-compiles.
class PresetLoad {
public:
    // The values of rto->videoStandardInput, named so the facets one byte
    // carries are visible: a source FORMAT in 1..9, a video PATH in 13..14, and
    // nothing-known at 0. Separating them is what retires the byte.

    // An RGBHV source, which is what sourceIsRgbhv() tests. Mode Detect names
    // nothing for it, and detection establishes it before any output has been
    // chosen. **WHAT THE SOURCE GETS IS NOT HERE**: Tv5725::RgbhvOutput says
    // whether a scaled mode is established, and that is what scalingRgbhv() and
    // rgbhvBypass() read.
    static const uint8_t Rgbhv = 14;

    // What applyPresets() is ASKED for when an RGBHV source should be passed
    // through. A dispatch value and never held -- the byte holds Rgbhv with the
    // output bypassed -- so getVideoMode() reconstructs it from the output
    // rather than reading it back.
    static const uint8_t BypassRgbhv = 15;

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

    // Whether the output in force is scaling RGBHV. State rather than a chip
    // register: it lived in s1_2c, an address RD-5725-1.1 does not document,
    // and every reader had to be ordered against the load that cleared it.
    static bool scalingRgbhvInForce();

    // A scaling RGBHV preset is loaded.
    static void rememberScalingRgbhv();

    // A load is starting, and what the last one enabled says nothing about it.
    static void forgetScalingRgbhv();
};

} // namespace Tv5725

#endif
