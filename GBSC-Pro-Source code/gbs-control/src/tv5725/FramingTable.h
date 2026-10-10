#ifndef TV5725_FRAMING_TABLE_H_
#define TV5725_FRAMING_TABLE_H_

// The framing a user tuned, kept against the source it was tuned for, so
// leaving a source and coming back restores it with nobody touching a control.
//
// Pure: it holds entries and answers lookups. Reading and writing the file is
// somebody else's job, because the arithmetic is testable on the host and the
// filesystem is not. docs/framing-presets.md

#include <stddef.h>
#include <stdint.h>

#include "Aspect.h"
#include "PanAndZoom.h"
#include "SourceKey.h"

namespace Tv5725 {

class FramingTable {
public:
    // Bounded because it lives in the ESP8266's globals. Refusing when full is
    // the stated policy: dropping the oldest entry loses work the user did with
    // nothing said, where a refusal is visible and one can be cleared.
    static const uint16_t Entries = 16;

    FramingTable();

    // The framing stored against this source, if there is one. Either pointer
    // may be null when only the other, or only the presence, matters.
    bool find(const SourceKey &key, PanAndZoom *into,
              Aspect *shape = NULL) const;

    // Replaces an entry for the same source rather than adding one, so
    // re-tuning does not fill the table with its own history. False when the
    // key identifies nothing, or when the table is full and this is new.
    bool remember(const SourceKey &key, const PanAndZoom &framing,
                  Aspect shape = Aspect());

    // Takes every entry another table holds, shapes included. The file is read
    // into a scratch table so a read failing part way cannot leave the live one
    // holding half a file, and this is what crosses that gap. False when any
    // entry would not fit; what fitted is kept.
    bool adopt(const FramingTable &other);

    bool forget(const SourceKey &key);

    uint16_t count() const;

    // Moves whenever the table does, and only then. A pad press must not write
    // flash, so whoever owns the file debounces on this rather than paying for
    // a read every tick -- and comparing it against what was last written is
    // how a write is known to be owed at all.
    uint16_t revision() const;

    // For whoever writes the file out.
    const SourceKey &keyAt(uint16_t index) const;
    const PanAndZoom &framingAt(uint16_t index) const;
    Aspect aspectAt(uint16_t index) const;

    void clear();

private:
    // The NEAREST entry this key identifies, not the first one met. Identity is
    // not transitive, so several entries can match one reading, and a scan
    // returning the first lets the order of the file decide which framing a
    // source gets back.
    int16_t indexOf(const SourceKey &key) const;

    // Drops every entry other than the kept one that this reading also
    // identifies. Identity is not transitive, so two entries outside tolerance
    // of EACH OTHER can both be inside tolerance of one reading -- and left
    // standing they split the source's framing between them, a later reading
    // landing on whichever it happens to sit nearer.
    void collapseOnto(uint16_t kept, const SourceKey &identified);

    void moved();

    SourceKey keys_[Entries];
    PanAndZoom framings_[Entries];
    Aspect aspects_[Entries];
    uint16_t count_;
    uint16_t revision_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_TABLE_H_
