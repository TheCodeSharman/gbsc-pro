// Host-compiled tests for src/videosource/VideoSourceSelection.h -- `make -C test input-source`.
//
// The six inputs the OLED offers, as one table: the frame byte the AV module is
// sent, the sync routing the TV5725 needs, and what the preferences file keeps.
//
// **THE FRAME'S LOW NIBBLE IS THE FAULT THIS TABLE EXISTS TO PREVENT.** VGA is
// the only input whose frame carries one, and it is what raises the HC32's
// asw_01 -- the schematic's HS_IN choice between the dedicated HSync pin and
// sync-on-green. Sent as 0x60 instead of 0x61 the mux stays on sync-on-green, a
// VGA source with separate sync has nothing for the sync processor to count, and
// SP_VTOTAL reads 0 while HPERIOD_IF measures the line perfectly. Measured on
// the bench, on every boot, until the byte was found.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "../GBSC-Pro-Source code/gbs-control/src/videosource/VideoSourceSelection.h"

TEST_CASE("every input names the frame the AV module is sent")
{
    // 'S' selects the input and the high nibble names it. RD of the frame
    // format is in CLAUDE.md; these are the bytes OLEDMenuImplementation.cpp
    // declares.
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Rgbs).frame == 0x40);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::RgsB).frame == 0x50);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Ypbpr).frame == 0x70);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::SVideo).frame == 0x10);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Composite).frame == 0x20);

    SUBCASE("and VGA is the one that carries a low nibble") {
        // 0x60 reaches the HC32 as a VGA selection that leaves HS_IN on
        // sync-on-green. The nibble is not decoration.
        CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Vga).frame == 0x61);
    }

    SUBCASE("which is why no other input has one") {
        const VideoSourceSelection::Id others[] = {
            VideoSourceSelection::Rgbs, VideoSourceSelection::RgsB, VideoSourceSelection::Ypbpr,
            VideoSourceSelection::SVideo, VideoSourceSelection::Composite};
        for (VideoSourceSelection::Id id : others)
            CHECK((VideoSourceSelection::settingsFor(id).frame & 0x0F) == 0);
    }
}

TEST_CASE("VGA is the only input that takes sync from the dedicated pin")
{
    // SP_EXT_SYNC_SEL 0 selects external H/V; everything else counts
    // composite or sync-on-green. This is the register half of the same
    // decision the frame's nibble makes at the HC32.
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Vga).extSyncSel == 0);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Rgbs).extSyncSel == 1);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::RgsB).extSyncSel == 1);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::SVideo).extSyncSel == 1);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Composite).extSyncSel == 1);
}

TEST_CASE("the ADC mux and its sync-on-green follow the input")
{
    SUBCASE("the RGB inputs take ADC input 1 with sync-on-green enabled") {
        for (VideoSourceSelection::Id id : {VideoSourceSelection::Rgbs, VideoSourceSelection::RgsB,
                                   VideoSourceSelection::Vga}) {
            CHECK(VideoSourceSelection::settingsFor(id).adcInputSel == 1);
            CHECK(VideoSourceSelection::settingsFor(id).adcSogEn == 1);
        }
    }

    SUBCASE("and the decoded inputs take input 0 with it off") {
        for (VideoSourceSelection::Id id : {VideoSourceSelection::SVideo, VideoSourceSelection::Composite}) {
            CHECK(VideoSourceSelection::settingsFor(id).adcInputSel == 0);
            CHECK(VideoSourceSelection::settingsFor(id).adcSogEn == 0);
        }
    }
}

TEST_CASE("every input points the ADC mux at itself")
{
    // YPbPr wrote none of the three and reached its input only because
    // detection swept the mux until something had sync. With the sweep
    // answering to the user's choice instead, a selection that does not move
    // the mux cannot arrive at all -- so selecting YPbPr left the ADC on the
    // RGB pins and nothing locked.
    //
    // Its values were already here and already match S-Video and composite,
    // which share the connector and write all three.
    for (VideoSourceSelection::Id id : {VideoSourceSelection::Rgbs, VideoSourceSelection::RgsB,
                               VideoSourceSelection::Vga, VideoSourceSelection::Ypbpr,
                               VideoSourceSelection::SVideo, VideoSourceSelection::Composite})
        CHECK(VideoSourceSelection::settingsFor(id).writesAdc);
}

TEST_CASE("the decoded inputs take the unit out of low power")
{
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::SVideo).clearsLowPower);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Composite).clearsLowPower);
    CHECK_FALSE(VideoSourceSelection::settingsFor(VideoSourceSelection::Vga).clearsLowPower);
    CHECK_FALSE(VideoSourceSelection::settingsFor(VideoSourceSelection::Rgbs).clearsLowPower);
}

TEST_CASE("the six inputs persist as three legacy values, which is why Info exists")
{
    // SeleInputSource cannot tell RGBs from RGsB, nor YPbPr from S-Video from
    // composite. The id here IS the Info byte, which can -- so a restore keyed
    // on the legacy value can only ever reconstruct VGA.
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Rgbs).legacySource == 1);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::RgsB).legacySource == 1);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Vga).legacySource == 2);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Ypbpr).legacySource == 3);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::SVideo).legacySource == 3);
    CHECK(VideoSourceSelection::settingsFor(VideoSourceSelection::Composite).legacySource == 3);
}

