#!/usr/bin/env python3
"""Who programs the TV5725, and how much of it still bypasses the engine.

    python3 tools/gbsc-pro-hwtest/write_owners.py
    python3 tools/gbsc-pro-hwtest/write_owners.py --contested
    python3 tools/gbsc-pro-hwtest/write_owners.py --sites

The campaign in docs/chip-initialisation.md is "one class per subsystem owns its
registers". Every address a PRESET writes has had SOME owner since 2026-08-15.
That is not the same question as whether the ENGINE owns them.

This asks the second question: how much pre-existing code programs the TV5725
without going via the engine.

Four owners, and only the first is the destination:

  engine     src/tv5725/*.cpp        hand-written, one class per subsystem
  bringup    src/tv5725/bringup/     generated from the tables, transitional
  sketch     gbs-control.ino         the legacy firmware, ~19k lines
  other      src/**, other headers   OLED, IR, clock, diagnostics

**A field written by both the engine and the sketch is the dangerous case**, not
merely untidy: whichever runs later wins, and this project has already paid for
it -- CAP_SAFE_GUARD_EN was written 1 by FrameBuffer::apply() and 0 by an
uncommented line further down doPostPresetLoadSteps(), and every check that
compared the map against the tables passed throughout. --contested lists them.

Counting rules, all of which matter:

  - A field is counted once per (owner, field), not per call site, so a name
    written five times in one function does not read as five problems. --sites
    gives the raw call sites instead.
  - Raw writeOneByte()/writeBytes() are counted SEPARATELY and by address where
    one is visible, because they carry no field name -- which is exactly why a
    by-name check cannot see them. They are the blind spot, not a footnote.
  - Tv5725.h is skipped: it declares the fields, it does not write them. It sits
    under src/tv5725/ where owner_of() would otherwise count it as the engine.
"""

import argparse
import glob
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code", "gbs-control")

# A field reached through the legacy flat view (GBS::VDS_HSCALE), through its
# owning subsystem (Tv5725::VideoProcessor::VDS_HSCALE), or unqualified from
# inside that subsystem's own .cpp. Requiring the GBS:: prefix used to be safe
# and stopped being so the moment a block moved into its owner: the writes were
# still there, and this counted none of them.
# The name must carry an underscore, which every genuine field does. That is what
# separates a field write from the bulk GBS::write(segment, reg, value), whose
# bytes carry no field name at all and are counted as raw below.
WRITE = re.compile(
    r"\b(?:[A-Za-z_][A-Za-z0-9_]*::)*?([A-Z][A-Z0-9]*_[A-Z0-9_]*(?:x[0-9A-F]+)?)::write\s*\(")
RAW = re.compile(r"\b(writeOneByte|writeBytes)\s*\(\s*(0[xX][0-9a-fA-F]+)?")

# A function definition at column 0 -- good enough for the .ino, which is where
# the interesting attribution is, and for `Type Class::method(` in the .cpp
# files. Not a parser; it only has to name the neighbourhood.
FUNC = re.compile(r"^[A-Za-z_][A-Za-z0-9_:<>\*&\s]*?([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)?)\s*\([^;]*$")


def owner_of(path):
    rel = os.path.relpath(path, SKETCH)
    if rel.startswith(os.path.join("src", "tv5725", "bringup")):
        return "bringup"
    if rel.startswith(os.path.join("src", "tv5725")):
        return "engine"
    if rel.endswith(".ino"):
        return "sketch"
    return "other"


def sources():
    for pattern in ("*.ino", "*.cpp", "*.h",
                    os.path.join("src", "**", "*.cpp"),
                    os.path.join("src", "**", "*.h")):
        for path in glob.glob(os.path.join(SKETCH, pattern), recursive=True):
            if os.path.basename(path) == "Tv5725.h":
                continue
            yield path


def scan():
    """[(owner, file, line, function, kind, name)] for every write in the tree."""
    found = []
    for path in sources():
        owner = owner_of(path)
        function = "(file scope)"
        with open(path, encoding="utf-8", errors="replace") as f:
            for number, line in enumerate(f, 1):
                stripped = line.rstrip("\n")
                if stripped and not stripped[0].isspace():
                    match = FUNC.match(stripped)
                    if match and not stripped.lstrip().startswith("//"):
                        function = match.group(1)
                code = stripped.split("//", 1)[0]
                for name in WRITE.findall(code):
                    found.append((owner, path, number, function, "field", name))
                for kind, address in RAW.findall(code):
                    label = f"{kind}({address})" if address else f"{kind}(?)"
                    found.append((owner, path, number, function, "raw", label))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--contested", action="store_true",
                    help="only the fields the engine and something else BOTH write")
    ap.add_argument("--sites", action="store_true",
                    help="every call site, not one line per (owner, field)")
    ap.add_argument("--owner", help="restrict to one owner: engine, bringup, sketch, other")
    args = ap.parse_args()

    found = scan()
    if args.owner:
        found = [f for f in found if f[0] == args.owner]

    fields = defaultdict(set)      # owner -> {field}
    raw_by_owner = defaultdict(int)
    per_function = defaultdict(lambda: defaultdict(set))
    for owner, _path, _n, function, kind, name in found:
        if kind == "field":
            fields[owner].add(name)
            per_function[owner][function].add(name)
        else:
            raw_by_owner[owner] += 1

    if args.contested:
        engine = fields["engine"]
        print("  fields the engine writes that something else writes too")
        print("  -- whichever runs later wins, and nothing reports the loser\n")
        any_found = False
        for owner in ("sketch", "bringup", "other"):
            both = sorted(engine & fields[owner])
            if not both:
                continue
            any_found = True
            print(f"    engine vs {owner}   {len(both)} fields")
            for name in both:
                print(f"      {name}")
            print()
        if not any_found:
            print("    none -- the engine's fields have no second writer")
        return 0

    if args.sites:
        for owner, path, number, function, kind, name in sorted(found):
            rel = os.path.relpath(path, SKETCH)
            print(f"  {owner:8} {rel}:{number}  {function}()  {name}")
        print(f"\n  {len(found)} call sites")
        return 0

    print("  WHO PROGRAMS THE TV5725\n")
    print(f"  {'owner':10} {'distinct fields':>15} {'call sites':>11} {'raw byte writes':>17}")
    total_sites = 0
    for owner in ("engine", "bringup", "sketch", "other"):
        sites = sum(1 for f in found if f[0] == owner and f[4] == "field")
        total_sites += sites
        print(f"  {owner:10} {len(fields[owner]):>15} {sites:>11} {raw_by_owner[owner]:>17}")
    print(f"  {'':10} {'':>15} {total_sites:>11} {sum(raw_by_owner.values()):>17}")

    everything = set().union(*fields.values()) if fields else set()
    not_engine = everything - fields["engine"]
    print(f"\n  {len(everything)} distinct fields are written anywhere.")
    print(f"  {len(fields['engine'])} of them by the engine, "
          f"{len(not_engine)} only by something else.")

    contested = sorted(fields["engine"] & (fields["sketch"] | fields["other"]))
    print(f"  {len(contested)} are written by the engine AND by legacy code "
          f"-- see --contested.")

    print("\n  THE SKETCH, BY FUNCTION (top 20 by distinct fields written)")
    ranked = sorted(per_function["sketch"].items(),
                    key=lambda kv: len(kv[1]), reverse=True)
    for function, names in ranked[:20]:
        print(f"    {len(names):4d}  {function}()")
    if len(ranked) > 20:
        rest = sum(len(n) for _, n in ranked[20:])
        print(f"    {rest:4d}  ... and {len(ranked) - 20} more functions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
