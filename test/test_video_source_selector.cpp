// Host-compiled tests for src/videosource/VideoSourceSelector.h -- `make -C test
// video-source-selector`.
//
// **SELECTING AN INPUT IS ONE SEQUENCE AND EVERY CALLER OWES ALL OF IT.** The
// boot used to apply the registers and send the frame and stop there, where the
// OLED, the IR handler and /input went on to install a reference sampling clock,
// tell the input formatter what scan that implies, and reset the sync processor.
// A unit that came up on ypbpr therefore took its first measurement of the
// arriving source through whatever divider the chip was left holding, and the
// early samples read 270 lines x 121.42 Hz against a source at 60.
//
// The order is what is asserted, not merely that each step ran: the reference
// clock has to be in force before anything measures through it, and the frame
// has to reach the HC32 before the registers describe what it routed.
// ../docs/investigations/the-green-ypbpr-boot-is-outside-the-register-file.md

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelector.h"

namespace {

std::vector<std::string> steps;
uint8_t frameSent;
VideoSourceSelection::Settings registersApplied;

uint8_t legacySource;
uint8_t brightnessSet;
bool sourceDisconnected;
bool inLowPowerMode;

void sendFrame(uint8_t frame) { steps.push_back("frame"); frameSent = frame; }
void selectionChanged() { steps.push_back("selectionChanged"); }
void installReferenceSamplingClock() { steps.push_back("referenceClock"); }
void applyBringUpScan() { steps.push_back("scan"); }
void resetSyncProcessor() { steps.push_back("syncReset"); }
void applyRegisters(const VideoSourceSelection::Settings &s)
{
    steps.push_back("registers");
    registersApplied = s;
}
void persist() { steps.push_back("persist"); }

VideoSourceSelector make()
{
    steps.clear();
    frameSent = 0;
    legacySource = 0;
    brightnessSet = 0;
    sourceDisconnected = false;
    inLowPowerMode = true;

    VideoSourceSelector::Actions actions;
    actions.sendFrame = sendFrame;
    actions.selectionChanged = selectionChanged;
    actions.installReferenceSamplingClock = installReferenceSamplingClock;
    actions.applyBringUpScan = applyBringUpScan;
    actions.resetSyncProcessor = resetSyncProcessor;
    actions.applyRegisters = applyRegisters;
    actions.persist = persist;

    VideoSourceSelector::State state;
    state.legacySource = &legacySource;
    state.brightnessSet = &brightnessSet;
    state.sourceDisconnected = &sourceDisconnected;
    state.inLowPowerMode = &inLowPowerMode;

    return VideoSourceSelector(actions, state);
}

const std::vector<std::string> Sequence = {
    "frame", "selectionChanged", "referenceClock", "scan", "syncReset", "registers"};

}  // namespace

TEST_CASE("choosing an input runs the whole sequence, in order")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Ypbpr);

    std::vector<std::string> expected = Sequence;
    expected.push_back("persist");
    CHECK(steps == expected);
}

TEST_CASE("restoring the stored input runs the same sequence")
{
    VideoSourceSelector selector = make();

    selector.restore(VideoSourceSelection::Ypbpr);

    CHECK(steps == Sequence);
}

TEST_CASE("a restore does not write back the selection it was given")
{
    VideoSourceSelector selector = make();

    selector.restore(VideoSourceSelection::Ypbpr);

    for (size_t i = 0; i < steps.size(); ++i)
        CHECK(steps[i] != "persist");
}

TEST_CASE("the frame sent is the one the selection names")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Vga);

    CHECK(frameSent == VideoSourceSelection::settingsFor(VideoSourceSelection::Vga).frame);
}

TEST_CASE("the registers applied are the ones the selection names")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Ypbpr);

    const VideoSourceSelection::Settings wanted =
        VideoSourceSelection::settingsFor(VideoSourceSelection::Ypbpr);
    CHECK(registersApplied.adcInputSel == wanted.adcInputSel);
    CHECK(registersApplied.extSyncSel == wanted.extSyncSel);
}

TEST_CASE("the legacy source and brightness follow the selection")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Ypbpr);

    const VideoSourceSelection::Settings wanted =
        VideoSourceSelection::settingsFor(VideoSourceSelection::Ypbpr);
    CHECK(legacySource == wanted.legacySource);
    CHECK(brightnessSet == wanted.brightnessSet);
}

TEST_CASE("the source counts as disconnected until detection says otherwise")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Ypbpr);

    CHECK(sourceDisconnected);
}

TEST_CASE("an input that clears low power clears it")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::SVideo);

    CHECK_FALSE(inLowPowerMode);
}

TEST_CASE("an input that does not clear low power leaves it alone")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::Ypbpr);

    CHECK(inLowPowerMode);
}

TEST_CASE("nothing chosen selects nothing")
{
    VideoSourceSelector selector = make();

    selector.select(VideoSourceSelection::None);

    CHECK(steps.empty());
}

TEST_CASE("the selection is recorded, so every reader sees the same answer")
{
    VideoSourceSelector selector = make();
    VideoSourceSelection::select(VideoSourceSelection::Vga);

    selector.restore(VideoSourceSelection::Ypbpr);

    CHECK(VideoSourceSelection::selected() == VideoSourceSelection::Ypbpr);
}
