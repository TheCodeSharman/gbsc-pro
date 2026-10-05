#ifndef PREFS_SETTINGS_H_
#define PREFS_SETTINGS_H_

// The user's settings as lines of text:
//
//   # GBSC-Pro settings
//   output = 1920x1080
//   frame-time-lock = 0
//   end
//
// One `key = value` a line. A missing key takes its default, an unknown one is
// skipped, and a malformed one is skipped rather than fatal -- so a build with a
// different field set reads a file another wrote instead of replacing it, and
// adding or removing a setting cannot shift the meaning of any other.
//
// `end` is the last line a save writes, and a read that does not reach it is a
// read that failed. That is the whole of the integrity check the positional file
// needed a byte count for, and it works at any length.
//
// Reading and writing the file belongs to the caller. This turns one line into a
// setting and one setting into a line, which is what makes the format testable
// on the host. docs/preferences-file.md

#include <stdint.h>

#include "SettingVisitor.h"

struct userOptions;
struct avOptions;

namespace Tv5725 {
class ColourBalance;
class PictureOptions;
}

namespace Prefs {

class Settings {
public:
    enum Line {
        // A comment, a blank line, an unknown key, or one the grammar does not
        // spell.
        Skipped,
        Applied,
        End,
    };

    Settings(userOptions &options, avOptions &av, Tv5725::ColourBalance &colour,
             Tv5725::PictureOptions &picture, uint8_t &volume,
             uint8_t &legacyInput, uint8_t &brightnessSet);

    // Every setting to its default: what a unit with no file comes up on, and
    // what a retried read starts from.
    void defaults();

    // What the reset button leaves. The AV module's picture and the chosen
    // input are not scaler preferences and survive it.
    void resetScalerSettings();

    Line readLine(const char *line);

    // The setting at `index` as a line, NUL terminated. False past the last one,
    // and for a line that would not fit -- a truncated line reads back as a
    // skipped one, which is a lost setting reported as nothing at all.
    bool writeLine(uint16_t index, char *out, uint8_t size);

    // The line a save ends with, which a read has to reach.
    static const char *terminator();

private:
    void each(SettingVisitor &visit);

    // The scaler's own preferences, which the reset button wipes.
    void eachScalerSetting(SettingVisitor &visit);

    // The board's: the AV module's picture, the volume and the chosen input.
    void eachBoardSetting(SettingVisitor &visit);

    userOptions &options_;
    avOptions &av_;
    Tv5725::ColourBalance &colour_;
    Tv5725::PictureOptions &picture_;
    uint8_t &volume_;
    uint8_t &legacyInput_;
    uint8_t &brightnessSet_;
};

}  // namespace Prefs

#endif  // PREFS_SETTINGS_H_
