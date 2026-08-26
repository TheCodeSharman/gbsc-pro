#ifndef TV5725_SOURCE_STANDARD_H
#define TV5725_SOURCE_STANDARD_H

#include <stdint.h>

namespace Tv5725 {

// What Mode Detect's classification of the source implies, for the blocks a
// mode change configures.
//
// A standard is not a measurement -- the geometry engine measures the line
// count, the field rate and the line rate, and computes every register that
// follows from them. What is left here is the handful of settings no
// measurement supplies: the ADC's analog filter, the sampling density, and how
// the input formatter and the video processor handle a line of this shape.
//
// The classification and the colour space are the whole input. Both are held,
// neither is read back off the chip.
class SourceStandard {
public:
    SourceStandard(uint8_t videoStandardInput, bool inputIsYpBpR);

    // Every register the standard alone decides, in the order a mode change
    // writes them. Returns the oversampling installed, which the geometry
    // engine solves the sampling clock with and optimizePhaseSP() picks the ADC
    // phase from.
    //
    // postDivider is the one already in force. It belongs to the caller because
    // it is the PREVIOUS mode's, and a standard with a sampling density of its
    // own replaces it rather than reading it back.
    uint8_t apply(uint8_t postDivider) const;

private:
    // 1 and 2 are Mode Detect's interlaced SD, NTSC and PAL.
    bool isSd() const;

    // 3, 4, 8 and 9 share the input formatter's line handling and a sampling
    // density between interlaced SD's and the HD standards'.
    bool isProgressive() const;

    // 5, 6 and 7, which reach a load through the HD bypass switch.
    bool isHd() const;

    uint8_t applySd() const;
    uint8_t applyProgressive() const;
    void applyHd() const;

    uint8_t standard_;
    bool inputIsYpBpR_;
};

}  // namespace Tv5725

#endif  // TV5725_SOURCE_STANDARD_H
