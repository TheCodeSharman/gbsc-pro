#ifndef VIDEOSOURCE_VIDEO_SOURCE_SELECTOR_H_
#define VIDEOSOURCE_VIDEO_SOURCE_SELECTOR_H_

// Putting a video source in force: the HC32's half of the mux and the TV5725's,
// in the one order, for every caller.
//
// **THE SEQUENCE IS NOT THE REGISTERS.** A selection also resets the sync
// processor and installs a reference sampling clock, because the first
// measurement of the arriving source is taken through whatever divider the chip
// was left holding otherwise.
//
// THE RESET COMES FIRST, because it leaves the capture counter's origin at 0 and
// the install is what writes it.
// ../../../../docs/investigations/the-retime-stop-is-the-counters-origin.md
//
// VideoSourceSelection is the table; this is the acting on it. What it cannot
// own -- the UART, the register writes, the preferences -- is injected, which is
// also what keeps it host-testable.
// ../../../../docs/investigations/the-green-ypbpr-boot-is-outside-the-register-file.md

#include <stdint.h>

#include "VideoSourceSelection.h"

class VideoSourceSelector {
public:
    struct Actions {
        void (*sendFrame)(uint8_t frame);
        void (*selectionChanged)();
        void (*resetSyncProcessor)();
        void (*installReferenceClock)();
        void (*applyRegisters)(const VideoSourceSelection::Settings &settings);
        void (*persist)();
    };

    struct State {
        uint8_t *legacySource;
        uint8_t *brightnessSet;
        bool *sourceDisconnected;
        bool *inLowPowerMode;
    };

    VideoSourceSelector(const Actions &actions, const State &state);

    // Somebody chose this one, so it is written back.
    void select(VideoSourceSelection::Id id);

    // The settings file already names this one, so writing it back would only
    // rewrite what was just read.
    void restore(VideoSourceSelection::Id id);

private:
    void apply(VideoSourceSelection::Id id, bool persist);

    Actions actions_;
    State state_;
};

#endif  // VIDEOSOURCE_VIDEO_SOURCE_SELECTOR_H_
