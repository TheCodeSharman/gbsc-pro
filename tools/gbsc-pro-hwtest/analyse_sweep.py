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

It also reports the hsync duty per VTOTAL, under the same settle filter, which
is the measurement deciding whether the duty can carry a term of the preset key.
That question is open in exactly one direction: statically the duty is strong --
two tight clusters over 33 committed snapshots, holding across PLLAD_MD
1856..3072 -- but those are all settled states, and HPERIOD_IF's failure rates
are known precisely because somebody swept it. The sweeps committed here predate
the field, so they report nothing rather than a reassuring zero.

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


def sync_width(settled):
    """Commonest HLOW_LEN over these samples, its spread in counts, and the
    duty it implies. None if the sweep does not carry the field.

    None rather than a default, and this is the sharp case: the committed
    sweeps predate HLOW_LEN entirely, so a summary that reported "spread 0" for
    them would be read as the stability this measurement exists to establish,
    from runs that never took it.

    The commonest value rather than the mean, because a dropped read is one
    sample and a mean smears it across the answer -- and the key matches on a
    bucket, so the mode is the quantity it wants. HTOTAL is zero-based like
    every counter here, hence the +1; the firmware already does this in ratioHs.
    """
    widths = [r["hlow"] for r in settled if r.get("hlow") is not None]
    totals = [r["htotal"] for r in settled if r.get("htotal")]
    if not widths or not totals:
        return None
    hlow = Counter(widths).most_common(1)[0][0]
    htotal = Counter(totals).most_common(1)[0][0]
    return {"hlow": hlow,
            "spread": max(widths) - min(widths),
            "duty": hlow / (htotal + 1)}


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
          f"{'exp':>5}  {'settled HPERIOD':<34} {'duty':>16} verdict")

    prev, tally, widths = None, Counter(), {}
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
        width = sync_width(settled)
        if width is None:
            shown = "-"
        else:
            shown = f"{width['duty']:.4f} +-{width['spread']}"
            widths.setdefault(run["vt"], []).append(width)
        print(f"{rs[0]['t']-t0:6.0f} {str(frm):>5} {'->':^3}{run['vt']:5d} "
              f"{dwell:6.1f} {pids:>7} {str(exp):>5}  {top:<34} {shown:>16} "
              f"{verdict}")
        prev = prev_for_next

    print("\nsummary:", dict(tally))
    report_sync_width(widths)
    return 0


# The duty has to hold to about this to carry a term of the preset key: every
# separation that matters between two AKF50 modes is >=5 counts, and the
# tightest useful gap in the whole file is 3.6 (360x480 against 640x480 at
# VTOTAL 525).
DUTY_TOLERANCE = 2


def report_sync_width(widths):
    """Whether HLOW_LEN is steady enough, across mode changes, to key on.

    Silence when nothing measured it, rather than a clean bill of health from
    a sweep that predates the field.
    """
    if not widths:
        print("\nno HLOW_LEN in this sweep -- it predates the field, so it says "
              "nothing\nabout duty stability either way. Re-run with a current "
              "sweeplog.")
        return

    print(f"\nhsync duty per VTOTAL, settled samples only "
          f"(tolerance +-{DUTY_TOLERANCE} counts):")
    worst = 0
    for vt in sorted(widths):
        seen = widths[vt]
        spread = max(w["spread"] for w in seen)
        across = max(w["hlow"] for w in seen) - min(w["hlow"] for w in seen)
        worst = max(worst, spread, across)
        duties = " ".join(f"{w['duty']:.4f}" for w in seen)
        flag = "" if max(spread, across) <= DUTY_TOLERANCE else "   <-- MOVES"
        print(f"  VTOTAL {vt:4d}  {len(seen)} dwell(s)  HLOW_LEN spread "
              f"{spread} within, {across} across   {duties}{flag}")

    print(f"\n  worst {worst} counts against a tolerance of {DUTY_TOLERANCE}")
    print("  the duty holds across mode changes, so it can carry the key"
          if worst <= DUTY_TOLERANCE else
          "  the duty MOVES across mode changes -- the key degrades to\n"
          "  (VTOTAL, rate) plus the variant selector")


if __name__ == "__main__":
    sys.exit(main())
