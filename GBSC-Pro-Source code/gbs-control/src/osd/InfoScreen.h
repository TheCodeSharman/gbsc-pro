#ifndef OSD_INFOSCREEN_H_
#define OSD_INFOSCREEN_H_

#include <stdint.h>

namespace Osd {

// What the board is doing, on the television's first two rows: what is going
// OUT on the first and what the engine holds about the source on the second.
// docs/osd-menu.md
//
// IT MEASURES NOTHING. Every number is handed in and every one of them is state
// the engine already holds -- the output mode, the source key and the scan type
// it last reached. A screen is drawn from loop(), so a measurement here is paid
// for at the redraw cadence: reading the output frame rate off the test bus
// TIMES PULSES, and in pass-through there is no pulse to time, which took a
// register read from 0.03 s to 5.2 s and starved OTA.
//
// The rate is the source's, which on the scaling path is also the output's
// because the raster is solved FOR it -- so there are not two numbers here.
// Pass-through solves no raster, and there the source's is the only one.
class InfoScreen {
public:
    static const char Bar = 0x11;
    static const char Title = 0x60;
    static const char Body = 0x17;

    // What the connector carries, which decides what the second row calls it.
    enum Kind { NoInput, Rgb, Component, SVideo, Composite };

    struct Report {
        // Pass-through is not a resolution: the source's own timing goes to the
        // encoder, so there is no raster of ours to name.
        bool bypass;
        uint16_t outputPx;
        uint16_t outputLines;

        // The selected connector, as a person reads it.
        const char *input;

        Kind kind;

        // RGB only: whether the source brings its own H and V, which is the
        // engine's held answer rather than a status bit's.
        bool separateSync;

        // The source the engine reports, which is the framed key where a solve
        // framed one and the measurement where none did. Nothing below
        // `present` means anything when it is false.
        bool present;
        uint16_t lines;
        bool interlaced;
        uint8_t rateHz;

        // The line rate, which is what tells two modes of the same height and
        // field rate apart. On the third row, which the chain left empty.
        uint32_t lineRateHz;
    };

    static void draw(const Report &report);
};

}  // namespace Osd

#endif  // OSD_INFOSCREEN_H_
