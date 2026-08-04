#!/usr/bin/env python3
"""Does a deep sync loss predict the HPERIOD_IF fault?

The committed claim is a clean split: 0 failures in 32 transitions that were not
preceded by a deep sync loss. That came from 42 transitions. This re-derives it
over every sweep run, classifying what happened BETWEEN two judged dwells:

  deep   an intervening VTOTAL run read 0, or a value that is no mode at all
  blip   an intervening run read only the ordinary 97/98/99 mode-change artefact
  clean  the source went from one real mode straight to another

Verdict for the destination dwell is analyse_sweep's, unchanged -- settled
samples only, so mode-change transients are not scored.

Reported in docs/tv5725-chip.md. The numbers there come from exactly this, run
over the two committed sweeps:

    python3 tools/gbsc-pro-hwtest/precursor.py \
        tools/gbsc-pro-hwtest/sweeps/2026-08-04-akf50-*.jsonl.gz
"""
import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse_sweep import EXPECT, RAILS, SETTLE, MIN_DWELL, samples

BLIP = (97, 98, 99)


def runs_of(path):
    rows = samples(path)
    out, cur = [], None
    for r in rows:
        if cur is None or r["vtotal"] != cur["vt"]:
            cur = {"vt": r["vtotal"], "rows": []}
            out.append(cur)
        cur["rows"].append(r)
    return out


def verdict(run):
    """None when the dwell cannot be judged, else True for failed."""
    rs = run["rows"]
    if rs[-1]["t"] - rs[0]["t"] < MIN_DWELL:
        return None
    settled = [r for r in rs if r["t"] - rs[0]["t"] >= SETTLE] or rs
    hp = [r["hperiod"] for r in settled]
    preset = Counter(r["preset"] for r in settled if r["preset"] is not None)
    if preset and preset.most_common(1)[0][0] == 0x22:
        return None                                  # bypass: the IF is out of the path
    exp = EXPECT.get(run["vt"])
    if exp is None:
        return None                                  # VTOTAL is not a mode we have an expectation for
    railed = sum(1 for v in hp if v in RAILS)
    far = sum(1 for v in hp if abs(v - exp) > 8)
    if railed >= max(2, len(hp) // 4) or far >= max(2, len(hp) // 4):
        return True
    return abs(Counter(hp).most_common(1)[0][0] - exp) > 2


def classify(between):
    """What kind of event separated the two judged dwells?"""
    if any(r["vt"] == 0 or (r["vt"] not in EXPECT and r["vt"] not in BLIP)
           for r in between):
        return "deep"
    if any(r["vt"] in BLIP for r in between):
        return "blip"
    return "clean"


def settled_hperiods(run):
    rs = run["rows"]
    return [r["hperiod"] for r in rs if r["t"] - rs[0]["t"] >= SETTLE] or \
           [r["hperiod"] for r in rs]


def main(paths):
    tally, fails, detail = Counter(), Counter(), []
    dwells, mode_fails, bad_values = Counter(), Counter(), {}
    for path in paths:
        runs = runs_of(path)
        prev_judged = None                           # index of the last dwell we scored
        for i, run in enumerate(runs):
            v = verdict(run)
            if v is None:
                continue
            dwells[run["vt"]] += 1
            if v:
                mode_fails[run["vt"]] += 1
                bad_values.setdefault(run["vt"], Counter()).update(
                    settled_hperiods(run))
            if prev_judged is not None:
                kind = classify(runs[prev_judged + 1:i])
                tally[kind] += 1
                if v:
                    fails[kind] += 1
                    detail.append((path, kind, runs[prev_judged]["vt"], run["vt"]))
            prev_judged = i

    print(f"{'preceded by':>12} {'transitions':>12} {'failed':>7} {'rate':>7}")
    for kind in ("deep", "blip", "clean"):
        n, f = tally[kind], fails[kind]
        rate = f"{100 * f / n:.0f}%" if n else "-"
        print(f"{kind:>12} {n:>12} {f:>7} {rate:>7}")
    print(f"{'total':>12} {sum(tally.values()):>12} {sum(fails.values()):>7}")

    # The destination mode discriminates far better than the precursor does,
    # which is the whole reason this script exists in the repo rather than in
    # a scratchpad -- see docs/tv5725-chip.md.
    print(f"\n{'VTOTAL':>7}{'expected':>9}{'dwells':>8}{'failed':>8}{'rate':>7}"
          f"   settled HPERIOD_IF over the failing dwells")
    for vt in sorted(dwells, key=lambda k: (-mode_fails[k], k)):
        top = "  ".join(f"{v}x{n}" for v, n in bad_values.get(vt, Counter()).most_common(4))
        print(f"{vt:>7}{EXPECT.get(vt, 0):>9}{dwells[vt]:>8}{mode_fails[vt]:>8}"
              f"{100 * mode_fails[vt] / dwells[vt]:>6.0f}%   {top}")

    print("\nfailures:")
    for path, kind, frm, to in detail:
        print(f"  {os.path.basename(path):<28} {kind:<6} {frm} -> {to}")


if __name__ == "__main__":
    main(sys.argv[1:])
