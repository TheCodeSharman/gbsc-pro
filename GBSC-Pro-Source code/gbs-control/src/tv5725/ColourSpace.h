#ifndef TV5725_COLOUR_SPACE_H
#define TV5725_COLOUR_SPACE_H

#include "Adc.h"
#include "ColourBalance.h"
#include "InputFormatter.h"
#include "VideoProcessor.h"

namespace Tv5725 {

// What the colour space a source arrives in implies: the ADC's R-Y select, the
// two matrix bypasses, and the chroma gains.
//
// A component OUTPUT is a modifier on top of whichever of those the source
// arrived in rather than a third space, so it re-takes the chroma gains and the
// balance's rest and leaves the rest of the input's choice standing.
//
// The offsets and the luma gain are ColourBalance's, which is handed in: they
// carry the user's picture colour as well as the colour space's own rest, and
// one owner of them is what stops a load putting the balance back to neutral.
class ColourSpace {
public:
    typedef UReg<0x05, 0x1F, 2, 1> DEC_MATRIX_BYPS;

    // The ADC gain each colour space wants, measured on the emitted frame.
    // docs/investigations/the-adc-gain-is-the-colour-spaces.md
    static const uint8_t ComponentGain = 0x33;
    static const uint8_t RgbGain = 0x7B;

    // The chroma cosine gains, per space. Both channels take the same value on
    // a component output and differ on an input space.
    static const uint8_t UCosGain = 0x1C;
    static const uint8_t VCosGain = 0x29;
    static const uint8_t ComponentOutputCosGain = 0x19;

    static void applyYuv(ColourBalance &balance);
    static void applyRgb(ColourBalance &balance);
    static void applyComponentOutput(ColourBalance &balance);
};

}  // namespace Tv5725

#endif  // TV5725_COLOUR_SPACE_H
