#!/usr/bin/env python3
"""Check every comment in tv5725.h against RD-5725-1.1.

    python3 tools/tv5725-header/audit_comments.py
    python3 tools/tv5725-header/audit_comments.py --show handwritten

Two different questions, and they need different treatment:

  GENERATED comments were copied from the datasheet by the audit, so the useful
  check is whether they still match it -- a mismatch means the text drifted, or
  was attached to the wrong field by a later reflow.

  HAND-WRITTEN comments were never checked against anything. They are the
  smaller set and the riskier one: they carry claims about widths, offsets and
  behaviour that someone reasoned out, and a wrong one is worse than silence
  because it reads like authority.

The mis-attribution check matters as much as the wording. Regrouping the header
swept two narrative blocks into PLL_VS's column, 2074 characters that were never
about PLL_VS. That one was obvious from its size; a note landing on the
neighbouring field would not be.
"""
import argparse
import difflib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import header

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "..", "GBSC-Pro-Source code",
                      "gbs-control", "tv5725.h")

TAG = re.compile(r"\[datasheet:\s*(.+?)\]\s*$")


def squash(text):
    return re.sub(r"[^a-z0-9]", "", text.lower())


def classify(name, comment, docs, by_addr, ident):
    """generated | tagged | handwritten, plus the datasheet text it should match."""
    tag = TAG.search(comment)
    if tag:
        # The STATUS block was annotated by ADDRESS, since gbs-control invented
        # readable names where the datasheet uses positional ones.
        return "tagged", by_addr.get(ident[name], {}).get("desc", "")
    entry = docs.get(name)
    if entry and squash(entry["desc"]) and squash(entry["desc"]) in squash(comment):
        return "generated", entry["desc"]
    return "handwritten", (entry or {}).get("desc", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--merged", default=os.path.join(HERE, "merged.json"))
    ap.add_argument("--show", choices=["handwritten", "drift", "all"])
    args = ap.parse_args()

    docs = json.load(open(args.merged))
    by_addr = {}
    for entry in docs.values():
        by_addr.setdefault((entry["seg"], entry["reg"], entry["lo"],
                            entry["width"]), entry)

    src = open(HEADER).read()
    fields = header.annotated(src)
    ident = {t.name: (t.seg, t.reg, t.offset, t.width) for t, _ in fields}

    buckets = {"generated": [], "tagged": [], "handwritten": [], "bare": []}
    drift = []
    for t, comment in fields:
        if not comment:
            buckets["bare"].append((t.name, comment, ""))
            continue
        kind, expect = classify(t.name, comment, docs, by_addr, ident)
        buckets[kind].append((t.name, comment, expect))
        if kind == "tagged" and expect and squash(expect) not in squash(comment):
            drift.append((t.name, comment, expect))

    print(f"{len(fields)} fields")
    for kind in ("generated", "tagged", "handwritten", "bare"):
        print(f"  {kind:<12} {len(buckets[kind]):>4}")
    print(f"\ntagged comments that no longer match the datasheet: {len(drift)}")

    if args.show == "handwritten":
        for name, comment, expect in buckets["handwritten"]:
            print(f"\n--- {name}  {header.where(ident[name])}")
            print(f"  header: {comment}")
            if expect:
                print(f"  sheet : {expect}")
    elif args.show == "drift":
        for name, comment, expect in drift:
            print(f"\n--- {name}  {header.where(ident[name])}")
            print(f"  header: {comment[:200]}")
            print(f"  sheet : {expect[:200]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
