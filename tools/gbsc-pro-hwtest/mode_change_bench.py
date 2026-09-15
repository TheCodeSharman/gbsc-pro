"""How long a source mode change takes to reach `acquired`, per ORDERED leg.

    python3 mode_change_bench.py --host <ip> --source <riscpc> --repeat 3

Times the whole acquisition from the far end: `/geometry` is sampled by a
thread running right across the command, the clock starts at the sample where
the departure raster was lost and stops where the engine reports
`state: acquired` on the destination. What sits between is
VideoSourceAcquisition's escalation, so the spread across destinations is what
says which rung is being paid for.

**ModeServ's reply is not when the timing changed.** It waits on the repaint,
and VIDC20 has moved ~70 ms after the register write, so a clock started at the
reply has already missed the transition -- and the engine often acquires inside
that call. That is why the sampler starts before the command is sent.

**A LEG IS ORDERED AND BOTH DIRECTIONS ARE WALKED.** The cost is not symmetric:
640x480 -> 320x256 pays for a divider that still describes the mode that left
and its reverse does not, so a table keyed on the destination alone averages the
two together and hides the finding. Every ordered pair of distinct modes is
walked, chained into one tour so each leg departs where the previous landed.

**A run ends when the engine is acquired on a raster it was not on before**, not
when it is merely acquired. `/geometry` reports the last thing solved, so for
the first moments after the command it answers about the mode that just left --
and a run timed against that reports the HTTP round trip, tenths of a second,
as the acquisition.

**AND A LEG IS NOT TIMED UNTIL ITS DEPARTURE RASTER HAS BEEN HELD.** One
`acquired` reading is not a settle: the engine keeps re-solving for about three
seconds afterwards, and a leg started on that first reading measures the
previous leg's churn. Timed that way the 640x480 -> 320x256 leg reported 0.27 s
against a measured 1.35 s.

`/geometry` answers from the network callback off held state and touches no
register, so polling it does not queue into loop() and does not starve the
transition being measured -- which a `/getreg` poll would.
"""
import argparse
import json
import re
import socket
import statistics
import threading
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


def unbroken_since(samples):
    """When the run of acquired samples ending `samples` started, or None.

    A settle is a HOLD rather than one reading. The engine keeps re-solving for
    about three seconds after it first answers `acquired` -- the console carries
    a further sampling phase and clock steer -- so a leg started on that first
    reading times the PREVIOUS leg's churn and reports tenths of a second.
    """
    if not samples:
        return None
    t_last, last = samples[-1]
    if last.get("state") != "acquired":
        return None
    want = signature(last)
    start = t_last
    for t, at in reversed(samples):
        if at.get("state") != "acquired" or signature(at) != want:
            break
        start = t
    return start


def settle(host, dwell_s, deadline_s, interval=0.05):
    """Wait until one raster has been held acquired for `dwell_s`. True if it was."""
    samples = []
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        at = geometry(host)
        if at:
            samples.append((time.time(), at))
        held = unbroken_since(samples)
        if held is not None and time.time() - held >= dwell_s:
            return True
        time.sleep(interval)
    return False


SHORT = re.compile(r"MODE\s+X(\d+)\s+Y(\d+).*?F(\d+)")


def short(mode):
    """`MODE X640 Y480 C256 F60` as `640x480@60`."""
    m = SHORT.match(mode)
    return f"{m.group(1)}x{m.group(2)}@{m.group(3)}" if m else mode


def leg(departs, lands):
    """What a timing is recorded under. ORDERED: the cost is not symmetric --
    640x480 -> 320x256 pays for a divider that describes the mode that left,
    and the reverse does not."""
    return f"{short(departs)} -> {short(lands)}"


def legs(modes):
    """Every ordered pair of distinct modes, chained so each departs where the
    previous landed.

    Both directions are walked because they are different transitions. Chaining
    them into one tour means a leg's departure is the previous leg's arrival,
    so the walk pays for one mode change per leg rather than two.
    """
    remaining = {m: [x for x in modes if x != m] for m in modes}
    tour, stack = [], [modes[0]]
    while stack:
        at = stack[-1]
        if remaining[at]:
            stack.append(remaining[at].pop(0))
        else:
            tour.append(stack.pop())
    tour.reverse()
    return list(zip(tour, tour[1:]))


@dataclass
class Transition:
    moved: float
    acquired: float
    interval: float


def analyse(samples, t_cmd, before):
    """When the source moved and when the engine was acquired on the new raster.

    `samples` are (t, geometry) taken right across the command, because
    mode_serv() blocks until the source has changed mode AND repainted: the
    engine often acquires inside that call, so a clock started at the reply has
    already missed the transition it meant to time.
    """
    want = signature(before)
    moved = done = None
    for t, at in samples:
        if t < t_cmd:
            continue
        acquired = at.get("state") == "acquired"
        differs = signature(at) != want
        if moved is None:
            if acquired and not differs:
                continue
            moved = t
        if acquired and differs:
            done = t
            break
    interval = None if (moved is None or done is None) else done - moved
    return Transition(moved, done, interval)


