#!/usr/bin/env python3
"""What a shear mark is made of, and the rule that predicts one.

The horizontal zoom shears the picture on about half its steps. Every creep tool
that marks a frame clean or corrupt shares this vocabulary: the registers a mark
is taken at, the arithmetic they mean, and the one predicate under test.

docs/known-issues.md carries what is measured and what is still open.
"""
import datetime
import json
import math
import os
import time

import setfield
from gbs_unit import get_json, read_fields

START_CONST = 55.0
START_PER_MAG = 25.0

# Where the memory window's near edge is pinned. Below this the solve wants a
# windowStop the part cannot write, the register clamps, and the picture is
# broken for a reason of its own that the width rule does not govern.
WINDOW_STOP_MIN = 8

# Everything the arithmetic behind a mark uses, read in ONE pass. A mark that is
# only a value and a verdict cannot be read again, and a dump taken afterwards is
# the state at dump time rather than at mark time.
STATE = ["VDS_HB_SP", "VDS_HB_ST", "VDS_DIS_HB_SP", "VDS_DIS_HB_ST",
         "VDS_HSCALE", "VDS_VSCALE", "VDS_HSYNC_RST", "VDS_VSYNC_RST",
         "VDS_HS_ST", "VDS_HS_SP", "IF_HB_SP2", "IF_HB_ST2",
         "PB_FETCH_NUM", "PB_CAP_OFFSET", "PLLAD_MD", "IF_HSYNC_RST"]


def derive(state):
    """What the registers mean, from one pass and no other source."""
    scale = state.get("VDS_HSCALE")
    magnification = 1024.0 / scale if scale else None
    capture = None
    if state.get("IF_HB_ST2") is not None and state.get("IF_HB_SP2") is not None:
        capture = state["IF_HB_ST2"] - state["IF_HB_SP2"]

    offset = (None if magnification is None
              else START_CONST + START_PER_MAG * magnification)
    origin = (None if offset is None or state.get("VDS_HB_SP") is None
              else state["VDS_HB_SP"] + offset)
    return {
        "capture": capture,
        "magnification": magnification,
        "produced": None if None in (capture, magnification) else capture * magnification,
        "offset": offset,
        "origin": origin,
        "width": (None if None in (state.get("VDS_HB_ST"), state.get("VDS_HB_SP"))
                  else state["VDS_HB_ST"] - state["VDS_HB_SP"]),
        # How far the picture's corner sits from where the write actually
        # starts. This is the rounding residual while the solve owns the
        # register, and it is what a jog moves off.
        "phase": (None if origin is None or state.get("VDS_DIS_HB_SP") is None
                  else state["VDS_DIS_HB_SP"] - origin),
    }


def clamped(state):
    """Whether the near edge is pinned at its floor.

    There the picture is broken on 49 of 51 marks whatever the width, so a mark
    taken here says nothing about the width rule either way.
    """
    return state.get("VDS_HB_SP") is not None and state["VDS_HB_SP"] <= WINDOW_STOP_MIN + 1


def predict(state):
    """What the rule expects of this state, before anyone looks.

    An EVEN memory window, `VDS_HB_ST - VDS_HB_SP`, shears and an odd one is
    clean. Two earlier rules are refuted and must not be reinstated: the parity
    of `VDS_HB_SP` alone, and `VDS_HB_SP + floor(produced)`. docs/known-issues.md
    """
    width = derive(state)["width"]
    if width is None:
        return "?"
    return "corrupt" if width % 2 == 0 else "clean"


def margin_targets(state, delta):
    """Where a margin of `delta` off the write floor puts both near edges.

    The window start and the corner move together: moving `VDS_HB_SP` alone
    opens the display window before the write starts, which shows unwritten
    memory down the left and is an artefact of its own. `delta` must be even,
    because it changes the memory window's width and an even width shears.

    None where the step would go below the floor, which the part clamps.
    """
    if delta % 2 or state.get("VDS_HB_SP") is None:
        return None
    wanted = state["VDS_HB_SP"] + delta
    if wanted < WINDOW_STOP_MIN:
        return None
    return {"VDS_HB_SP": wanted,
            "VDS_DIS_HB_SP": state["VDS_DIS_HB_SP"] + delta}


def floor_scale(state):
    """What the write-floor rule expects of this state, before anyone looks.

    On the write floor the picture is clean at a `VDS_HSCALE` that is a multiple
    of 64 and corrupt at every other, measured from 334 down. That is also
    exactly the set dividing 25600 within the swept band, so the band cannot
    tell the two readings apart; this one is implemented because it has a
    mechanism -- the interpolation phase repeats every `1024 / gcd(scale, 1024)`
    output pixels, 16 at 320 against 1024 at 321. They separate at 384 and 448,
    multiples of 64 that do not divide 25600, and at 400, which divides 25600
    and is not a multiple of 64.

    The zoom cannot reach this regime any more -- Scale::Min stops it -- so this
    rule governs restored framings alone.
    docs/investigations/horizontal-scale-corruption.md

    The corner is refuted and must not be reinstated: `VDS_DIS_HB_SP` was jogged
    four units either side of an exact write origin and the picture stayed clean
    at all 11 marks.
    docs/investigations/horizontal-scale-corruption.md
    """
    scale = state.get("VDS_HSCALE")
    if not scale:
        return "?"
    return "clean" if scale % 64 == 0 else "corrupt"


