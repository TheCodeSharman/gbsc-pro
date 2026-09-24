#ifndef TV5725_OUTPUT_MAPPING_H_
#define TV5725_OUTPUT_MAPPING_H_

// How one axis of a capture is mapped onto the output raster: the scale that
// does the mapping, how big the picture came out, and the two blanking windows
// bounding where it landed. Every VDS output register for that axis.
//
// What OutputWindow::horizontal() and ::vertical() return, the way a
// BlankingTiming is what CaptureWindow's return. Filled by the one class
// entitled to compute it.
#include <stdint.h>

#include "BlankingTiming.h"
#include "Scale.h"

namespace Tv5725 {

class OutputMapping {
public:
    OutputMapping();

    // VDS_?SCALE, as fitToRaster() chose it for this axis.
    Scale scale() const;

    float produced() const;

    // VDS_?B_ST / VDS_?B_SP -- the window the playback stage fetches through.
    const BlankingTiming &memory() const;

    // VDS_DIS_?B_ST / VDS_DIS_?B_SP -- the aperture that reaches the encoder.
    const BlankingTiming &display() const;

    bool usable() const;

private:
    friend class OutputWindow;   // the only thing that may fill one in
    Scale scale_;
    float produced_;
    BlankingTiming memory_, display_;
};

}  // namespace Tv5725

#endif  // TV5725_OUTPUT_MAPPING_H_
