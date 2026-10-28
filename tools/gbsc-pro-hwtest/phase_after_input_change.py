#!/usr/bin/env python3
"""Which sampling phase an input change leaves behind, over many round trips.

    python3 tools/gbsc-pro-hwtest/phase_after_input_change.py --host <ip> --legs 10

**ONE TRANSITION PROVES NOTHING HERE.** The phase search that runs inside a
transition scores a dithering band the settled source does not have, and
whether its answer survives depends on what the acquired run happened to be
when the transition arrived -- so the reading is a share of legs, not a state.

Each leg selects the other input, waits for the sync processor to count it,
selects the input under test back, waits for its own count, and then waits out
the re-search window before reading `PA_SP_S`. **The count is what is polled,
never `/geometry`'s `state`**, which is stale across an input change and
answers for the source that just went away.

**AN ANSWER IS SCORED BY ABSENCE FROM THE SETTLED BAND, NOT BY MEMBERSHIP OF
THE TRANSIENT ONE.** `--settled-low`..`--settled-high` is where a settled sweep
lands and everything else is displaced, because the transient band's own bounds
cannot cover an answer displaced further than any yet seen -- scored the other
way round, a `PA_SP_S` of 4 against a settled 29..31 reads as good.

Those bounds are the measured profile for the bench RiscPC in pass-through at
`PLLAD_MD` 2038 and belong to that sampling, so pass the profile's own band when
the source or the divider differs.
`docs/investigations/the-sampling-phase-is-searched-before-the-source-settles.md`

**ONE PROCESS MAY DRIVE THE UNIT AT A TIME.** Two runs calling `/input` time
each other's source changes, which reads as an unreliable unit.
"""

import argparse
import sys
import time

import gbs_unit as g

# Below this the sync processor is not following the source at all, so a count
# read under it is no evidence the input arrived. gbs_unit carries the same
# floor for the same reason.
LOCKED_VTOTAL_MIN = g.LOCKED_VTOTAL_MIN


def displaced(phase, settled):
    """Whether a phase is one a settled sweep would not have chosen."""
    low, high = settled
    return not (low <= phase <= high)


def counted(host, want=None, other_than=None, timeout=40.0):
    """The locked count once the sync processor has one, or None.

    **A COUNT ALONE IS NOT EVIDENCE THE INPUT CHANGED.** The register holds the
    count of the source that just went away for seconds after the selection, so
    a wait for any locked count returns at once with the previous input's. That
    is the same trap `/geometry`'s stale `state` is, one register along.
    So the caller says which count to hold out for: `want` the count the
    arriving input must read, `other_than` any count but that one.
    """
    def arrived():
        # An empty reply is loop() being busy inside detection, which is
        # exactly when this is asked -- not a reading of zero.
        seen = g.read_named(host, "STATUS_SYNC_PROC_VTOTAL")
        if seen is None or seen < LOCKED_VTOTAL_MIN:
            return None
        if want is not None and seen != want:
            return None
        if other_than is not None and seen == other_than:
            return None
        return seen

    return g.wait_for(arrived, timeout=timeout, interval=0.25)


def leg(host, under_test, other, baseline, settle_s, timeout, hold_s=None):
    """One round trip, returning what the input under test was left holding."""
    g.select_input(host, other)
    if hold_s is None:
        # WAITING FOR THE OTHER SOURCE TO COUNT HIDES THE FIRST-SWEEP FAULT,
        # because it spends long enough on it for a sweep to have already run.
        # --hold is the short round trip that reproduces it.
        if counted(host, other_than=baseline, timeout=timeout) is None:
            return dict(ok=False, why=f"{other} never counted")
    else:
        time.sleep(hold_s)

    g.select_input(host, under_test)
    count = counted(host, want=baseline, timeout=timeout)
    if count is None:
        return dict(ok=False, why=f"{under_test} never counted")

    # The re-search is on the maintenance cadence, so the answer under test is
    # the one held after its window rather than the one the transition chose.
    time.sleep(settle_s)

    held = g.read_fields(host, ["PA_SP_S", "PA_ADC_S", "PLLAD_MD",
                                "STATUS_SYNC_PROC_VTOTAL"])
    return dict(ok=True, count=count, **held)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--legs", type=int, default=10)
    parser.add_argument("--input", default="vga", help="the input under test")
    parser.add_argument("--other", default="ypbpr",
                        help="the input each leg transitions through")
    parser.add_argument("--settle", type=float, default=22.0,
                        help="seconds after the count before reading the phase")
    parser.add_argument("--timeout", type=float, default=40.0)
    parser.add_argument("--hold", type=float, default=None,
                        help="seconds to hold the other input before switching "
                             "back, instead of waiting for it to count -- about "
                             "three reproduces the displaced first sweep")
    parser.add_argument("--settled-low", type=int, default=29)
    parser.add_argument("--settled-high", type=int, default=31)
    args = parser.parse_args()

    # The count the input under test reads, taken with it already selected, so
    # each leg knows what arrival looks like rather than accepting any lock.
    g.select_input(args.host, args.input)
    baseline = counted(args.host, timeout=args.timeout)
    if baseline is None:
        print(f"{args.input} is not counted, so there is nothing to soak")
        return 1
    print(f"{args.input} counts {baseline}, {args.other} is the leg's transit")

    rows = []
    for n in range(args.legs):
        row = leg(args.host, args.input, args.other, baseline,
                  args.settle, args.timeout, args.hold)
        if not row["ok"]:
            print(f"leg {n + 1:2d}: {row['why']}", flush=True)
            rows.append(row)
            continue
        phase = row["PA_SP_S"]
        bad = displaced(phase, (args.settled_low, args.settled_high))
        rows.append(dict(row, bad=bad))
        print(f"leg {n + 1:2d}: PA_SP_S {phase:2d}  "
              f"PA_ADC_S {row['PA_ADC_S']:2d}  MD {row['PLLAD_MD']}  "
              f"count {row['count']}  {'BAD' if bad else 'good'}", flush=True)

    scored = [r for r in rows if r.get("ok")]
    if not scored:
        print("\nno leg completed")
        return 1

    bad = [r for r in scored if r["bad"]]
    print(f"\n{len(bad)} of {len(scored)} displaced, settled band "
          f"{args.settled_low}..{args.settled_high}"
          f"  ({len(rows) - len(scored)} legs did not complete)")
    print("phases:", " ".join(str(r["PA_SP_S"]) for r in scored))
    return 0


if __name__ == "__main__":
    sys.exit(main())
