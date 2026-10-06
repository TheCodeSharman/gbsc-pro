#!/usr/bin/env python3
"""The acceptance reading for a configure change: did anything move?

    ./configure_oracle.py --host 192.168.88.108 --path vga-sync0 --save BASE.json
    ./configure_oracle.py --host 192.168.88.108 --path vga-sync0 --against BASE.json

One reading is the nine sync-processor fields a configure act places, the
`/geometry` the engine solved, the source's own measurement, and
`picstate.score()` on the emitted frame. `--against` diffs it and exits non-zero
on a FAIL.

**WHAT THE ENGINE COMPUTED IS AN ORACLE AND WHAT IT MEASURED IS CONTEXT.** A
solved value moving is a failure and exits 1; a measured one is a note, because
it is the source. What is placed FROM a measurement inherits its tolerance --
one line of the held count, which is what a count alternating by one costs.

Two pairs are excluded by construction, and including either is how a clean
change reads as a regression -- `docs/known-issues.md`:

  SP_PRE_COAST/SP_POST_COAST  the recovery ladder's, through
                              SyncRecovery::CoastWindow, so a post-acquisition
                              reading says whether the ladder ran
  SP_HS_POL_ATO/SP_VS_POL_ATO inherited: only the separate-sync branch writes
                              them, so their value on a csync source records
                              where the unit has been

`docs/chip-initialisation.md` step 8 is what this reading gates.
"""

import argparse
import collections
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit

FAIL = "FAIL"
NOTE = "NOTE"

Difference = collections.namedtuple("Difference", "group key was now severity")

# The sync-processor fields a configure act places: the clamp, the coast window
# and the sync separator.
ORACLE = (
    "SP_CLAMP_MANUAL", "SP_CLP_SRC_SEL", "SP_NO_CLAMP_REG",
    "SP_H_CST_ST", "SP_H_CST_SP", "SP_HCST_AUTO_EN",
    "SP_CS_CLP_ST", "SP_CS_CLP_SP", "SP_EXT_SYNC_SEL",
)

# Read beside the oracle so a difference in it has an explanation: SP_H_CST_SP
# follows the measured line rate, and the sync processor counts in ADC clocks so
# STATUS_SYNC_PROC_HTOTAL echoes the divider.
CONTEXT = ("PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL",
           "STATUS_SYNC_PROC_VTOTAL", "SP_SOG_MODE")

GEOMETRY = ("oh", "eh", "ov", "ev", "ch", "cv", "fh", "fv",
            "poh", "peh", "pov", "pev", "aspect", "shaped", "present", "state",
            "lineRateHz", "lowLineRate")

PICTURE = ("left", "right", "top", "bottom", "spread", "luma", "flat")

# The framing the user tuned, in ten-thousandths of the capturable region. It is
# re-adopted from the capture on every solve rather than computed from the
# source, so it rounds whenever that region moves -- 673/9327 against one 480i
# landing and 670/9330 against the other. A framing actually LOST moves the
# window's units beside it, and those are the oracle.
FRAMING_PROPORTIONS = ("poh", "peh", "pov", "pev")

# Per group, the keys the engine COMPUTED. Everything else in a group is
# something it measured, or something the link placed, and moving is a question
# rather than an answer.
COMPUTED = {
    "oracle": set(ORACLE),
    "context": {"PLLAD_MD"},
    "geometry": set(GEOMETRY) - {"lineRateHz", "lowLineRate"}
               - set(FRAMING_PROPORTIONS),
    "picture": {"flat"},
}

# picstate scores the emitted frame off the dongle, so there is no room light in
# it: a boot pair held to a tenth of a level on both numbers.
TOLERANCE = {"spread": 1.0, "luma": 1.0}

# Placed from the measured line rate, which is the held count times the field
# rate, so one line of that count moves each of them -- 312 lines put
# SP_H_CST_SP at 1672 and 313 at 1666. A count alternating by one is the same
# source (SteadyRun::agree()), so that much has to be absorbed. The spare unit
# is the two floors in the chain, and the clamp placement keeps a withinOneOf
# guard of its own.
RATE_DERIVED = ("PLLAD_MD", "SP_H_CST_ST", "SP_H_CST_SP",
                "SP_CS_CLP_ST", "SP_CS_CLP_SP")

