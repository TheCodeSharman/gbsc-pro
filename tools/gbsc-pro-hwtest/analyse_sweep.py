#!/usr/bin/env python3
"""Per-transition verdict from a sweeplog run, using SETTLED samples only.

The whole point is the settle filter. Tonight produced garbage HPERIOD readings
during almost every mode change that resolved to the correct value within a few
seconds -- calling those "railed" gave 15 false positives in one run. A rail
only counts if it persists in a window where VTOTAL is stable.

Verdicts:
  ok        settled HPERIOD matches the mode's expected value, stable
  RAILED    settled HPERIOD is rails-or-scatter while VTOTAL is steady
  bypass    preset 0x22 -- the IF is out of the path, so HPERIOD is meaningless
  no-lock   VTOTAL steady at a value that is not a real AKF50 mode
  short     dwell too short to judge

    python3 tools/gbsc-pro-hwtest/analyse_sweep.py sweeps/2026-08-04-akf50-b.jsonl.gz
"""
import gzip, json, sys
from collections import Counter

RAILS = (0, 511)
SETTLE = 6.0          # seconds to discard after a mode change
MIN_DWELL = 9.0

# stock AKF50: VTOTAL -> expected HPERIOD_IF, measured on a frozen unit
EXPECT = {311: 431, 312: 431, 261: 429, 262: 429, 363: 308, 364: 308,
          448: 214, 449: 214, 499: 179, 500: 179, 519: 176, 520: 176,
          524: 212, 525: 212, 533: 250, 534: 250, 624: 191, 625: 191,
          627: 176, 628: 176}


def samples(path):
    """The committed sweeps are gzipped; a fresh sweeplog run is not."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as fh:
        return [json.loads(line) for line in fh if line.strip()]


def main():
    rows = samples(sys.argv[1])
    if not rows:
        print("no samples")
        return 1
    t0 = rows[0]["t"]

    runs, cur = [], None
    for r in rows:
        if cur is None or r["vtotal"] != cur["vt"]:
            cur = {"vt": r["vtotal"], "rows": []}
            runs.append(cur)
        cur["rows"].append(r)

    print(f"{len(rows)} samples over {len(runs)} VTOTAL runs "
          f"({(rows[-1]['t']-t0)/60:.1f} min)\n")
    print(f"{'t(s)':>6} {'from':>5}{'':3}{'to':>5} {'dwell':>6} {'preset':>7} "
          f"{'exp':>5}  {'settled HPERIOD':<34} verdict")

    prev, tally = None, Counter()
    for run in runs:
        rs = run["rows"]
        dwell = rs[-1]["t"] - rs[0]["t"]
        frm = prev["vt"] if prev else None
        prev_for_next = run
        if dwell < MIN_DWELL:
            prev = prev_for_next
            continue
        settled = [r for r in rs if r["t"] - rs[0]["t"] >= SETTLE] or rs
        hp = [r["hperiod"] for r in settled]
        preset = Counter(r["preset"] for r in settled if r["preset"] is not None)
        pid = preset.most_common(1)[0][0] if preset else None
        dist = Counter(hp)
        top = "  ".join(f"{a}x{b}" for a, b in dist.most_common(3))
        exp = EXPECT.get(run["vt"])

        if pid == 0x22:
            verdict = "bypass (HPERIOD n/a)"
        elif exp is None:
            verdict = "no-lock (VTOTAL not a mode)"
        else:
            railed = sum(1 for v in hp if v in RAILS)
            # Scatter only counts if the values are actually WRONG. A settled
            # mode jitters by a count or two (148 ns each) and that is not a
            # fault -- 308/310/311 against an expected 308 is jitter, and an
            # earlier ">=4 distinct values" rule called it a rail.
            # Fraction-based, not spread-based: one bad read in fourteen is a
            # dropped sample, not a fault, and a raw spread test flags it.
            far = sum(1 for v in hp if abs(v - exp) > 8)
            if railed >= max(2, len(hp) // 4) or far >= max(2, len(hp) // 4):
                verdict = "*** RAILED"
            elif abs(dist.most_common(1)[0][0] - exp) <= 2:
                verdict = "ok"
            else:
                verdict = f"WRONG VALUE (expected ~{exp})"
        tally[verdict.split(" ")[0]] += 1
        pids = f"0x{pid:02x}" if pid is not None else "?"
        print(f"{rs[0]['t']-t0:6.0f} {str(frm):>5} {'->':^3}{run['vt']:5d} "
              f"{dwell:6.1f} {pids:>7} {str(exp):>5}  {top:<34} {verdict}")
        prev = prev_for_next

    print("\nsummary:", dict(tally))
    return 0


if __name__ == "__main__":
    sys.exit(main())
