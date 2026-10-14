#ifndef PREFS_FRAMING_FILE_H_
#define PREFS_FRAMING_FILE_H_

// The framing table's file on flash. Tv5725::FramingText is the grammar over
// one line; this is the file the lines live in, which is why it sits beside
// SettingsFile rather than beside the grammar. docs/framing-presets.md

#include <stdint.h>

namespace Tv5725 {
class FramingTable;
}

namespace Prefs {

class FramingFile {
public:
    FramingFile(Tv5725::FramingTable &table, const char *path);

    // THE TABLE IS LEFT ALONE UNLESS THE WHOLE FILE ARRIVED, and the size is
    // what says so. A flash awake enough to serve metadata and not yet awake
    // enough to serve content satisfies the open and the size and hands the
    // parser nothing, and this format has no terminator to notice that with --
    // every line is independently skippable by design, so a short read is
    // indistinguishable from a file with fewer sources in it. Bytes consumed
    // against bytes claimed separates them, and catches a partial read too.
    //
    // NOTHING STORED YET IS NOT A FAILED READ: every source takes its computed
    // default and the first tuning has to be saveable.
    //
    // NO RETRY, AND THE ORDER IS WHY. The flash's power-up race is waited out
    // by the settings file, whose read retries until its own terminator arrives
    // and which runs first for that reason -- so a flash serving this open has
    // already served a file whole, milliseconds earlier.
    // ../../../../docs/preferences-file.md
    bool read();

    // Every entry, behind the header that says what the format is. False where
    // the last read did not come back: the table then holds what this boot
    // could read rather than what the file holds, and writing it is the one way
    // a transient flash fault becomes permanent.
    bool write();

    // Whether the table in memory is what the file holds.
    bool readable() const;

    // Long enough for the longest line the grammar writes.
    static const uint8_t LineBytes = 80;

private:
    Tv5725::FramingTable &table_;
    const char *path_;
    bool readable_;
};

}  // namespace Prefs

#endif  // PREFS_FRAMING_FILE_H_
