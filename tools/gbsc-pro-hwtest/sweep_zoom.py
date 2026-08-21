#!/usr/bin/env python3
"""Step the framing one unit at a time and take a verdict per press.

    python3 sweep_zoom.py --host <ip>

    ENTER  clear      t  torn      a  almost      b  back one step
    n      note       r  measure again      q  quit and restore the framing

Each press moves the zoom by ONE INPUT UNIT, waits for the geometry to settle,
records the thirty registers `characterise.py` records, and asks for the
verdict.

`a` is the RESIDUAL, and it is its own category because the question it answers
is whether a fix is COMPLETE. "Almost counts as clear" is right while the job is
finding the band edges of a gross fault; once most of the fault is gone, scoring
a residual clean puts 66/66 clean in the log and erases what separates "fixed"
from "much better".

**The swept variable is the FRAMING, not VDS_HSCALE.** The engine takes a zoom
in input units and solves every window from it; HSCALE is an output and does not
move on every press -- `dense-01` widened the capture 842 -> 843 with HSCALE
unchanged at 694. Sweeping HSCALE directly would be a different experiment, and
one the firmware would undo at the next solve.

**SWEEP CONSECUTIVE PRESSES, SKIPPING NOTHING.** The fault alternates on roughly
one unit, so readings 5 to 30 units apart are aliased by construction and every
pattern found across those gaps is an artefact -- periodicity in HSCALE, distance
from a whole pixel, capture width parity, four of them refuted that way. A
press-and-type cycle per reading is what makes a dense sweep practical.

**NOTHING HERE SCORES A PICTURE.** The verdict comes from the screen.
"""

import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import characterise  # noqa: E402  - after the path insert, same as the others


# ENTER is clear because clear is the common case in a dense sweep and the
# common case should cost the least. `t` is torn. Nothing else is a verdict:
# a mis-hit key must not be recorded as a picture nobody looked at.
#
# `a` is the RESIDUAL. "Almost counts as clear" is right while the job is finding
# the band edges of a gross fault; once most of the fault is gone the question is
# whether the fix is COMPLETE, and scoring a residual clean puts 66/66 clean in
# the log and erases the only thing separating "fixed" from "much better".
VERDICT_KEYS = {
    "": "clean", "\r": "clean", "\n": "clean", " ": "clean",
    "t": "glitch", "T": "glitch",
    "a": "almost", "A": "almost",
}


# What the solver writes, and the only thing that "the geometry has settled" or
# "the press moved something" can be judged on. The rest of a snapshot is the
# rasters -- constant -- and segment 0's LIVE MEASUREMENTS, which blip: settling
# on those waits the whole timeout every step and then calls a clamped framing
# moved.
SOLVED_FIELDS = ("VDS_HSCALE", "VDS_VSCALE",
                 "IF_HB_SP2", "IF_HB_ST2", "IF_VB_SP", "IF_VB_ST",
                 "VDS_HB_SP", "VDS_HB_ST", "VDS_VB_SP", "VDS_VB_ST",
                 "VDS_DIS_HB_SP", "VDS_DIS_HB_ST",
                 "VDS_DIS_VB_SP", "VDS_DIS_VB_ST")


def solved(registers):
    """Just the solver's outputs out of a snapshot."""
    if registers is None:
        return None
    return {name: registers.get(name) for name in SOLVED_FIELDS}


def wait_until_settled(snapshot, timeout=8.0, interval=0.4, clock=time.monotonic,
                       sleep=time.sleep):
    """The geometry once two consecutive reads agree, or the last read at timeout.

    Unfrozen, a read can land between the engine's write and the sync watcher's
    answer to it, so a single read after a press can be either. A fixed sleep
    would be shorter than the firmware some days and longer than necessary on
    all the others; a unit that never settles is a finding, not a reason to hang
    at the bench with no reading, so the last read comes back regardless.
    """
    deadline = clock() + timeout
    previous = None
    while clock() < deadline:
        current = snapshot()
        if previous is not None and solved(current) == solved(previous):
            return current
        if previous is not None:
            sleep(interval)
        previous = current
    return previous


def verdict_for(key):
    """The verdict a keypress means, or None if it is not a verdict at all."""
    return VERDICT_KEYS.get(key)


def next_number(rows, prefix):
    """The first free `prefix-NN` number, so a second run cannot reuse a label.

    Two rows sharing a label are two different scales recorded as one reading,
    which is the kind of damage that is invisible until the analysis disagrees
    with the pictures.
    """
    used = []
    for row in rows:
        label = str(row.get("label", ""))
        head, _, tail = label.rpartition("-")
        if head == prefix and tail.isdigit():
            used.append(int(tail))
    return max(used) + 1 if used else 1