class Rule:
    """A predicate under test, and which marks it governs.

    Two live rules divide the zoom range between them: the width rule above the
    clamp, the floor scale rule on the write floor. Each is silent where the other
    speaks, so a session scored by the wrong one reads as a refutation that
    never happened.
    """

    def __init__(self, name, predicate, governs):
        self.name = name
        self.predicate = predicate
        self.governs = governs

    def __call__(self, state):
        return self.predicate(state)


WidthRule = Rule("the width rule", predict, lambda state: not clamped(state))
FloorScaleRule = Rule("the floor scale rule", floor_scale, clamped)


def describe(value, state):
    d = derive(state)
    parts = [f"VDS_HB_SP {value}"]
    if d["magnification"]:
        parts.append(f"HSCALE {state['VDS_HSCALE']} (x{d['magnification']:.4f})")
    if d["capture"] is not None:
        parts.append(f"capture {d['capture']}")
    if d["width"] is not None:
        parts.append(f"width {d['width']} {'even' if d['width'] % 2 == 0 else 'odd'}")
    if d["origin"] is not None:
        parts.append(f"origin {d['origin']:.2f}")
    if d["phase"] is not None:
        parts.append(f"phase {d['phase']:+.2f}")
    return "  ".join(parts)


def unsolved(live):
    """Why this state is not one the engine wrote, or None if it is.

    A departure from a state no solve produced measures nothing, and the near and
    far edges agreeing with each OTHER is not enough -- a restore can leave both
    carrying another framing's values while VDS_HSCALE has moved on.
    """
    if live["VDS_HB_ST"] != live["VDS_DIS_HB_ST"]:
        return (f"VDS_HB_ST {live['VDS_HB_ST']} and VDS_DIS_HB_ST "
                f"{live['VDS_DIS_HB_ST']} have come apart.")
    d = derive(live)
    if d["produced"] is None or d["offset"] is None:
        return None
    due = math.floor(live["VDS_HB_SP"] + d["offset"] + d["produced"])
    if abs(due - live["VDS_DIS_HB_ST"]) > 1:
        return (f"the far edge is {live['VDS_DIS_HB_ST']} where this capture and "
                f"scale want {due}: the window belongs to another framing.")
    return None


class Session:
    """The marks, each with the state it was taken at, written as they happen."""

    def __init__(self, host, path, tool="creep_hb_sp", rule=None,
                 judging="unstated"):
        self.host = host
        self.path = path
        self.tool = tool
        # Which artefact the verdict is about. A corrupt verdict on this card
        # is several features at once -- two curves, the wedge, the label
        # text's vertical lines, a line down the right-hand bracket, and more
        # that are simply harder to find -- and marks that do not say which one
        # was watched cannot be separated afterwards. docs/known-issues.md
        self.judging = judging
        self.rule = rule or WidthRule
        self.started = datetime.datetime.now().isoformat(timespec="seconds")
        self.marks = []

    def mark(self, value, verdict, state):
        self.marks.append({"value": value, "verdict": verdict,
                           "judging": self.judging,
                           "predicted": self.rule(state),
                           "clamped": clamped(state),
                           "governed": self.rule.governs(state),
                           "registers": state, "derived": derive(state)})
        self.write()

    def write(self):
        if not self.path:
            return
        directory = os.path.dirname(os.path.abspath(self.path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        with open(self.path, "w") as handle:
            json.dump({"tool": self.tool, "host": self.host,
                       "judging": self.judging,
                       "started": self.started, "marks": self.marks},
                      handle, indent=2)

    def table(self):
        if not self.marks:
            return "no marks taken"
        lines = [f"{'step':>6} {'verdict':>8} {'predicted':>10} {'HSCALE':>6} "
                 f"{'HB_SP':>6} {'width':>6} {'phase':>7}  called"]
        for m in self.marks:
            d = m["derived"]
            called = ("-- not this rule's" if not m.get("governed", not m["clamped"])
                      else "ok" if m["verdict"] == m["predicted"] else "*** REFUTES ***")
            lines.append(
                f"{m['value']:>6} {m['verdict']:>8} {m['predicted']:>10} "
                f"{m['registers'].get('VDS_HSCALE', 0):>6} "
                f"{m['registers'].get('VDS_HB_SP', 0):>6} "
                f"{d['width'] if d['width'] is not None else 0:>6} "
                f"{d['phase'] if d['phase'] is not None else 0:>+7.2f}  {called}")
        return "\n".join(lines)

    def score(self):
        """How the rule did on the marks it governs."""
        judged = [m for m in self.marks
                  if m.get("governed", not m["clamped"])]
        if not judged:
            return (f"no marks {self.rule.name} governs "
                    f"({len(self.marks)} outside it)")
        hits = sum(1 for m in judged if m["verdict"] == m["predicted"])
        skipped = len(self.marks) - len(judged)
        return (f"{self.rule.name} called {hits} of {len(judged)} right"
                + (f", {skipped} marks it does not govern" if skipped else ""))


def freeze(host, on):
    status, payload = get_json(host, f"/freeze?on={1 if on else 0}")
    return status == 200 and isinstance(payload, dict) and payload.get("frozen") is on


def write_field(host, name, value):
    return setfield.apply(host, name, setfield.load_map()[name], value, False)


def settled(host, tries=12, interval=0.25):
    """The state after a re-solve, once two consecutive reads agree.

    A press re-solves, and reading through the change pairs a capture from one
    solve with a window from another.
    """
    previous = None
    for _ in range(tries):
        state = read_fields(host, STATE)
        if state is not None and state == previous:
            return state
        previous = state
        time.sleep(interval)
    return previous
