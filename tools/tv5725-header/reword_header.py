#!/usr/bin/env python3
"""Replace every comment in tv5725.h with the datasheet's own text.

    python3 tools/tv5725-header/reword_header.py

The comments came from an extraction that bled across field boundaries, so a
field's comment ended with the NEXT field's title and the field below lost its
own. 831 of them were wrong in that way. This rewrites them all from the
corrected parse.

Nothing here may move a register. `header.compare()` returning [] is what makes
that a proof rather than a claim, and it is checked before anything is written.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "gbsc-pro-hwtest"))

import extract_registers
import header
import rewrite

TARGET = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code",
                      "gbs-control", "tv5725.h")


def main():
    before = open(TARGET, encoding="utf-8", errors="replace").read()
    described = extract_registers.build(partitioned=False)
    after = rewrite.apply_wordings(
        before, {n: e["desc"] for n, e in described.items() if e["desc"]})

    moved = header.compare(before, after)
    if moved:
        raise SystemExit(f"refusing to write: {len(moved)} registers moved: "
                         f"{moved[:5]}")

    was, now = header.wordings(before), header.wordings(after)
    changed = sum(1 for n in now if was.get(n) != now[n])
    open(TARGET, "w", encoding="utf-8").write(after)
    print(f"{changed} comments changed, {len(moved)} registers moved")
    print(f"  described: {sum(1 for e in described.values() if e['desc'])}"
          f" of {len(described)} declared")


if __name__ == "__main__":
    main()
