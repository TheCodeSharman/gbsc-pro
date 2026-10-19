#!/usr/bin/env python3
"""Score a coast setting against the count collapse in INTERLEAVED legs.

    python3 tools/gbsc-pro-hwtest/collapse_legs.py --host <ip> --seconds 300 \
        default coast:0/2
    python3 tools/gbsc-pro-hwtest/collapse_legs.py --host <ip> --seconds 90 \
        default SP_CS_P_SWAP=0,SP_HS2PLL_INV_REG=0

Needs a build carrying `Tv5725::SamplingLog` -- `flash-ota … GBS_SAMPLING_LOG=1`.

**A BLOCK OF LEGS AT ONE VALUE IS NOT AN A/B.** The collapse runs 0 to 10 per
900 s on ONE state, which covers the whole range any candidate could move, so a
run of legs at one value scored against controls taken outside the run produces
a confident wrong answer -- `ADC_SOGCTRL` was published as an elevenfold lever
on exactly that error. This walks the states in rotation and prints every leg,
and prints no pooled figure at all.

Each leg reports the collapse SPLIT BY WHERE THE COUNT RESTARTED, because the
two bands are two faults and a pooled count reads a lever on one as a lever on
the other. It reports the separator's pulse dropout beside them, which is five
times more frequent and so the cheaper to score -- but its own within-state
spread is 28 against 63 per 900 s, so it resolves a tenfold change and not a
twofold one.

**LEGS ARE COMPARABLE ONLY INSIDE ONE SESSION.** Within a session the rate is
steady and does not ramp after a re-solve; between sessions it moves about
threefold.

A state is a comma-separated list of `coast:PRE/POST`, `dlt:VALUE` and
`NAME=VALUE`, or the word `default`. The coast and the separator's width
threshold go through `/coast` and `/sepwidth`, because those survive the
re-solve the engine runs behind them; a named field is poked, so a field the
engine rewrites per pass cannot be held that way -- the leg prints every
watched field back, so one that moved says so.

**THE ASSAY IS `dlt:48`, AND A CANDIDATE IS SCREENED ON TOP OF IT.** At the
engine's own 192 the mid-picture fault runs 10 per 900 s, which no leg this
session can resolve; at 48 it runs 187, which a 60 s leg settles. So a
candidate is `dlt:48,CANDIDATE` against a `dlt:48` control, and anything that
works is confirmed back at `dlt:192`.

**SCREEN FOR A LARGE EFFECT IN A SHORT LEG RATHER THAN MEASURING A SMALL ONE IN
A LONG ONE.** At one collapse every 90 to 150 s a state twenty times worse
holds eight to thirteen in 90 s, which one leg settles; a state twice as bad
needs more legs than the fault is worth. What is wanted is a state where the
fault is frequent, not a precise figure for one that is not.

docs/investigations/the-large-composite-jump-is-a-torn-frame.md
"""
import argparse
import json
import sys
import time
import urllib.request

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from gbs_unit import read_fields, write_named                 # noqa: E402
from sync_dropouts import COLLAPSE_BELOW, DROPOUT_BELOW       # noqa: E402
from sync_dropouts import HLOW, VTOTAL, capture, events, rows_of  # noqa: E402

WATCHED = ["SP_PRE_COAST", "SP_POST_COAST", "SP_CS_P_SWAP", "SP_HS2PLL_INV_REG",
           "SP_DLT_REG", "SP_JITTER_SYNC", "SP_H_PULSE_IGNOR", "ADC_SOGCTRL",
           "SP_CS_CLP_ST", "PLLAD_MD"]

# The coast reaches the chip from loop(), and the engine re-solves behind it.
SETTLE_S = 8.0

# THE RESTART VALUE SPLITS TWO FAULTS AND A POOLED COUNT HIDES IT. A restart at
# or past this is the vertical detected inside the source's vertical blanking,
# a few lines early; below it the vertical arrives mid picture, 130 to 200 lines
# early, and only that one tears the bench source. Reverting the separator's
# edge reference multiplies the first by 160 and leaves the second alone, which
# a count of every reading under 300 reads as a tenfold lever on the fault.
# docs/investigations/the-large-composite-jump-is-a-torn-frame.md
IN_BLANKING_FROM = 240


def ask(host, path):
    return urllib.request.urlopen(f"http://{host}{path}", timeout=5).read().decode()


def tokens_of(state):
    return [t for t in state.split(",") if t and t != "default"]


def fields_of(state):
    return dict(t.split("=") for t in tokens_of(state) if "=" in t)


def held_of(state, prefix):
    """What one of the two engine-held quantities is asked for, or None for the
    engine's own value."""
    for token in tokens_of(state):
        if token.startswith(prefix):
            return token[len(prefix):]
    return None


