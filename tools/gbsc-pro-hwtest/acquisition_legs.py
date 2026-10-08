"""Time how long the engine takes to show a picture after the source changes.

    python3 acquisition_legs.py --host <ip> --input vga,ypbpr --legs 20
    python3 acquisition_legs.py --host <ip> --mode "X800 Y600 C256 F60,X320 Y256 C256 F50"

THE BUDGETS, AND THEY ARE DIFFERENT. A source mode change is allowed 5 s to the
picture being shown and an input or sync-type change 10 s, both measured to
`sync pad: driven` rather than to `state: acquired` -- a solve is not a picture
and the pad returns 0.4..0.6 s later.

**SIX LEGS CANNOT DISTINGUISH TWO BUILDS. USE TWENTY.** One binary gave 2 of 6
and 4 of 6 on two runs of the same test, so four variants were once compared
against noise and three conclusions withdrawn.

**JUDGE ON THE COUNT, THEN THE TIME, THEN THE PAD.** /geometry's `cv` is twice
the line count on an interlaced source, so the Wii in 480i is 520/522 and a
534 is a 267-line solve wearing an acquired state. On time alone that scores as
a success, and one build produced exactly it.

**A LEG TIMED WHILE THE UNIT IS BOOTING IS NOT A LEG**, so every run waits for
the unit to be acquired on something before the first leg starts.
"""
import argparse
import json
import time

import gbs_unit

# What each source IS, so a leg cannot be scored against the predecessor's state
# or against a plausible-looking wrong count. `cv` is the engine's own count
# doubled on an interlaced source.
SOURCES = {
    "ypbpr": {"cv": (514, 526), "lineRateHz": (15450, 15750)},
    "vga": {"cv": (624, 632), "lineRateHz": (37600, 38100)},
}

MODES = {
    "X800 Y600 C256 F60": {"cv": (624, 632), "lineRateHz": (37600, 38100)},
    "X320 Y256 C256 F50": {"cv": (620, 628), "lineRateHz": (15500, 15750)},
    "X640 Y480 C256 F60": {"cv": (522, 528), "lineRateHz": (31200, 31700)},
}

# Past this a leg has failed. Capped rather than open-ended because a run that
# sits on a dead source teaches nothing and costs the rest of the run.
INPUT_LIMIT_S = 32.0
MODE_LIMIT_S = 25.0

# Between legs, so the engine is not asked to leave a source it has only just
# reached.
SETTLE_S = 3.0


def arrived(payload, want):
    """Whether /geometry reports the source this leg asked for, acquired."""
    if payload.get("state") != "acquired":
        return False
    for field, (low, high) in want.items():
        if not low <= payload.get(field, 0) <= high:
            return False
    return True


def leg(host, console, start, want, limit_s):
    """Time one leg to the pad being driven. `start` performs the change.

    The pad is read off the console rather than over HTTP: VideoPath holds what
    it last wrote and no route reports it, and the line is the only thing that
    says the output was handed back.
    """
    console.drain()
    t0 = time.time()
    start()

    acquired = None
    payload = {}
    while time.time() - t0 < limit_s:
        status, body = gbs_unit.get_json(host, "/geometry")
        if status != 200 or body is None:
            continue
        payload = body
        if acquired is None and arrived(payload, want):
            acquired = time.time() - t0
        if acquired is not None and console_has(console, "sync pad: driven"):
            return acquired, time.time() - t0, payload
        time.sleep(0.2)

    return acquired, None, payload


def console_has(console, text):
    return any(text in line for line in console.lines)


def report(name, results, budget_s):
    """One line a build can be compared on, and the legs it came from."""
    shown = [pad for _, pad, _ in results if pad is not None]
    inside = [pad for pad in shown if pad <= budget_s]
    print(f"\n{name}: {len(shown)}/{len(results)} shown a picture, "
          f"{len(inside)}/{len(results)} inside {budget_s:.0f}s")
    if shown:
        print(f"  {min(shown):.1f}s .. {max(shown):.1f}s, "
              f"median {sorted(shown)[len(shown) // 2]:.1f}s")


def run_inputs(host, console, order, legs):
    results = {name: [] for name in order}
    for trip in range(legs):
        for name in order:
            acquired, pad, payload = leg(
                host, console,
                lambda n=name: gbs_unit.select_input(host, n),
                SOURCES[name], INPUT_LIMIT_S)
            print(f"leg {trip + 1:>3} {name:>6}  "
                  f"{'%5.1fs' % pad if pad else ' NEVER'}  "
                  f"(acquired {'%5.1fs' % acquired if acquired else '  n/a'})  "
                  f"cv={payload.get('cv')} rate={payload.get('lineRateHz')} "
                  f"state={payload.get('state')}", flush=True)
            results[name].append((acquired, pad, payload))
            time.sleep(SETTLE_S)
    for name in order:
        report(f"input {name}", results[name], 10.0)
    return results


def run_modes(host, console, source, order, legs):
    results = {mode: [] for mode in order}
    for trip in range(legs):
        for mode in order:
            acquired, pad, payload = leg(
                host, console,
                lambda m=mode: gbs_unit.mode_serv(source, f"MODE {m}", timeout=25),
                MODES[mode], MODE_LIMIT_S)
            print(f"leg {trip + 1:>3} {mode:>20}  "
                  f"{'%5.2fs' % pad if pad else ' NEVER'}  "
                  f"(acquired {'%5.2fs' % acquired if acquired else '  n/a'})  "
                  f"cv={payload.get('cv')} rate={payload.get('lineRateHz')}",
                  flush=True)
            results[mode].append((acquired, pad, payload))
            time.sleep(SETTLE_S)
    for mode in order:
        report(f"mode {mode}", results[mode], 5.0)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.88.108")
    parser.add_argument("--source", default="192.168.88.10",
                        help="the ModeServ host, for --mode")
    parser.add_argument("--input", help=f"inputs to cycle, e.g. {','.join(SOURCES)}")
    parser.add_argument("--mode", help="source modes to cycle, comma separated")
    parser.add_argument("--legs", type=int, default=20,
                        help="trips through the list; six cannot distinguish two builds")
    args = parser.parse_args()

    if not args.input and not args.mode:
        parser.error("one of --input or --mode")

    # A leg timed while the unit is booting is not a leg: the first trip after an
    # OTA scores NEVER or 0.0s depending on which input the flash rebooted onto.
    status, start = gbs_unit.get_json(args.host, "/geometry")
    assert status == 200 and start is not None, f"/geometry answered {status}"
    if start.get("state") != "acquired":
        print(f"waiting for the unit to acquire something: {json.dumps(start)}")
        gbs_unit.wait_for(
            lambda: (gbs_unit.get_json(args.host, "/geometry")[1] or {})
                    .get("state") == "acquired",
            timeout=60.0, interval=0.5)

    console = gbs_unit.Console(args.host)
    time.sleep(1.0)

    if args.input:
        order = [name.strip() for name in args.input.split(",")]
        for name in order:
            assert name in SOURCES, f"no expected count for {name}"
        run_inputs(args.host, console, order, args.legs)

    if args.mode:
        order = [mode.strip() for mode in args.mode.split(",")]
        for mode in order:
            assert mode in MODES, f"no expected count for {mode}"
        run_modes(args.host, console, args.source, order, args.legs)


if __name__ == "__main__":
    main()