TEST_CASE("the RGB inputs are sync variants of one connector, not three inputs")
{
    // Which is what makes a search legitimate after an explicit choice: the
    // cable may carry composite sync where separate H/V was selected, on the
    // same pins. Searching those is fair; searching the other port's branches
    // is the 6 s timeout that has nothing to do with the input chosen.
    CHECK(VideoSourceSelection::sharesPort(VideoSourceSelection::Vga, VideoSourceSelection::Rgbs));
    CHECK(VideoSourceSelection::sharesPort(VideoSourceSelection::Vga, VideoSourceSelection::RgsB));
    CHECK(VideoSourceSelection::sharesPort(VideoSourceSelection::SVideo, VideoSourceSelection::Composite));

    SUBCASE("and the two ports are distinct") {
        CHECK_FALSE(VideoSourceSelection::sharesPort(VideoSourceSelection::Vga, VideoSourceSelection::SVideo));
        CHECK_FALSE(VideoSourceSelection::sharesPort(VideoSourceSelection::Rgbs, VideoSourceSelection::Composite));
    }

    SUBCASE("and nothing chosen shares a port with anything") {
        CHECK_FALSE(VideoSourceSelection::sharesPort(VideoSourceSelection::None, VideoSourceSelection::None));
        CHECK_FALSE(VideoSourceSelection::sharesPort(VideoSourceSelection::None, VideoSourceSelection::Vga));
    }
}

TEST_CASE("an input can be named, so a request can carry one")
{
    CHECK(VideoSourceSelection::fromName("vga") == VideoSourceSelection::Vga);
    CHECK(VideoSourceSelection::fromName("rgbs") == VideoSourceSelection::Rgbs);
    CHECK(VideoSourceSelection::fromName("rgsb") == VideoSourceSelection::RgsB);
    CHECK(VideoSourceSelection::fromName("ypbpr") == VideoSourceSelection::Ypbpr);
    CHECK(VideoSourceSelection::fromName("sv") == VideoSourceSelection::SVideo);
    CHECK(VideoSourceSelection::fromName("av") == VideoSourceSelection::Composite);

    SUBCASE("and anything else is refused rather than guessed at") {
        CHECK(VideoSourceSelection::fromName("") == VideoSourceSelection::None);
        CHECK(VideoSourceSelection::fromName("VGA ") == VideoSourceSelection::None);
        CHECK(VideoSourceSelection::fromName("component") == VideoSourceSelection::None);
        CHECK(VideoSourceSelection::fromName(0) == VideoSourceSelection::None);
    }

    SUBCASE("and every name round-trips") {
        for (uint8_t id = VideoSourceSelection::Rgbs; id <= VideoSourceSelection::Composite; ++id) {
            const char *n = VideoSourceSelection::name((VideoSourceSelection::Id)id);
            CHECK(VideoSourceSelection::fromName(n) == (VideoSourceSelection::Id)id);
        }
    }
}

TEST_CASE("a stored id outside the six is nothing chosen")
{
    // Which is the whole point of the distinction: nothing chosen sweeps, and
    // a choice is obeyed. A value nobody wrote must not read as a choice.
    CHECK_FALSE(VideoSourceSelection::chosen(0));
    CHECK_FALSE(VideoSourceSelection::chosen(7));
    CHECK_FALSE(VideoSourceSelection::chosen(255));
    for (uint8_t id = VideoSourceSelection::Rgbs; id <= VideoSourceSelection::Composite; ++id)
        CHECK(VideoSourceSelection::chosen(id));
}

TEST_CASE("the boot restore reconstructs the input from the Info byte")
{
    // `Info` carries all six; `SeleInputSource` carries three. Keyed on the
    // legacy byte the restore cannot tell RGsB from RGBs, nor S-Video from
    // composite from YPbPr -- and it sent VGA the frame RGBs asks for, which is
    // where the missing nibble came from.
    CHECK(VideoSourceSelection::fromStored(1) == VideoSourceSelection::Rgbs);
    CHECK(VideoSourceSelection::fromStored(2) == VideoSourceSelection::RgsB);
    CHECK(VideoSourceSelection::fromStored(3) == VideoSourceSelection::Vga);
    CHECK(VideoSourceSelection::fromStored(4) == VideoSourceSelection::Ypbpr);
    CHECK(VideoSourceSelection::fromStored(5) == VideoSourceSelection::SVideo);
    CHECK(VideoSourceSelection::fromStored(6) == VideoSourceSelection::Composite);

    SUBCASE("and anything else is nothing chosen, which is what sweeps") {
        CHECK(VideoSourceSelection::fromStored(0) == VideoSourceSelection::None);
        CHECK(VideoSourceSelection::fromStored(7) == VideoSourceSelection::None);
        CHECK(VideoSourceSelection::fromStored(255) == VideoSourceSelection::None);
    }
}

// Only VGA can be either. Every other input arrives on composite sync or
// sync-on-green -- extSyncSel 1 -- so the type is settled by the connector and
// measuring it can only get it wrong. Measured: probing YPbPr answers "own
// vsync", which puts a sync-on-luma source on the separate-sync path and it
// never locks. docs/sync-type-selection.md
TEST_CASE("only VGA leaves the sync type open to measurement")
{
    CHECK(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::Vga));

    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::Ypbpr));
    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::Rgbs));
    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::RgsB));
    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::SVideo));
    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::Composite));

    // Nothing chosen is not an invitation to probe.
    CHECK_FALSE(VideoSourceSelection::syncTypeMustBeMeasured(VideoSourceSelection::None));
}
