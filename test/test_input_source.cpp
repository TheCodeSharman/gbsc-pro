// Host-compiled tests for src/input/InputSource.h -- `make -C test input-source`.
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

#include "../GBSC-Pro-Source code/gbs-control/src/input/InputSource.h"

TEST_CASE("every input names the frame the AV module is sent")
{
    // 'S' selects the input and the high nibble names it. RD of the frame
    // format is in CLAUDE.md; these are the bytes OLEDMenuImplementation.cpp
    // declares.
    CHECK(InputSource::settingsFor(InputSource::Rgbs).frame == 0x40);
    CHECK(InputSource::settingsFor(InputSource::RgsB).frame == 0x50);
    CHECK(InputSource::settingsFor(InputSource::Ypbpr).frame == 0x70);
    CHECK(InputSource::settingsFor(InputSource::SVideo).frame == 0x10);
    CHECK(InputSource::settingsFor(InputSource::Composite).frame == 0x20);

    SUBCASE("and VGA is the one that carries a low nibble") {
        // 0x60 reaches the HC32 as a VGA selection that leaves HS_IN on
        // sync-on-green. The nibble is not decoration.
        CHECK(InputSource::settingsFor(InputSource::Vga).frame == 0x61);
    }

    SUBCASE("which is why no other input has one") {
        const InputSource::Id others[] = {
            InputSource::Rgbs, InputSource::RgsB, InputSource::Ypbpr,
            InputSource::SVideo, InputSource::Composite};
        for (InputSource::Id id : others)
            CHECK((InputSource::settingsFor(id).frame & 0x0F) == 0);
    }
}

TEST_CASE("VGA is the only input that takes sync from the dedicated pin")
{
    // SP_EXT_SYNC_SEL 0 selects external H/V; everything else counts
    // composite or sync-on-green. This is the register half of the same
    // decision the frame's nibble makes at the HC32.
    CHECK(InputSource::settingsFor(InputSource::Vga).extSyncSel == 0);
    CHECK(InputSource::settingsFor(InputSource::Rgbs).extSyncSel == 1);
    CHECK(InputSource::settingsFor(InputSource::RgsB).extSyncSel == 1);
    CHECK(InputSource::settingsFor(InputSource::SVideo).extSyncSel == 1);
    CHECK(InputSource::settingsFor(InputSource::Composite).extSyncSel == 1);
}