def apply_state(host, state, resting):
    """EVERY LEG STARTS FROM THE RESTING VALUES, so a field one state pokes is
    back where the engine left it in the next.

    THE ENGINE-HELD REQUESTS GO FIRST AND ARE WAITED OUT, because each is queued
    for loop() and the re-solve behind it runs `applyForSyncType()`, which
    rewrites the edge reference and the pulse ignore. A poke written before that
    lands is silently undone -- which read as the edge pair not being a lever at
    all.
    """
    coast = held_of(state, "coast:")
    if coast and coast != "default":
        pre, post = coast.split("/")
        ask(host, f"/coast?pre={pre}&post={post}")
    else:
        ask(host, "/coast?clear")

    dlt = held_of(state, "dlt:")
    ask(host, f"/sepwidth?dlt={dlt}" if dlt else "/sepwidth?clear")
    time.sleep(SETTLE_S)

    for name, value in resting.items():
        write_named(host, name, value)
    asked = {name: int(value, 0) for name, value in fields_of(state).items()}
    for _ in range(4):
        for name, value in asked.items():
            write_named(host, name, value)
        time.sleep(1.0)
        if not asked or read_fields(host, sorted(asked)) == asked:
            break
    time.sleep(2.0)


def geometry(host):
    try:
        return json.loads(ask(host, "/geometry"))
    except Exception:
        return {}


def leg(host, state, seconds, out, resting):
    apply_state(host, state, resting)
    before = read_fields(host, WATCHED)
    acquired = geometry(host).get("state")
    lines = capture(host, 1, int(seconds * 1000), out=out)
    after = read_fields(host, WATCHED)
    rows, done = rows_of(lines)
    if done is None:
        print(f"  {state:34s} the run did not report its own sample count")
        return
    samples, span = done
    secs = span / 1000.0
    collapses = events([r for r in rows if r[VTOTAL] < COLLAPSE_BELOW])
    dropouts = events([r for r in rows if r[HLOW] < DROPOUT_BELOW])
    mid = [c for c in collapses if c[0][VTOTAL] < IN_BLANKING_FROM]
    blanking = [c for c in collapses if c[0][VTOTAL] >= IN_BLANKING_FROM]
    print(f"  {state:34s} {acquired:9s}"
          f"  {secs:4.0f} s  {samples/secs:4.0f} samples/s"
          f"   mid picture {len(mid):3d} ({900*len(mid)/secs:5.1f}/900s)"
          f"   in blanking {len(blanking):3d} ({900*len(blanking)/secs:5.1f}/900s)"
          f"   dropouts {len(dropouts):4d} ({900*len(dropouts)/secs:5.1f}/900s)")

    # EVERY WATCHED FIELD, EVERY LEG. A leg that only says whether a field MOVED
    # cannot say whether the poke landed at all, and the engine re-derives the
    # coast behind a clear -- so a leg whose state is not printed is a leg whose
    # state is not known.
    print("           " + "  ".join(f"{k}={before[k]}" for k in WATCHED))
    asked = {k: int(v, 0) for k, v in fields_of(state).items()}
    dlt = held_of(state, "dlt:")
    if dlt:
        asked["SP_DLT_REG"] = int(dlt, 0)
    missed = [k for k, v in asked.items() if before.get(k, v) != v]
    moved = [k for k in WATCHED if before[k] != after[k]]
    if missed:
        print(f"           THE POKE DID NOT LAND: {','.join(missed)}")
    if moved:
        print(f"           MOVED DURING THE LEG: {','.join(moved)}")
    if collapses:
        where = {}
        for event in collapses:
            where[event[0][VTOTAL]] = where.get(event[0][VTOTAL], 0) + 1
        print("           restarts: " + "  ".join(
            f"{k}:{v}" for k, v in sorted(where.items())))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--seconds", type=float, default=300.0)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--out-prefix")
    parser.add_argument("state", nargs="+", help="PRE/POST, or 'default'")
    args = parser.parse_args()

    wanted = set()
    for state in args.state:
        wanted |= set(fields_of(state))
    resting = read_fields(args.host, sorted(wanted)) if wanted else {}
    if resting:
        print("resting values: " + "  ".join(
            f"{k}={v}" for k, v in sorted(resting.items())))

    for round_ in range(args.rounds):
        print(f"round {round_ + 1}")
        for state in args.state:
            tag = state.replace("/", "-").replace(",", "+").replace("=", "")
            out = (f"{args.out_prefix}-{tag}-{round_}.log"
                   if args.out_prefix else None)
            leg(args.host, state, args.seconds, out, resting)
    apply_state(args.host, "default", resting)
    print("the engine's own coast and threshold and the resting fields are back")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
