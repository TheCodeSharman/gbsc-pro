#include "SyncSearch.h"

SyncSearch::Search SyncSearch::searchFor(uint8_t inputSource, bool vsyncActive)
{
    if (inputSource != SourceRgbs && inputSource != SourceVga) {
        return None;
    }
    // Both RGB sources reach both searches. The asymmetry this replaces --
    // V-sync-present accepting S_VGA and S_RGBs, V-sync-absent accepting only
    // S_RGBs -- left S_VGA with no search to run and detection livelocked.
    return vsyncActive ? VsyncPresent : VsyncAbsent;
}

bool SyncSearch::shouldSweepSyncProcessor(uint8_t modeReadout,
                                          bool sourceIsCounted)
{
    return modeReadout == 0 && !sourceIsCounted;
}