TEST_CASE("the ADC mux and its sync-on-green follow the input")
{
    SUBCASE("the RGB inputs take ADC input 1 with sync-on-green enabled") {
        for (InputSource::Id id : {InputSource::Rgbs, InputSource::RgsB,
                                   InputSource::Vga}) {
            CHECK(InputSource::settingsFor(id).adcInputSel == 1);
            CHECK(InputSource::settingsFor(id).adcSogEn == 1);
        }
    }

    SUBCASE("and the decoded inputs take input 0 with it off") {
        for (InputSource::Id id : {InputSource::SVideo, InputSource::Composite}) {
            CHECK(InputSource::settingsFor(id).adcInputSel == 0);
            CHECK(InputSource::settingsFor(id).adcSogEn == 0);
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
    for (InputSource::Id id : {InputSource::Rgbs, InputSource::RgsB,
                               InputSource::Vga, InputSource::Ypbpr,
                               InputSource::SVideo, InputSource::Composite})
        CHECK(InputSource::settingsFor(id).writesAdc);
}

TEST_CASE("the decoded inputs take the unit out of low power")
{
    CHECK(InputSource::settingsFor(InputSource::SVideo).clearsLowPower);
    CHECK(InputSource::settingsFor(InputSource::Composite).clearsLowPower);
    CHECK_FALSE(InputSource::settingsFor(InputSource::Vga).clearsLowPower);
    CHECK_FALSE(InputSource::settingsFor(InputSource::Rgbs).clearsLowPower);
}

TEST_CASE("the six inputs persist as three legacy values, which is why Info exists")
{
    // SeleInputSource cannot tell RGBs from RGsB, nor YPbPr from S-Video from
    // composite. The id here IS the Info byte, which can -- so a restore keyed
    // on the legacy value can only ever reconstruct VGA.
    CHECK(InputSource::settingsFor(InputSource::Rgbs).legacySource == 1);
    CHECK(InputSource::settingsFor(InputSource::RgsB).legacySource == 1);
    CHECK(InputSource::settingsFor(InputSource::Vga).legacySource == 2);
    CHECK(InputSource::settingsFor(InputSource::Ypbpr).legacySource == 3);
    CHECK(InputSource::settingsFor(InputSource::SVideo).legacySource == 3);
    CHECK(InputSource::settingsFor(InputSource::Composite).legacySource == 3);
}

TEST_CASE("the RGB inputs are sync variants of one connector, not three inputs")
{
    // Which is what makes a search legitimate after an explicit choice: the
    // cable may carry composite sync where separate H/V was selected, on the
    // same pins. Searching those is fair; searching the other port's branches
    // is the 6 s timeout that has nothing to do with the input chosen.
    CHECK(InputSource::sharesPort(InputSource::Vga, InputSource::Rgbs));
    CHECK(InputSource::sharesPort(InputSource::Vga, InputSource::RgsB));
    CHECK(InputSource::sharesPort(InputSource::SVideo, InputSource::Composite));

    SUBCASE("and the two ports are distinct") {
        CHECK_FALSE(InputSource::sharesPort(InputSource::Vga, InputSource::SVideo));
        CHECK_FALSE(InputSource::sharesPort(InputSource::Rgbs, InputSource::Composite));
    }

    SUBCASE("and nothing chosen shares a port with anything") {
        CHECK_FALSE(InputSource::sharesPort(InputSource::None, InputSource::None));
        CHECK_FALSE(InputSource::sharesPort(InputSource::None, InputSource::Vga));
    }
}

TEST_CASE("an input can be named, so a request can carry one")
{
    CHECK(InputSource::fromName("vga") == InputSource::Vga);
    CHECK(InputSource::fromName("rgbs") == InputSource::Rgbs);
    CHECK(InputSource::fromName("rgsb") == InputSource::RgsB);
    CHECK(InputSource::fromName("ypbpr") == InputSource::Ypbpr);
    CHECK(InputSource::fromName("sv") == InputSource::SVideo);
    CHECK(InputSource::fromName("av") == InputSource::Composite);

    SUBCASE("and anything else is refused rather than guessed at") {
        CHECK(InputSource::fromName("") == InputSource::None);
        CHECK(InputSource::fromName("VGA ") == InputSource::None);
        CHECK(InputSource::fromName("component") == InputSource::None);
        CHECK(InputSource::fromName(0) == InputSource::None);
    }

    SUBCASE("and every name round-trips") {
        for (uint8_t id = InputSource::Rgbs; id <= InputSource::Composite; ++id) {
            const char *n = InputSource::name((InputSource::Id)id);
            CHECK(InputSource::fromName(n) == (InputSource::Id)id);
        }
    }
}

TEST_CASE("a stored id outside the six is nothing chosen")
{
    // Which is the whole point of the distinction: nothing chosen sweeps, and
    // a choice is obeyed. A value nobody wrote must not read as a choice.
    CHECK_FALSE(InputSource::chosen(0));
    CHECK_FALSE(InputSource::chosen(7));
    CHECK_FALSE(InputSource::chosen(255));
    for (uint8_t id = InputSource::Rgbs; id <= InputSource::Composite; ++id)
        CHECK(InputSource::chosen(id));
}

TEST_CASE("the boot restore reconstructs the input from the Info byte")
{
    // `Info` carries all six; `SeleInputSource` carries three. Keyed on the
    // legacy byte the restore cannot tell RGsB from RGBs, nor S-Video from
    // composite from YPbPr -- and it sent VGA the frame RGBs asks for, which is
    // where the missing nibble came from.
    CHECK(InputSource::fromStored(1) == InputSource::Rgbs);
    CHECK(InputSource::fromStored(2) == InputSource::RgsB);
    CHECK(InputSource::fromStored(3) == InputSource::Vga);
    CHECK(InputSource::fromStored(4) == InputSource::Ypbpr);
    CHECK(InputSource::fromStored(5) == InputSource::SVideo);
    CHECK(InputSource::fromStored(6) == InputSource::Composite);

    SUBCASE("and anything else is nothing chosen, which is what sweeps") {
        CHECK(InputSource::fromStored(0) == InputSource::None);
        CHECK(InputSource::fromStored(7) == InputSource::None);
        CHECK(InputSource::fromStored(255) == InputSource::None);
    }
}
