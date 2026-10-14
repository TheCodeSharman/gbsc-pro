#ifndef PREFS_SETTINGS_FILE_H_
#define PREFS_SETTINGS_FILE_H_

// The settings file on flash: how many attempts a read spends, what it starts
// each one from, and what a save puts down. Settings is the grammar over one
// line; this is the file the lines live in.
//
// It reports each attempt rather than logging one, so it needs neither a clock
// nor a printf -- the caller formats, which is also what keeps the boot log's
// PREFS lines in the one place that can timestamp them.
// docs/preferences-file.md

#include <stddef.h>
#include <stdint.h>

namespace Prefs {

class Settings;

class SettingsFile {
public:
    // What one attempt did, for the caller to log.
    struct Attempt {
        uint8_t index;
        bool opened;
        uint32_t size;
        uint16_t applied;
        bool reachedEnd;
    };

    typedef void (*Report)(const Attempt &);

    SettingsFile(Settings &settings, const char *path);

    bool exists() const;

    // Read the file in, starting every attempt from the defaults so a
    // part-applied read is never what the next one builds on. True once the
    // terminator arrived, which is the whole of the integrity check the format
    // needs.
    //
    // **WAIT FOR THE FILE TO BE READABLE, NOT MERELY OPENABLE.** A flash awake
    // enough to serve metadata and not yet awake enough to serve content
    // satisfies the open and the size on the first attempt and hands the parser
    // nothing, so the retry is on reaching the terminator. A caller reading a
    // file it has just written itself needs one attempt; a boot spends
    // BootAttempts.
    bool read(uint8_t attempts, Report report = NULL);

    // Every setting, ending with the terminator a read has to reach.
    bool write();

    // A second of patience before a boot gives up, which costs nothing where
    // the first attempt succeeds.
    static const uint8_t BootAttempts = 10;
    static const uint16_t RetryDelayMs = 100;

    // Long enough for the longest line the grammar writes.
    static const uint8_t LineBytes = 80;

private:
    bool readOnce(Attempt *attempt);

    Settings &settings_;
    const char *path_;
};

}  // namespace Prefs

#endif  // PREFS_SETTINGS_FILE_H_
