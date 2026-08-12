#ifndef INPUT_SYNC_SEARCH_H_
#define INPUT_SYNC_SEARCH_H_

// Which sync search detection should run, given the saved input source and
// whether the sync processor reports a V-sync.
//
// detectAndSwitchToActiveInput() spelled this out twice, inline, and the two
// disagreed: the V-sync-present search accepted S_VGA and S_RGBs, the
// V-sync-absent one only S_RGBs. A unit saved as S_VGA on a source with H-sync
// and no V-sync matched neither, so detection fell out of the bottom of the
// function and livelocked -- ADC_INPUT_SEL alternating on a ~1 s beat, every
// boot ending in low power with the DAC down, measured 2026-08-13.
//
// V-sync absent is not exotic here: VSACT reads 0 on this bench source with a
// perfect picture, so the search that handles it has to be reachable from the
// input the user actually has saved.

#include <stdint.h>

class SyncSearch {
public:
    // The persisted SeleInputSource values, from OLEDMenuImplementation.h.
    // Repeated rather than included because that header pulls in Arduino and
    // would cost this class its host test. The sketch static_asserts them
    // against the originals, so the copy cannot drift silently.
    static const uint8_t SourceRgbs = 1;
    static const uint8_t SourceVga = 2;
    static const uint8_t SourceYuv = 3;

    enum Search {
        // The saved source is not one this path searches -- YPbPr has its own
        // branch, and 0 means nothing meaningful was ever saved.
        None,

        // The sync processor reports a V-sync: probe the field rate to decide
        // csync versus separate H/V, then sweep the med-res line count.
        VsyncPresent,

        // H-sync but no V-sync, which is what a composite-sync source looks
        // like here. Sweep the SOG slice level looking for a video mode.
        VsyncAbsent,
    };

    static Search searchFor(uint8_t inputSource, bool vsyncActive);
};

#endif  // INPUT_SYNC_SEARCH_H_
