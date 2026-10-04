#include "VideoSourceSelector.h"

VideoSourceSelector::VideoSourceSelector(const Actions &actions, const State &state)
    : actions_(actions), state_(state)
{
}

void VideoSourceSelector::select(VideoSourceSelection::Id id) { apply(id, true); }

void VideoSourceSelector::restore(VideoSourceSelection::Id id) { apply(id, false); }

void VideoSourceSelector::apply(VideoSourceSelection::Id id, bool persist)
{
    if (id == VideoSourceSelection::None)
        return;

    const VideoSourceSelection::Settings settings = VideoSourceSelection::settingsFor(id);

    *state_.legacySource = settings.legacySource;
    VideoSourceSelection::select(id);

    // The frame first: the registers below describe a route the HC32 has to
    // have made, and the reference clock is measured through it.
    actions_.sendFrame(settings.frame);

    actions_.selectionChanged();
    actions_.installReferenceSamplingClock();
    actions_.applyBringUpScan();
    actions_.resetSyncProcessor();
    actions_.applyRegisters(settings);

    *state_.brightnessSet = settings.brightnessSet;
    *state_.sourceDisconnected = true;
    if (settings.clearsLowPower)
        *state_.inLowPowerMode = false;

    if (persist)
        actions_.persist();
}