class Sweep:
    """One consecutive walk of the framing, a measurement and a verdict a step.

    Takes the three things it does to the world as callables -- apply the zoom,
    measure, record a row -- so the whole state machine runs without a unit.
    """

    def __init__(self, apply_zoom, snapshot, record, zoom, step,
                 prefix="dense", first=1, axis="h", restore_to=None):
        self._apply = apply_zoom
        self._snapshot = snapshot
        self._record = record
        # Where the sweep BEGINS and where the picture belongs are different
        # questions once --start exists: restoring to the start point would
        # leave the unit somewhere it had never been.
        self._origin = zoom if restore_to is None else restore_to
        self.zoom = zoom
        self.step_units = step
        self.prefix = prefix
        self.number = first
        self.axis = axis
        self.pending = None
        self.moved = None
        self._previous = None

    def step(self):
        """One unit further along the sweep, measured where it lands."""
        return self._move(self.step_units)

    def back(self):
        """One unit back, and MEASURED again rather than remembered.

        `end-1` came out register-identical to `onset-1`, which is a finding
        about the fault being free of hysteresis only because both ends were
        read off the unit. Reusing the earlier snapshot would make it true by
        construction.
        """
        return self._move(-self.step_units)

    def remeasure(self):
        """The same zoom, read again -- for a step judged while still settling."""
        return self._measure()

    def _move(self, units):
        if self._previous is None:
            # The baseline for "did the unit accept the press", taken once.
            self._previous = self._snapshot()
        self.zoom += units
        # The engine clamps, so the zoom is where it LANDED rather than where it
        # was sent -- otherwise the sweep walks a counter the unit is not on.
        _, self.zoom = self._apply(self.zoom)
        return self._measure()

    def _measure(self):
        registers = self._snapshot()
        self.moved = solved(registers) != solved(self._previous)
        self._previous = registers
        self.pending = characterise.observe(registers.get, "", "")
        self.pending["framing"] = {"axis": self.axis, "zoom": self.zoom}
        return self.pending

    def keep(self, verdict, note=""):
        """Record the step under judgement. Labels run in recording order."""
        row = self.pending
        row["label"] = f"{self.prefix}-{self.number:02d}"
        row["verdict"] = verdict
        row["note"] = note
        self._record(row)
        self.number += 1
        self.pending = None
        return row

    def restore(self):
        """Back to the framing the sweep started from."""
        _, self.zoom = self._apply(self._origin)


# --- the unit ---------------------------------------------------------------


def segments_for(fields, regs):
    """Which segments have to be read to cover `fields`."""
    return sorted({regs[name]["seg"] for name in fields if name in regs})


def snapshotter(host, regs, fields=characterise.FIELDS):
    """A snapshot() reading each segment ONCE, not each field once.

    Thirty fields is thirty HTTP round trips a step, and a sweep is dozens of
    steps with somebody waiting at the screen for each. Whole segments also read
    the co-varying registers simultaneously, which is what makes the row a state
    rather than a sequence of samples.
    """
    from gbs_unit import field_from, read_segment

    def snapshot():
        banks = {}
        for segment in segments_for(fields, regs):
            banks[segment] = read_segment(host, segment) or {}
        values = {}
        for name in fields:
            entry = regs.get(name)
            values[name] = None if entry is None else field_from(
                banks.get(entry["seg"], {}), entry["reg"], entry["off"],
                entry["width"])
        return values

    return snapshot


def applier(host, axis, snapshot, settle=8.0, interval=0.4):
    """Walk the zoom to a value through the pads, then wait for the geometry to
    stop moving. Returns the settled state and where the zoom actually landed --
    the engine clamps, so a swept value is not always reachable."""
    from gbs_unit import framing_to

    def apply(value):
        landed = framing_to(host, f"z{axis}", value)
        state = wait_until_settled(snapshot, timeout=settle, interval=interval)
        return state, value if landed is None else landed[f"z{axis}"]

    return apply


# --- the bench loop ---------------------------------------------------------


def transitions(rows):
    """The rows either side of every verdict change, and the two ends.

    A 342 press run printed as its last forty rows is forty identical lines
    saying clean -- which is exactly what a sweep that left the fault region at
    press eleven looks like from the tail, and it reads as though nothing was
    ever found. The boundaries are the finding; the interiors are one fact
    each.
    """
    kept = []
    for index, row in enumerate(rows):
        first_or_last = index in (0, len(rows) - 1)
        changed = index > 0 and row["verdict"] != rows[index - 1]["verdict"]
        next_changes = (index + 1 < len(rows)
                        and rows[index + 1]["verdict"] != row["verdict"])
        if first_or_last or changed or next_changes:
            kept.append(row)
    return kept


def read_key(prompt):
    """One keypress, without an ENTER after it -- ENTER is itself a verdict.

    Falls back to a whole line where stdin is not a terminal, so the loop can be
    driven from a pipe.
    """
    sys.stdout.write(prompt)
    sys.stdout.flush()
    if not sys.stdin.isatty():
        line = sys.stdin.readline()
        if line == "":
            return "q"
        return line.rstrip("\r\n")[:1]
    import termios
    import tty
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        key = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
    if key == "\x03":       # ctrl-c, which raw mode swallows
        raise KeyboardInterrupt
    sys.stdout.write("\r\n")
    return key