def watch(host, samples, stop, interval=0.05):
    """Poll /geometry into `samples` until `stop` is set."""
    while not stop.is_set():
        at = geometry(host)
        if at:
            samples.append((time.time(), at))
        time.sleep(interval)


def run_one(host, source, mode, deadline_s):
    before = geometry(host)
    samples, stop = [], threading.Event()
    watcher = threading.Thread(target=watch, args=(host, samples, stop), daemon=True)
    watcher.start()
    time.sleep(0.3)
    t_cmd = time.time()
    reply = mode_serv(source, mode)
    deadline = time.time() + deadline_s
    while True:
        transition = analyse(samples, t_cmd, before)
        if transition.acquired is not None or time.time() >= deadline:
            break
        time.sleep(0.05)
    stop.set()
    watcher.join(timeout=2)
    return reply, transition


def why_missed(tr):
    """Which half of the transition went unseen, for a run that has no interval."""
    if tr.moved is None:
        return "(no move seen)"
    if tr.acquired is None:
        return "(never reacquired)"
    return ""


def opposed(name):
    """The leg `name` reverses."""
    there, back = name.split(" -> ")
    return f"{back} -> {there}"


def asymmetry(summaries):
    """Each pair of opposed legs, slower first, with the gap between them.

    Reported as a pair because the pair is the finding: one direction pays for a
    divider still describing the mode that left and the other does not, and a
    table keyed on the destination alone cannot show it.
    """
    out, seen = [], set()
    for name, there in summaries.items():
        back = summaries.get(opposed(name))
        if back is None or name in seen:
            continue
        seen.add(name)
        seen.add(opposed(name))
        if there.median is None or back.median is None:
            continue
        slow, fast = ((name, there), (opposed(name), back))
        if back.median > there.median:
            slow, fast = fast, slow
        out.append((slow, fast, slow[1].median - fast[1].median))
    return sorted(out, key=lambda row: -row[2])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", default="192.168.88.108")
    p.add_argument("--source", default="192.168.88.10")
    p.add_argument("--repeat", type=int, default=1,
                   help="times to walk the whole tour")
    p.add_argument("--deadline", type=float, default=30.0,
                   help="seconds to wait for acquired before recording a miss")
    p.add_argument("--dwell", type=float, default=4.0,
                   help="seconds one raster must be held before a leg is timed")
    p.add_argument("--modes", nargs="*", default=[
        "MODE X320 Y256 C256 F50",
        "MODE X640 Y480 C256 F60",
        "MODE X800 Y600 C256 F60",
        "MODE X720 Y576 C256 F50",
    ])
    args = p.parse_args()

    tour = legs(args.modes)
    console = gbs_unit.Console(args.host)
    time.sleep(1.0)

    print(f"{len(tour)} legs x {args.repeat}, departing from {short(tour[0][0])}")
    mode_serv(args.source, tour[0][0])

    runs = {}
    for n in range(args.repeat):
        for departs, lands in tour:
            name = leg(departs, lands)
            if not settle(args.host, args.dwell, args.deadline):
                print(f"  {name:26} run {n + 1}   NO SETTLE at the departure")
                runs.setdefault(name, []).append(None)
                mode_serv(args.source, lands)
                continue
            console.drain()
            reply, tr = run_one(args.host, args.source, lands, args.deadline)
            reports = [r for r in (parse_sampling(x) for x in console.lines) if r]
            runs.setdefault(name, []).append(tr.interval)
            shown = f"{tr.interval:6.2f}s" if tr.interval is not None else "   MISS"
            print(f"  {name:26} run {n + 1}  {shown}  {why_missed(tr):18} "
                  f"first={reports[0] if reports else None}"
                  f"{'' if reply else '   (no ModeServ reply)'}")

    summaries = {name: summarise(v) for name, v in runs.items()}

    print(f"\n{'leg':28} {'median':>8} {'fastest':>8} {'slowest':>8}   acquired")
    for name, st in sorted(summaries.items(),
                           key=lambda kv: (kv[1].median is None, kv[1].median or 0)):
        if st.median is None:
            print(f"{name:28} {'-':>8} {'-':>8} {'-':>8}   0/{st.attempts}")
        else:
            print(f"{name:28} {st.median:8.2f} {st.fastest:8.2f} {st.slowest:8.2f}"
                  f"   {st.acquired}/{st.attempts}")

    rows = asymmetry(summaries)
    if rows:
        print("\nopposed legs, slower first")
        for (slow, s_st), (fast, f_st), gap in rows:
            print(f"  {slow:28} {s_st.median:6.2f}   "
                  f"{fast:28} {f_st.median:6.2f}   gap {gap:6.2f}")
    console.close()


if __name__ == "__main__":
    main()