# The capture window's own units, placed from the HELD count -- which lands on
# either member of an alternating pair and cannot be read back, because
# STATUS_SYNC_PROC_VTOTAL samples the register rather than reporting what the
# solve used. Measured on the 480i Wii: cv 520 / ev 485 on one landing and 522 /
# 487 on the other, two of three acquisitions of one source on one image, with
# the geometry steady at whichever it landed on through 48 reads. One line of the
# count is the tolerance, the same shape as the rate-derived group above.
COUNT_DERIVED = ("ov", "ev", "cv", "fv")

# A slack of one held line, whichever reason the key has for needing it.
PER_LINE = RATE_DERIVED + COUNT_DERIVED

# Read and printed, never compared. The docstring says why.
EXCLUDED = ("SP_PRE_COAST", "SP_POST_COAST", "SP_HS_POL_ATO", "SP_VS_POL_ATO")

# 480i alternates its count by one and still acquires, so a path is only a
# different path beyond that.
PATH_VTOTAL_SLACK = 1

# One device open per burst: the warm-up residue can reach the returned frames,
# and re-opening costs forty discarded frames again.
CAPTURE_BURST = 12
CAPTURE_SETTLE_S = 15.0
CAPTURE_POLL_S = 1.0


def _moved(key, was, now, lines):
    if key in PER_LINE and lines:
        slack = abs(was) / float(lines) + 1.0
    else:
        slack = TOLERANCE.get(key)
    if slack is None:
        return was != now
    return abs(float(was) - float(now)) > slack


def compare(was, now):
    """Every difference between two readings, each classified FAIL or NOTE."""
    lines = was["context"].get("STATUS_SYNC_PROC_VTOTAL")
    out = []
    for group, keys in (("oracle", ORACLE), ("context", CONTEXT),
                        ("geometry", GEOMETRY), ("picture", PICTURE)):
        for key in keys:
            before, after = was[group].get(key), now[group].get(key)
            if before is None or after is None or not _moved(key, before, after, lines):
                continue
            severity = FAIL if key in COMPUTED[group] else NOTE
            out.append(Difference(group, key, before, after, severity))
    return out


def same_path(was, now):
    """Do two readings come from the same bench path at all?

    A comparison across a sync type or a source mode is void, not a diff: the
    oracle is per path, and reading one against another is how a correct change
    reads as a regression.
    """
    if was["context"].get("SP_SOG_MODE") != now["context"].get("SP_SOG_MODE"):
        return False
    lines = (was["context"].get("STATUS_SYNC_PROC_VTOTAL"),
             now["context"].get("STATUS_SYNC_PROC_VTOTAL"))
    if None in lines:
        return False
    return abs(lines[0] - lines[1]) <= PATH_VTOTAL_SLACK


def merged(held, label, taken):
    """The baseline file with this path replaced and every other one kept."""
    return dict(held, **{label: taken})


def take(host, capture=True, limit_s=90.0):
    """One reading, with the source acquired and holding still first.

    A reading taken mid-solve pairs a capture window from one pass with a sync
    window from the next, which invents differences.
    """
    settled = gbs_unit.acquired_and_settled(host, limit_s=limit_s, holds=3)
    seen = gbs_unit.get_json(host, "/geometry")[1] or {}
    oracle = gbs_unit.read_fields(host, list(ORACLE))
    context = gbs_unit.read_fields(host, list(CONTEXT))
    excluded = gbs_unit.read_fields(host, list(EXCLUDED))
    return {
        "settled": settled,
        "oracle": oracle or {},
        "context": context or {},
        "excluded": excluded or {},
        "geometry": {key: seen.get(key) for key in GEOMETRY},
        "picture": picture_of() if capture else {},
    }


def settled_picture(grab, pause=time.sleep, limit_s=CAPTURE_SETTLE_S):
    """The first scored frame of a burst that is not a flat field.

    The dongle delivers black inside a settled stream as well as for seconds
    after the link re-acquires, with the board's output state identical
    throughout, so one grab is never evidence either way.
    docs/bench-output-capture.md
    """
    deadline = time.monotonic() + limit_s
    last = {"flat": True, "spread": 0.0, "luma": 0.0,
            "left": 0, "right": 0, "top": 0, "bottom": 0}
    while True:
        for scored in grab():
            if not scored["flat"]:
                return scored
            last = scored
        if time.monotonic() >= deadline:
            return last
        pause(CAPTURE_POLL_S)


