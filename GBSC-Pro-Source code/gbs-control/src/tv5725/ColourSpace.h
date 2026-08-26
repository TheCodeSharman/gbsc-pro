#ifndef TV5725_COLOUR_SPACE_H
#define TV5725_COLOUR_SPACE_H

#include "Adc.h"
#include "InputFormatter.h"
#include "VideoProcessor.h"

namespace Tv5725 {

// What the colour space a source arrives in implies: the ADC's R-Y select, the
// two matrix bypasses, and the video processor's gains and offsets.
//
// The offsets are the input to the R/G/B round trip the sketch keeps, so the
// caller reads them back after any component mixing rather than assuming what
// was written here survived.
class ColourSpace {
public:
    typedef UReg<0x05, 0x1F, 2, 1> DEC_MATRIX_BYPS;

    static void applyYuv();
    static void applyRgb();
};

}  // namespace Tv5725

#endif  // TV5725_COLOUR_SPACE_H