def read_line(prompt):
    sys.stdout.write(prompt)
    sys.stdout.flush()
    return sys.stdin.readline().rstrip("\r\n")


AXIS_FIELDS = {
    "h": ("HSCALE", "VDS_HSCALE", "IF_HB_SP2", "IF_HB_ST2"),
    "v": ("VSCALE", "VDS_VSCALE", "IF_VB_SP", "IF_VB_ST"),
}


def describe(row):
    """The one line that goes on screen beside the picture being judged.

    Of the axis being SWEPT. A vertical run reporting HSCALE and a horizontal
    capture would be reporting the axis that was held still.
    """
    r = row["registers"]
    name, scale_field, start_field, stop_field = AXIS_FIELDS[row["framing"]["axis"]]
    start, stop = r.get(start_field), r.get(stop_field)
    scale = r.get(scale_field)
    width = None if None in (start, stop) else stop - start
    produced = "-" if not (width and scale) else f"{width * 1024.0 / scale:.2f}"
    return (f"zoom {row['framing']['zoom']:>5}  {name} {scale}  "
            f"capture {start}..{stop}  width {width}  produced {produced}")


HELP = """
  ENTER  clear
  t      torn
  a      almost -- some corruption, but not the full fault
  b      back one step, and measure it again
  r      measure this step again, if it was judged mid-settle
  n      type a note for this step, then judge it
  q      quit, and put the framing back where it started
"""


def run(sweep, key_source=read_key, note_source=read_line, out=print):
    """Step, show, judge, repeat. Returns the number of rows recorded."""
    recorded = 0
    note = ""
    row = sweep.step()
    while True:
        out("  " + describe(row))
        if sweep.moved is False:
            out("  the geometry did not move -- the framing is clamped, and "
                "every further step here is the same reading")
        key = key_source("  torn (t) / clear (ENTER) ? ")
        if key in ("q", "Q"):
            break
        if key == "?":
            out(HELP)
            continue
        if key == "n":
            note = note_source("  note: ")
            continue
        if key == "r":
            row = sweep.remeasure()
            continue
        if key == "b":
            row = sweep.back()
            note = ""
            continue
        verdict = verdict_for(key)
        if verdict is None:
            out(f"  {key!r} is not a verdict. ? for the keys")
            continue
        kept = sweep.keep(verdict, note)
        recorded += 1
        note = ""
        out(f"  recorded {kept['label']} {verdict}")
        row = sweep.step()
    return recorded


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", required=True)
    parser.add_argument("--axis", choices=("h", "v"), default="h",
                        help="which zoom to step (default h)")
    parser.add_argument("--step", type=int, default=-1,
                        help="input units per press; negative widens the "
                             "capture, which is the direction the last sweep "
                             "ran (default -1)")
    parser.add_argument("--prefix", default="dense",
                        help="label prefix; numbering continues past the index")
    parser.add_argument("--start", type=int,
                        help="zoom to jump to before the first press. The "
                             "sweep begins there; q still restores the framing "
                             "the unit was actually on")
    parser.add_argument("--index", default=characterise.DEFAULT_INDEX)
    parser.add_argument("--settle", type=float, default=8.0)
    parser.add_argument("--no-restore", action="store_true",
                        help="leave the framing where the sweep ended")
    args = parser.parse_args(argv)

    from gbs_unit import get_json

    with open(os.path.join(HERE, "tv5725_registers.json")) as f:
        regs = json.load(f)

    status, framing = get_json(args.host, "/geometry")
    if status != 200 or not framing:
        print(f"{args.host} did not answer /geometry (status {status})")
        return 1
    zoom = framing[f"z{args.axis}"]

    snapshot = snapshotter(args.host, regs)
    apply_zoom = applier(args.host, args.axis, snapshot, settle=args.settle)
    rows = characterise.load(args.index)

    start = zoom if args.start is None else args.start
    if start != zoom:
        print(f"  walking zoom {args.axis} {zoom} -> {start}")
        _, start = apply_zoom(start)

    sweep = Sweep(apply_zoom, snapshot,
                  lambda row: characterise.append(args.index, row),
                  zoom=start, step=args.step, axis=args.axis,
                  prefix=args.prefix, restore_to=zoom,
                  first=next_number(rows, args.prefix))

    print(f"\n  {args.host}, zoom {args.axis} from {start}, {args.step:+d} a press"
          f"\n  labels {args.prefix}-{sweep.number:02d} onward, into {args.index}")
    print(HELP)
    try:
        recorded = run(sweep)
    except KeyboardInterrupt:
        recorded = None
        print("\n  interrupted")
    finally:
        if not args.no_restore:
            sweep.restore()
            print(f"  framing restored to zoom {args.axis} {sweep.zoom}")

    print()
    print(characterise.table(transitions(characterise.load(args.index))))
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
