#!/usr/bin/env python3
"""Score the coast pair on a composite source, on the count AND on the blanking.

    python3 tools/gbsc-pro-hwtest/sweep_coast.py --host 192.168.88.108
    python3 tools/gbsc-pro-hwtest/sweep_coast.py --host … --pre 7,9,11 --post 3,6
    python3 tools/gbsc-pro-hwtest/sweep_coast.py --host … --dwell 20

`SP_PRE_COAST`/`SP_POST_COAST` hold the PLL over the vertical interval, where an
irregular reference would otherwise pull it. Two things answer to them and they
fail in different places, so both are scored here and neither alone is the
measure:

- **the count**, which a wrong coast makes the separator return nonsense for. The
  engine prints a `sampling:` line only when it re-measures, so a settled source
  prints NONE and a burst of differing counts is the dither itself. A `scan:`
  line is edge-triggered and appearing at all means the scan decision moved.
- **the vertical blanking**, which `IF_VB_ST` carries to the pin. Composite sync
  refuses a small set of values for it, the set moves with the coast, and only
  the test bus sees it -- `STATUS_IF_VT_OK` reads 1 and `VPERIOD_IF` reads a
  correct period while the signal is dead. Selector 2 is the control.

**NEVER FREEZE.** The freeze holds the pair and also stops the re-solve, so a
coast swept frozen measures the blanking with the count consequence frozen out.

**The engine solves `IF_VB_ST` from the framing, so it moves with the coast.**
The value a row was scored at is reported per row for that reason: two rows with
different blanking outcomes may be at different `IF_VB_ST`, which is a result
about the pair rather than about the coast alone.

../../docs/known-issues.md
"""
import argparse
import sys
import time

from gbs_unit import Console, get, get_json, read_fields

WINDOW_MS = 150

# The engine re-measures on its own cadence, so a window shorter than a few of
# its passes cannot tell a steady source from one nobody asked.
DWELL_S = 14

SETTLE_S = 8


def testbus(host, console, window_ms):
    """Transitions for the input formatter's vertical, and for the control."""
    console.drain()
    status, body = get(host, f"/testbus?ms={window_ms}&if=3")
    if status == 404 or (status == 500 and not body.strip()):
        sys.exit("no /testbus on this firmware; it is behind GBS_DEBUG")

    rows, done, deadline = {}, False, time.time() + 30
    while time.time() < deadline and not done:
        for line in console.collect(0.3):
            if line == "tb,done":
                done = True
            elif line.startswith("tb,") and not line.startswith("tb,header"):
                parts = line.split(",")
                if len(parts) >= 5:
                    rows[int(parts[1])] = int(parts[2])
        console.drain()
    return rows.get(0), rows.get(2)


def watch(console, seconds):
    """The counts the engine re-measured in the window, and whether the scan
    decision moved. No line at all is the settled case."""
    console.drain()
    counts, scans = [], []
    deadline = time.time() + seconds
    while time.time() < deadline:
        for line in console.collect(0.5):
            if line.startswith("sampling: ") and " lines x " in line:
                counts.append(int(line.split()[1]))
            elif line.startswith("scan: "):
                scans.append(line)
        console.drain()
    return counts, scans


def score(host, console, dwell, window_ms):
    counts, scans = watch(console, dwell)
    vertical, control = testbus(host, console, window_ms)
    fields = read_fields(host, [
        "SP_PRE_COAST", "SP_POST_COAST", "IF_VB_ST", "STATUS_SYNC_PROC_VTOTAL"])
    state = get_json(host, "/geometry")[1] or {}
    return {
        "applied": (fields["SP_PRE_COAST"], fields["SP_POST_COAST"]),
        "ifvb": fields["IF_VB_ST"],
        "vtotal": fields["STATUS_SYNC_PROC_VTOTAL"],
        "state": state.get("state", "?"),
        "resolves": len(counts),
        "distinct": sorted(set(counts)),
        "scans": scans,
        "vertical": vertical,
        "control": control,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--pre", default="0,4,7,9,11,13")
    parser.add_argument("--post", default="0,3,6,9,12")
    parser.add_argument("--dwell", type=float, default=DWELL_S)
    parser.add_argument("--window-ms", type=int, default=WINDOW_MS)
    args = parser.parse_args()

    pres = [int(v) for v in args.pre.split(",")]
    posts = [int(v) for v in args.post.split(",")]

    console = Console(args.host)
    time.sleep(0.5)

    print(f"# {'coast':>7} {'applied':>8} {'state':>9} {'ifvb':>5} {'vt':>5} "
          f"{'tb0':>4} {'ctl':>4} {'solves':>6}  counts")
    good = []
    try:
        for pre in pres:
            for post in posts:
                get(args.host, f"/coast?pre={pre}&post={post}")
                time.sleep(SETTLE_S)
                row = score(args.host, console, args.dwell, args.window_ms)

                steady = (row["state"] == "acquired"
                          and len(row["distinct"]) <= 1
                          and not row["scans"])
                live = bool(row["vertical"]) and bool(row["control"])
                mark = ""
                if steady and live:
                    mark = "   <<< count steady and blanking live"
                    good.append((pre, post, row))
                elif not live:
                    mark = "   no vertical"
                elif not steady:
                    mark = "   count moved"

                print(f"  {pre:>3}/{post:<3} {row['applied'][0]:>3}/"
                      f"{row['applied'][1]:<4} {row['state']:>9} "
                      f"{row['ifvb']:>5} {row['vtotal']:>5} "
                      f"{str(row['vertical']):>4} {str(row['control']):>4} "
                      f"{row['resolves']:>6}  {row['distinct']}{mark}",
                      flush=True)
                for line in row["scans"]:
                    print(f"        {line}", flush=True)
    finally:
        get(args.host, "/coast?clear=1")
        console.close()

    print()
    if not good:
        print("no pair gave both a steady count and a live blanking")
        return
    print("both, at:")
    for pre, post, row in good:
        print(f"  {pre}/{post} at IF_VB_ST {row['ifvb']}, count {row['vtotal']}")


if __name__ == "__main__":
    main()
