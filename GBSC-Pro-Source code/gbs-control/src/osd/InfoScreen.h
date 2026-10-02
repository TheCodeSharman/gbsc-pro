#ifndef OSD_INFOSCREEN_H_
#define OSD_INFOSCREEN_H_

#include <stdint.h>

namespace Osd {

// What the board is doing, on the television's first two rows: what is going
// OUT on the first and what was measured coming IN on the second.
// docs/osd-menu.md
//
// EVERY NUMBER HERE IS HANDED IN. A draw must not become a second owner of a
// measured fact: the scan type in particular comes from
// SourceMeasurement::scanType(), which is the answer already reached, because
// measuring it feeds a steadiness run the acquisition layer is steering on.
class InfoScreen {
public:
    static const char Bar = 0x11;
    static const char Title = 0x60;
    static const char Body = 0x17;

    struct Report {
        // Pass-through is not a resolution: the source's own timing goes to the
        // encoder, so there is no raster of ours to name and the mode's active
        // region is zero. Named rather than reported as 0x0.
        bool bypass;
        uint16_t outputPx;
        uint16_t outputLines;
        uint8_t outputRateHz;

        // Which connector is selected, which is a setting rather than a
        // measurement.
        const char *input;

        // What was measured of the source. `present` false is a source the
        // engine is not holding, and then nothing below it means anything.
        bool present;
        uint16_t lines;
        bool interlaced;
        uint8_t fieldRateHz;
        uint32_t lineRateHz;
    };

    static void draw(const Report &report);
};

}  // namespace Osd

#endif  // OSD_INFOSCREEN_H_
