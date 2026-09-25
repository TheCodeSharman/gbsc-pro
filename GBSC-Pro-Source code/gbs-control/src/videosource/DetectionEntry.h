#ifndef VIDEOSOURCE_DETECTION_ENTRY_H_
#define VIDEOSOURCE_DETECTION_ENTRY_H_

// What a detection pass should do next while it waits for a source to appear
// on the input just selected: keep waiting, act on what has arrived, or give up
// and let the caller try again.
//
// **THE INSTRUMENT IS THE CALLER'S AND IT MATTERS.**
// STATUS_SYNC_PROC_HSACT rails in both directions -- 0 for a whole window on a
// source that then acquired, 1 at every separator level while nothing was
// counted -- so what is passed in decides whether this answers anything.
// Tv5725::SyncProcessor::signalPresent() counts transitions on the test bus and
// does not rail. ../../../../docs/known-issues.md
//
// The blocking stays with the caller, which owns delay() and the WiFi stack.

#include <stdint.h>

class DetectionEntry {
public:
    // How long absence is waited out before a pass concludes. Sized from the
    // gap between a selection and sync, which is not one quantity: the input
    // route is queued to loop() and the mux is the HC32's over a UART with no
    // readback. The caller retrying is what covers the rest, so this does not
    // have to be long enough for the worst case.
    static const uint16_t WindowMs = 450;

    enum Step {
        Wait,
        Act,
        GiveUp,
    };

    static Step stepAt(bool signalPresent, uint32_t waitedMs);
};

#endif  // VIDEOSOURCE_DETECTION_ENTRY_H_
