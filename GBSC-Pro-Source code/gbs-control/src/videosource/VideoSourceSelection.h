#ifndef VIDEOSOURCE_VIDEO_SOURCE_SELECTION_H_
#define VIDEOSOURCE_VIDEO_SOURCE_SELECTION_H_

// The six video sources the unit offers, and everything that differs between
// them. Nothing here is about key presses -- src/input/ is those.
//
// **THE VIDEO SOURCE PATH IS TWO MUXES IN SERIES AND ONLY ONE IS VISIBLE.** The TV5725's
// ADC_INPUT_SEL picks which ADC input is read; whether the HC32F460 has connected
// anything to it is asw_01..04, which appear in no register dump and cannot be
// read back. Both are set from one row here, so the halves cannot disagree.
//
// **VGA'S FRAME CARRIES A LOW NIBBLE AND THE OTHERS DO NOT.** It is what raises
// asw_01 -- the schematic's choice of the dedicated HSync pin over sync-on-green.
// 0x60 selects VGA and leaves HS_IN on SOGIN, so a VGA source with separate sync
// gives the sync processor nothing to count: SP_VTOTAL 0 while HPERIOD_IF reads
// the line correctly. That is the fault this table exists to make unrepeatable.
//
// Pure, and free of Arduino, so it is host-tested. The acting on it -- the frame,
// the register writes, the preferences -- belongs to the callers, which is what
// lets the OLED, HTTP and the boot restore share one answer.

#include <stdint.h>

class VideoSourceSelection {
public:
    // The values the preferences file already keeps in `Info`, so the enum IS
    // the stored representation and nothing new goes on flash.
    enum Id : uint8_t {
        None = 0,
        Rgbs = 1,
        RgsB = 2,
        Vga = 3,
        Ypbpr = 4,
        SVideo = 5,
        Composite = 6,
    };

    struct Settings {
        // The 'S' command's byte, low nibble included.
        uint8_t frame;

        // SeleInputSource. Three values for six inputs, so it cannot tell RGBs
        // from RGsB nor YPbPr from S-Video from composite -- kept because the
        // file is positional and other code still reads it, not because a
        // restore can use it.
        uint8_t legacySource;

        // BriorCon.
        uint8_t brightnessSet;

        uint8_t adcInputSel;
        uint8_t adcSogEn;

        // SP_EXT_SYNC_SEL: 0 takes H and V from the dedicated pins, 1 leaves the
        // sync processor on composite or sync-on-green.
        uint8_t extSyncSel;

        bool clearsLowPower;
    };

    // Whether a stored id names one of the six. **A value nobody wrote must not
    // read as a choice**: nothing chosen is what makes detection sweep, and a
    // choice is what it must obey instead.
    static bool chosen(uint8_t id);

    static Settings settingsFor(Id id);

    // Which physical connector an input reads. **RGBs, RGsB and VGA share one**
    // -- they are composite sync, sync-on-green and separate H/V on the same
    // pins, not three inputs. So a cable presenting a different sync than the
    // one selected is ordinary, and searching the variants of the CHOSEN port is
    // legitimate where searching another port's is not: that is where a
    // selection waits out timeouts for branches the ADC is not even reading.
    // Whether the sync type has to be measured, or the connector settles it.
    // Only VGA takes H and V on their own pins and so can present either;
    // everything else is composite sync or sync-on-green by construction, where
    // probing answers "own vsync" and puts the source on a path it cannot lock
    // to. docs/sync-type-selection.md
    static bool syncTypeMustBeMeasured(Id id);

    static uint8_t port(Id id);
    static bool sharesPort(Id a, Id b);

    // The three sync variants of the RGB connector, which is what the firmware
    // means by RGBHV -- the connector, not the sync type, so composite sync and
    // sync-on-green on those pins are in it.
    //
    // This says an RGBHV source is SELECTED. The standard byte it replaces said
    // one had been DETECTED, and on a freshly chosen input with no sync yet the
    // two differ. docs/video-source-acquisition.md
    static bool isRgbhv(Id id);

    // For the boot restore, which reads `Info` from the preferences. That byte
    // carries all six; `SeleInputSource` carries three, so a restore keyed on
    // the legacy value cannot tell RGsB from RGBs and sends the wrong frame for
    // four of the six.
    static Id fromStored(uint8_t info);

    // For a request that carries an input by name. Anything unrecognised is
    // None rather than a guess -- selecting the wrong input is worse than
    // refusing.
    static Id fromName(const char *name);
    static const char *name(Id id);

    // **WHICH SOURCE IS SELECTED, HELD HERE.** It is not measurable: nothing on
    // the chip reports which of the six is connected, because half the path is
    // the HC32F460's analog switches and those cannot be read back at all. So
    // the selection is recorded as it is made and every reader asks this.
    static void select(Id id);

    // The same, from a stored byte: anything naming none of the six selects
    // nothing, which is what makes detection sweep.
    static void selectStored(uint8_t info);

    static Id selected();

    // A different unit, as far as anything reading the selection is concerned.
    static void forgetSelection();
};

#endif  // VIDEOSOURCE_VIDEO_SOURCE_SELECTION_H_
