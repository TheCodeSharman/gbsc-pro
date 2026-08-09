#!/usr/bin/env python3
"""Step PB_FETCH_NUM one unit at a time and take a verdict per press.

    python3 sweep_fetch.py --host <ip> --from 260 --to 150

    ENTER  clear      t  torn      a  almost      b  back one step
    n      note       r  measure again      q  quit and restore the fetch

**The framing is held STILL, and that is the whole point.** `sweep_zoom.py`
varies the capture width, which varies the memory WRITE rate. This varies
`PB_FETCH_NUM`, which is the playback burst length and so the READ rate. One
rate at a time against a fixed other, which is what a beat needs to be measured
against and what the zoom sweep by construction cannot give.

## What it is testing

`PB_FETCH_NUM` 256 -> 200 cleared the tearing on 2026-08-09
(docs/investigations/hscale-tearing-characterisation.md). The fault is a beat between the
capture write bursts and the playback read bursts on the SDRAM bus, and no
single fetch value is clean at every zoom, because the write rate sweeps 70%
with the capture width while the read rate stays put.

So the engine has to COMPUTE the fetch, and this measures the function. The
candidate, with a mechanism behind it: choose the fetch so a whole number of
bursts covers the line, and every line then starts at the same phase rather
than precessing.

**It makes a prediction this tool can refute in one column.** If the fault is
periodic in the fetch, there are SEVERAL separate clean windows -- a comb at
words/2, words/3, words/4. If there is one broad valley, the model is wrong and
a measured constant is the honest answer. `--table` reads the windows off the
rows so the answer does not depend on anyone's eye for a pattern.

**NOTHING HERE SCORES A PICTURE.** The verdict comes from the screen.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import characterise  # noqa: E402  - after the path insert, same as the others
import sweep_zoom    # noqa: E402  - the keypress loop and settle wait, shared


# PB_FETCH_NUM is ten bits. Stepping past the ends would wrap a starved value
# into a register the picture depends on.
REGISTER = "PB_FETCH_NUM"
MINIMUM = 1
MAXIMUM = (1 << 10) - 1


class FetchSweep:
    """One consecutive walk of PB_FETCH_NUM, a measurement and a verdict a step.

    Same shape as sweep_zoom.Sweep and deliberately not a subclass of it: the
    two differ in what they hold still, which is the entire experimental
    distinction between them, and burying that in an inheritance relationship
    would make it look incidental.
    """

    def __init__(self, apply_fetch, snapshot, record, fetch, step,
                 prefix="comb", first=1):
        self._apply = apply_fetch
        self._snapshot = snapshot
        self._record = record
        self._origin = fetch
        self.fetch = fetch
        self.step_units = step
        self.prefix = prefix
        self.number = first
        self.pending = None

    def step(self):
        """One unit along, or None at the end of the field."""
        return self._move(self.step_units)

    def back(self):
        return self._move(-self.step_units)

    def remeasure(self):
        return self._measure()

    def _move(self, units):
        wanted = self.fetch + units
        if not MINIMUM <= wanted <= MAXIMUM:
            return None
        self.fetch = wanted
        self._apply(self.fetch)
        return self._measure()

    def _measure(self):
        registers = self._snapshot()
        self.pending = characterise.observe(registers.get, "", "")
        self.pending["sweep"] = {"register": REGISTER, "value": self.fetch}
        return self.pending

    def keep(self, verdict, note=""):
        row = self.pending
        row["label"] = f"{self.prefix}-{self.number:02d}"
        row["verdict"] = verdict
        row["note"] = note
        self._record(row)
        self.number += 1
        self.pending = None
        return row

    def restore(self):
        self._apply(self._origin)
        self.fetch = self._origin

    # The keypress loop reports whether the last press moved anything. Here it
    # always does -- the write is direct, with no solver in between -- so this
    # exists to satisfy the shared loop rather than to say anything.
    moved = True


def windows(rows):
    """The runs of consecutive CLEAN fetch values, widest end first.

    `almost` is not clean. Once a fix is being refined the residual is the
    discriminating category, and a window that is only clean if you squint is
    not a window.
    """
    clean = sorted((row["sweep"]["value"] for row in rows
                    if row.get("verdict") == "clean" and row.get("sweep")),
                   reverse=True)
    runs = []
    for value in clean:
        if runs and runs[-1][0] == value + 1:
            runs[-1][0] = value
        else:
            runs.append([value, value])
    return [(low, high) for low, high in runs]


def report(rows, words_per_line=None):
    """The windows, and what they would mean."""
    found = windows(rows)
    out = [f"  {len(found)} clean window(s):"]
    for low, high in found:
        span = f"{low}..{high}" if low != high else str(low)
        line = f"    {span:>12}  ({high - low + 1} wide)"
        if words_per_line:
            centre = (low + high) / 2.0
            line += f"   words/{words_per_line / centre:.2f}"
        out.append(line)
    out.append("")
    if len(found) >= 2:
        out.append("  SEVERAL windows -- the fault is periodic in the fetch, so")
        out.append("  the engine can compute it. The spacing is the formula.")
        centres = [(a + b) / 2.0 for a, b in found]
        gaps = [round(x - y, 1) for x, y in zip(centres, centres[1:])]
        out.append(f"  window centres {centres}, gaps {gaps}")
    elif len(found) == 1:
        out.append("  ONE window -- not periodic in the fetch. The 'whole number")
        out.append("  of bursts per line' model is refuted, and a measured")
        out.append("  constant per mode is the honest answer.")
    else:
        out.append("  NO clean window in the range swept.")
    return "\n".join(out)


def applier(host, regs, settle=1.0):
    """Write PB_FETCH_NUM by read-modify-write, the way setfield.py does.

    Ten bits across two bytes sharing a neighbour, so poking a byte with the
    value you wanted the field to have destroys whatever else lives there.
    """
    import setfield
    spec = regs[REGISTER]

    def apply(value):
        setfield.apply(host, REGISTER, spec, value, dry_run=False)

    return apply


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True)
    parser.add_argument("--from", dest="start", type=int,
                        help="fetch value to start at (default: what it is now)")
    parser.add_argument("--step", type=int, default=-1,
                        help="units per press (default -1, downward)")
    parser.add_argument("--prefix", default="comb")
    parser.add_argument("--index", default=characterise.DEFAULT_INDEX)
    parser.add_argument("--table", action="store_true",
                        help="read the clean windows off what is recorded")
    args = parser.parse_args(argv)

    rows = characterise.load(args.index)
    mine = [r for r in rows if r.get("sweep", {}).get("register") == REGISTER
            and str(r.get("label", "")).startswith(args.prefix)]

    if args.table:
        print()
        print(report(mine))
        print()
        return 0

    from gbs_unit import get_json
    with open(os.path.join(HERE, "tv5725_registers.json")) as f:
        regs = json.load(f)

    snapshot = sweep_zoom.snapshotter(args.host, regs)
    apply_fetch = applier(args.host, regs)

    status, _ = get_json(args.host, "/wifi/status")
    if status != 200:
        print(f"{args.host} did not answer")
        return 1

    now = snapshot().get(REGISTER)
    if now is None:
        print(f"could not read {REGISTER}")
        return 1
    start = now if args.start is None else args.start
    if start != now:
        print(f"  jumping {REGISTER} {now} -> {start}")
        apply_fetch(start)

    sweep = FetchSweep(apply_fetch, snapshot,
                       lambda row: characterise.append(args.index, row),
                       fetch=start, step=args.step, prefix=args.prefix,
                       first=sweep_zoom.next_number(rows, args.prefix))

    capture = snapshot()
    width = (capture.get("IF_HB_ST2") or 0) - (capture.get("IF_HB_SP2") or 0)
    scale = capture.get("VDS_HSCALE") or 1
    produced = width * 1024.0 / scale
    print(f"\n  {args.host}: {REGISTER} from {start}, {args.step:+d} a press"
          f"\n  framing HELD at capture width {width}, HSCALE {scale}, "
          f"produced {produced:.1f} px"
          f"\n  labels {args.prefix}-{sweep.number:02d} onward, into {args.index}")
    print(sweep_zoom.HELP.replace("back one step, and measure it again",
                                  "back one step"))

    try:
        sweep_zoom.run(sweep, out=print)
    except KeyboardInterrupt:
        print("\n  interrupted")
    finally:
        sweep.restore()
        print(f"  {REGISTER} restored to {sweep.fetch}")

    recorded = [r for r in characterise.load(args.index)
                if r.get("sweep", {}).get("register") == REGISTER
                and str(r.get("label", "")).startswith(args.prefix)]
    print()
    print(report(recorded, words_per_line=produced / 2.0))
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
