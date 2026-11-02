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

#include "FramingEntry.h"
#include "Route.h"

namespace Tv5725 {

class FramingTable {
public:
    // Bounded because it lives in the ESP8266's globals, and sized against what
    // a monitor definition asks for rather than against what fits: the stock
    // AKF50's modes produce 32 keys across both sync types and the bench's
    // RetroScaler-Acorn 124, so a table of 16 filled on the first sync type and
    // refused everything after it.
    //
    // Refusing when full is still the policy: dropping the oldest entry loses
    // work the user did with nothing said, where a refusal is visible and one
    // can be cleared. docs/framing-presets.md
    static const uint16_t Entries = 128;

    FramingTable();

    // What is stored against this source, if anything is. Either pointer may be
    // null when only the other, or only the presence, matters. The route is not
    // here: it is keyed differently, and findRoute() is the one way to ask.
    bool find(const SourceKey &key, PanAndZoom *into,
              Aspect *shape = NULL) const;

    // THE ROUTE IS NOT KEYED ON THE SYNC DUTY, because the duty is read through
    // the divider the path in force installed and the route is the field that
    // decides which path measures the source. Keyed on it, a route is written
    // under one key and read back under another, and the choice reads as never
    // made. docs/source-identity-and-framing-lookup.md
    bool findRoute(const SourceKey &key, Route *route) const;

    // Replaces an entry for the same source rather than adding one, so
    // re-tuning does not fill the table with its own history. False when the
    // key identifies nothing, or when the table is full and this is new.
    bool remember(const SourceKey &key, const PanAndZoom &framing,
                  Aspect shape = Aspect(), Route route = Route());

    // Takes every entry another table holds, whole. The file is read
    // into a scratch table so a read failing part way cannot leave the live one
    // holding half a file, and this is what crosses that gap. False when any
    // entry would not fit; what fitted is kept.
    bool adopt(const FramingTable &other);

    // Everything stored against this source: the entry, and the route wherever
    // it is held. The route lives on every entry the source has, so a reset
    // taken on one path would otherwise leave the choice standing on the
    // other and the default would never return. Entries for another source
    // that shares the count and the rate keep their framings.
    bool forget(const SourceKey &key);

    uint16_t count() const;

    // Moves whenever the table does, and only then. A pad press must not write
    // flash, so whoever owns the file debounces on this rather than paying for
    // a read every tick -- and comparing it against what was last written is
    // how a write is known to be owed at all.
    uint16_t revision() const;

    // For whoever writes the file out. By value because what is held is the
    // stored form, which FramingEntry turns back into the live one.
    SourceKey keyAt(uint16_t index) const;
    PanAndZoom framingAt(uint16_t index) const;
    Aspect aspectAt(uint16_t index) const;
    Route routeAt(uint16_t index) const;

    void clear();

private:
    // The nearest entry this key identifies apart from the duty that holds a
    // route somebody chose.
    int16_t routeIndexOf(const SourceKey &key) const;

    // Puts one route on every entry this reading identifies apart from the
    // duty, so the choice answers from whichever path measures the source
    // next. Held on the entry formed on one path the two disagree, and the
    // route then oscillates: each path finds the choice made on it and moves
    // to the other. Whether it changed anything.
    bool routeAcross(const SourceKey &key, Route route);

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

    FramingEntry entries_[Entries];
    uint16_t count_;
    uint16_t revision_;
};

}  // namespace Tv5725

#endif  // TV5725_FRAMING_TABLE_H_
