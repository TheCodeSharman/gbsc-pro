"""How long a source mode change takes to reach `acquired`, per destination.

    python3 mode_change_bench.py --host <ip> --source <riscpc> --repeat 3

Times the whole acquisition from the far end: the clock starts when ModeServ
says the source is in the new mode and stops when the engine reports
`state: acquired`. What sits between is VideoSourceAcquisition's escalation, so
the spread across destinations is what says which rung is being paid for.

**The destination predicts the cost, not the origin**, so runs are keyed on
where the source lands. Each is preceded by a settle in a fixed mode, which
makes the transition itself comparable between destinations.

**A run ends when the engine is acquired on a raster it was not on before**, not
when it is merely acquired. `/geometry` reports the last thing solved, so for
the first moments after the command it answers about the mode that just left --
and a run timed against that reports the HTTP round trip, tenths of a second,
as the acquisition. The destination equal to the settle mode has no new raster
to wait for and is timed from the state alone.

`/geometry` answers from the network callback off held state and touches no
register, so polling it does not queue into loop() and does not starve the
transition being measured -- which a `/getreg` poll would.
"""
import argparse
import json
import re
import socket
import statistics
import time
from dataclasses import dataclass

import gbs_unit

# SyncRecovery's rungs, mirrored so a pass count can be named without a build.
# src/tv5725/SyncRecovery.cpp is the source of truth; these are read-only here.
LADDER = [
    (2, "LiftSogFloor"), (8, "CoastWindow"), (27, "SyncProcessorDynamic"),
    (32, "ReleaseCapture"), (34, "HoldClamp"), (38, "NudgeModeDetect"),
    (48, "HsyncOverflowProtect"), (150, "FullReset"), (151, "ReprobeSyncType"),
    (413, "ToggleInput"), (450, "ReopenSogSeparator"),
]
CYCLE_LENGTH = 451
FIRST_ESCALATION_PASS = 2

SAMPLING = re.compile(
    r"sampling:\s*(\d+)\s*lines\s*x\s*([\d.]+)\s*Hz\s*->\s*line rate\s*(\d+)")


def ladder_step(passes):
    """The recovery due after this many consecutive failed passes, or None."""
    if passes < FIRST_ESCALATION_PASS:
        return None
    at = passes % CYCLE_LENGTH
    for position, name in LADDER:
        if position == at:
            return name
    return None


def parse_sampling(line):
    """(lines, field_hz, line_rate) from a console sampling report, or None.

    A line rate of 0 is a reading rateFollowsCount() rejected, so it is not a
    measurement and must not be recorded as one.
    """
    m = SAMPLING.search(line)
    if not m:
        return None
    lines, field, rate = int(m.group(1)), float(m.group(2)), int(m.group(3))
    return None if rate == 0 else (lines, field, rate)


@dataclass
class Summary:
    attempts: int
    acquired: int
    median: float
    fastest: float
    slowest: float


def summarise(samples):
    """Timings for the runs that acquired, counting the ones that did not.

    A run that never acquired has no duration. Folding it in as a zero would
    rank the mode that never solves as the quickest.
    """
    got = [s for s in samples if s is not None]
    if not got:
        return Summary(len(samples), 0, None, None, None)
    return Summary(len(samples), len(got), statistics.median(got),
                   min(got), max(got))


def mode_serv(where, command, timeout=20):
    """One command per connection: the close is the end of the reply."""
    with socket.create_connection((where, 6502), timeout) as link:
        link.sendall((command + "\n").encode())
        chunks = []
        while True:
            b = link.recv(4096)
            if not b:
                break
            chunks.append(b)
    return b"".join(chunks).decode(errors="replace").strip()


def geometry(host):
    status, body = gbs_unit.get(host, "/geometry")
    if status != 200:
        return None
    try:
        return json.loads(body)
    except ValueError:
        return None


# What the engine has solved FOR, rather than whether it has solved. The state
# alone is the previous mode's answer until the source move registers, so a read
# taken straight after the command says "acquired" about the raster that just
# left -- which times the HTTP round trip and calls it acquisition.
SIGNATURE = ("lineRateHz", "cv")


def signature(at):
    return tuple(at.get(k) for k in SIGNATURE) if at else None


def solved_new_mode(at, before, same_mode=False):
    """Whether `at` is the destination solved, given what preceded it."""
    if not at or at.get("state") != "acquired":
        return False
    return True if same_mode else signature(at) != signature(before)


def wait_for_acquired(host, deadline_s, before=None, same_mode=False,
                      interval=0.05):
    """Seconds until the engine is acquired on a raster it was not on, or None.

    `before` is the reading taken before the source was asked to move. Without
    it this waits only for the state, which is what the stale answer satisfies.
    """
    start = time.time()
    while time.time() - start < deadline_s:
        at = geometry(host)
        if solved_new_mode(at, before, same_mode):
            return time.time() - start
        time.sleep(interval)
    return None


def run_one(host, source, mode, deadline_s, same_mode=False):
    before = geometry(host)
    reply = mode_serv(source, mode)
    took = wait_for_acquired(host, deadline_s, before, same_mode)
    return reply, took, before


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", default="192.168.88.108")
    p.add_argument("--source", default="192.168.88.10")
    p.add_argument("--repeat", type=int, default=3)
    p.add_argument("--deadline", type=float, default=30.0,
                   help="seconds to wait for acquired before recording a miss")
    p.add_argument("--settle", default="MODE X320 Y256 C256 F50",
                   help="the mode every run departs from")
    p.add_argument("--modes", nargs="*", default=[
        "MODE X320 Y256 C256 F50",
        "MODE X640 Y480 C256 F60",
        "MODE X800 Y600 C256 F60",
        "MODE X1024 Y768 C256 F60",
        "MODE X640 Y240 C256 F60",
        "MODE X720 Y576 C256 F50",
    ])
    args = p.parse_args()

    console = gbs_unit.Console(args.host)
    time.sleep(1.0)

    results = {}
    for mode in args.modes:
        samples = []
        for n in range(args.repeat):
            same = mode == args.settle
            if not same:
                at = geometry(args.host)
                mode_serv(args.source, args.settle)
                wait_for_acquired(args.host, args.deadline, at)
            console.drain()
            reply, took, _ = run_one(args.host, args.source, mode,
                                     args.deadline, same)
            reports = [parse_sampling(x) for x in console.lines]
            reports = [r for r in reports if r]
            samples.append(took)
            shown = f"{took:6.2f}s" if took is not None else "   MISS"
            first = reports[0] if reports else None
            print(f"  {mode:28} run {n + 1}  {shown}   "
                  f"reply={reply.splitlines()[0] if reply else '?':22} "
                  f"first={first}")
        results[mode] = summarise(samples)

    print(f"\n{'destination':30} {'median':>8} {'fastest':>8} {'slowest':>8}   acquired")
    for mode, s in sorted(results.items(), key=lambda kv: (kv[1].median is None,
                                                          kv[1].median or 0)):
        if s.median is None:
            print(f"{mode:30} {'-':>8} {'-':>8} {'-':>8}   0/{s.attempts}")
        else:
            print(f"{mode:30} {s.median:8.2f} {s.fastest:8.2f} {s.slowest:8.2f}"
                  f"   {s.acquired}/{s.attempts}")
    console.close()


if __name__ == "__main__":
    main()