def picture_of(limit_s=CAPTURE_SETTLE_S):
    """settled_picture off the dongle, or why it could not be read."""
    try:
        import hdmi_capture
        import picstate

        device = hdmi_capture.device()

        def grab():
            return [picstate.score(frame)
                    for frame in hdmi_capture.frames(CAPTURE_BURST, device)]

        scored = settled_picture(grab, limit_s=limit_s)
        return {key: scored[key] for key in PICTURE}
    except SystemExit as missing:
        return {"error": str(missing)}
    except Exception as failed:                                 # noqa: BLE001
        return {"error": f"{type(failed).__name__}: {failed}"}


def report(taken):
    lines = [
        "  oracle   " + "  ".join(f"{k}={taken['oracle'].get(k)}" for k in ORACLE),
        "  context  " + "  ".join(f"{k}={taken['context'].get(k)}" for k in CONTEXT),
        "  geometry " + "  ".join(f"{k}={taken['geometry'].get(k)}" for k in GEOMETRY),
    ]
    shown = taken.get("picture") or {}
    if "error" in shown:
        lines.append(f"  picture  no capture: {shown['error']}")
    elif shown:
        lines.append(
            f"  picture  margins {shown['left']}/{shown['right']}"
            f"/{shown['top']}/{shown['bottom']}"
            f"  spread {shown['spread']:.1f}  luma {shown['luma']:.1f}"
            f"  {'FLAT FIELD' if shown['flat'] else 'picture'}"
        )
    lines.append("  not compared  "
                 + "  ".join(f"{k}={taken['excluded'].get(k)}" for k in EXCLUDED))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--host", required=True)
    parser.add_argument("--path", required=True,
                        help="which bench path this reading is of, e.g. vga-sync0. "
                             "The oracle is per path and comparing across two is void")
    parser.add_argument("--save", metavar="FILE",
                        help="store this reading under --path, keeping every other path")
    parser.add_argument("--against", metavar="FILE",
                        help="diff this reading against the one stored under --path")
    parser.add_argument("--no-capture", action="store_true",
                        help="skip the HDMI capture; the picture is then not compared")
    parser.add_argument("--limit-s", type=float, default=90.0)
    args = parser.parse_args(argv)

    taken = take(args.host, capture=not args.no_capture, limit_s=args.limit_s)
    print(f"\n  {args.path}   settled={taken['settled']}\n")
    print(report(taken))

    if not taken["oracle"] or not taken["context"]:
        print("\n  INCOMPLETE: a field batch did not answer -- loop() is busy or "
              "the unit is gone. Nothing compared.")
        return 2
    if not taken["settled"]:
        print("\n  NOT SETTLED: the engine is still re-solving, so this reading "
              "pairs one pass with the next. Nothing compared.")
        return 2

    status = 0
    if args.against:
        with open(args.against) as handle:
            held = json.load(handle)
        was = held.get(args.path)
        if was is None:
            print(f"\n  NO BASELINE for {args.path} in {args.against}. "
                  f"Held: {', '.join(sorted(held)) or 'nothing'}")
            return 2
        if not same_path(was, taken):
            print(f"\n  WRONG PATH: the baseline for {args.path} was taken at "
                  f"SOG={was['context'].get('SP_SOG_MODE')} / "
                  f"{was['context'].get('STATUS_SYNC_PROC_VTOTAL')} lines, this "
                  f"reading at SOG={taken['context'].get('SP_SOG_MODE')} / "
                  f"{taken['context'].get('STATUS_SYNC_PROC_VTOTAL')}. Not compared.")
            return 2
        found = compare(was, taken)
        print("")
        for difference in found:
            print(f"  [{difference.severity}] {difference.group:8s} "
                  f"{difference.key:26s} {difference.was} -> {difference.now}")
        if not found:
            print("  no differences")
        failed = [d for d in found if d.severity == FAIL]
        status = 1 if failed else 0
        print(f"\n  {len(failed)} FAIL, {len(found) - len(failed)} NOTE")
        if failed:
            print("  A computed value moved. Either the change is wrong or the "
                  "baseline is stale -- a NOTE is the source or the link.")

    if args.save:
        held = {}
        if os.path.exists(args.save):
            with open(args.save) as handle:
                held = json.load(handle)
        with open(args.save, "w") as handle:
            json.dump(merged(held, args.path, taken), handle,
                      indent=1, sort_keys=True)
        print(f"\n  wrote {args.path} to {args.save}")

    return status


if __name__ == "__main__":
    sys.exit(main())
