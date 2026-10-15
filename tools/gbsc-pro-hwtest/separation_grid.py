#!/usr/bin/env python3
"""Walk the sync separator's two settings and report what the source counts at.

    python3 tools/gbsc-pro-hwtest/separation_grid.py --host 192.168.88.108 \
        --levels 4,6,8,9,10,12,14,16,20 --thresholds 20,44,70,99,124,150,180,220

    ... --thresholds 200:240:1     a band's width, one value at a time
    ... --raw /tmp/grid.csv        every reading, for sizing a run-of-K predicate
    ... --keep                     leave the last pair in force

THE COUNT IS SAMPLED ON THE DEVICE, NOT OVER HTTP. `/samplinglog?rates=` times
the field rate off DEBUG_IN_PIN exactly as the engine does and reads
STATUS_SYNC_PROC_VTOTAL beside it, from inside loop(); an HTTP point read of
that register answers at tens of hertz at best and over-reports a steady
register as intermittent, which is indistinguishable from the separator
genuinely scattering the count.

Readings land ~140 ms apart at 60 Hz, which is longer than a field, so each one
is a fresh measurement rather than a re-read of the same latched value.

The two settings are ONE setting: the threshold that reads a source moves with
the slice level, so neither can be swept alone.
docs/investigations/the-engine-makes-the-composite-count-unmeasurable.md
"""

import argparse
import os
import statistics
import sys
import time

import gbs_unit
import hdmi_capture
import setfield

HERE = os.path.dirname(os.path.abspath(__file__))

# What the engine would have to read for the source to be acquired. The count
# band is the source's own total with room for the ±1 a healthy reading carries;
# the rate band is what rateFollowsCount() would accept.
DEFAULT_COUNT_BAND = (615, 635)
DEFAULT_RATE_BAND = (59.0, 61.5)

# How many pulses one rate reading spans by default. ONE, because that is what
# the engine times: SourceMeasurement takes nine single-pulse readings and asks
# which value recurs, and keeps an 8-pulse span only when it agrees with that
# cluster -- a span on this source being wrong most of the time. A walk scored
# at 8 measures a statistic the engine distrusts.
RATE_PULSE_SPAN = 1

# A reading at a threshold the source cannot be read through still has to time
# out, and the pin's timeout restarts per pulse -- so a point's budget is not
# the healthy case's.
POINT_TIMEOUT_S = 90.0

# Frames per picture reading, and how many to discard first. THE WALK NEVER
# TOUCHES THE LINK, so the dongle's settling warm-up is paid once rather than
# per step -- at the default it would be most of the walk.
PICTURE_FRAMES = 2
PICTURE_WARMUP = 4


def relock(where, mode, settle_s=6.0):
    """Make the source leave and return so the separator re-locks with whatever
    threshold is now in force.

    A VALUE POKED AFTER THE LOCK IS NOT THE SAME EXPERIMENT as the value the
    block locked on, and the firmware only ever does the latter: it writes the
    threshold and the source arrives, or arrives again. Re-issuing the source's
    MODE is what reproduces that with automation held, so the divider the walk
    is scored against does not move.

    The reply waits on the repaint rather than on the timing change, so it is
    not the settle -- the separator is given its own.
    """
    reply = gbs_unit.mode_serv(where, f"MODE {mode}")
    time.sleep(settle_s)
    return reply


def sync_pulse_samples(host, divider):
    """The source's sync pulse in ADC samples, as the chip measures it.

    STATUS_SYNC_PROC_HLOW_LEN is the LOW run, so on a path that inverts the
    separator's reference it reports the line less the pulse -- 1890 of a
    2038-sample line on the Wii in pass-through, whose pulse is the remaining
    148. The shorter of the two is the pulse: no source here spends most of its
    line in sync.
    """
    low = gbs_unit.read_named(host, "STATUS_SYNC_PROC_HLOW_LEN")
    if low is None or divider == 0:
        return None
    return min(low, divider - low) if low < divider else low


def picture_luma(device):
    """Mean luma of the emitted frame, or None when no dongle was asked for.

    THE COUNT CANNOT SEE EVERY FAILURE. The threshold feeds the separator's
    horizontal output, which is what the ADC PLL locks to, so a value that
    rejects real pulses can leave the vertical count perfect and the sampling
    wrong -- measured on the Wii at 480p through a 2038-sample line, a steady
    524 beside mean luma 0.00.
    """
    if device is None:
        return None
    rgb = hdmi_capture.frames(PICTURE_FRAMES, device, warmup=PICTURE_WARMUP)
    return float(hdmi_capture.luma(rgb).mean())


def fail(message):
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def write_named(host, name, value):
    """A field by NAME, read-modify-write. A hand-written segment/register/
    offset/width does not error on a wrong address, it returns a plausible
    number."""
    spec = setfield.load_map().get(name)
    if spec is None:
        raise KeyError(f"not in tv5725_registers.json: {name}")
    writes = setfield.byte_writes(host, spec, value)
    if writes is None:
        return False
    for register, _old, new in writes:
        if gbs_unit.write_reg(host, spec["seg"], register, new) is None:
            return False
    return setfield.read_field(host, spec) == value


def queue_rates(host, reads, pulses):
    """Ask for `reads` on-device readings, and say whether the request landed.

    409 is a previous run still going, which is worth waiting out rather than
    failing on: the route is the only instrument here.
    """
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        status, payload = gbs_unit.get_json(
            host, f"/samplinglog?rates={reads}&pulses={pulses}"
        )
        if status == 200 and isinstance(payload, dict) and payload.get("queued"):
            return True
        if status == 404 or status == 500:
            return False
        time.sleep(1.0)
    return False


def read_point(host, console, reads, pulses=RATE_PULSE_SPAN):
    """The readings at whatever separation is in force, as [(count, rate_hz)].

    The socket is drained and the route queued from the same process, in that
    order: queuing from a shell while a capture process starts up races both
    ways, and either way the capture comes back empty -- which reads as a dead
    route rather than as a missed window.
    """
    console.drain()
    if not queue_rates(host, reads, pulses):
        return None

    deadline = time.monotonic() + POINT_TIMEOUT_S
    while time.monotonic() < deadline:
        lines = console.collect(0.3)
        if any(line.startswith("smp,done") for line in lines):
            break
    else:
        lines = console.collect(0.1)

    out = []
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) != 4 or parts[0] != "rate":
            continue
        try:
            out.append((int(parts[2]), int(parts[3]) / 1000.0))
        except ValueError:
            continue
    return out


def verdict(readings, count_band, rate_band, luma=None):
    """What a point is worth, as a dict. `held` is the fraction of readings a
    predicate asking for both the count and the rate would accept."""
    if not readings:
        return None
    counts = [c for c, _ in readings]
    rates = [r for _, r in readings]
    good = [
        count_band[0] <= c <= count_band[1] and rate_band[0] <= r <= rate_band[1]
        for c, r in readings
    ]
    return {
        "n": len(readings),
        "count": statistics.median(counts),
        "distinct": len(set(counts)),
        "rate": statistics.median(rates),
        "held": sum(good) / len(good),
        "run": longest_run(good),
        "luma": luma,
    }


def longest_run(flags):
    best = run = 0
    for flag in flags:
        run = run + 1 if flag else 0
        best = max(best, run)
    return best


def walk_levels(host, console, reads):
    """The count at every separator level, through the device's own sog walk.

    `/samplinglog?sog=` re-asserts the level per reading and restores the entry
    level itself, which a host-side walk cannot: the recovery ladder writes
    ADC_SOGCTRL too, so a level set once per step can be walked out from under
    the readings taken at it.
    """
    console.drain()
    status, payload = gbs_unit.get_json(
        host, f"/samplinglog?sog=0&to=31&reads={reads}"
    )
    if status != 200 or not (isinstance(payload, dict) and payload.get("queued")):
        return None

    deadline = time.monotonic() + 32 * reads * 1.0 + 60.0
    while time.monotonic() < deadline:
        lines = console.collect(0.5)
        if any(line.startswith("smp,done") for line in lines):
            break

    out = {}
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) != 5 or parts[0] != "sog":
            continue
        try:
            out.setdefault(int(parts[1]), []).append(
                (int(parts[3]), int(parts[4]) / 1000.0)
            )
        except ValueError:
            continue
    return out


def thresholds_at_levels(host, console, reads, levels, thresholds, entry_level,
                         pulses, relock_to=None):
    """Every threshold at every level, the threshold inner. Yields
    (level, threshold, readings)."""
    for level in levels:
        if level != entry_level and not write_named(host, "ADC_SOGCTRL", level):
            yield level, 0, None
            continue
        for threshold in thresholds:
            if not write_named(host, "SP_H_PULSE_IGNOR", threshold):
                yield level, threshold, None
                continue
            if relock_to is not None:
                relock(*relock_to)
            yield level, threshold, read_point(host, console, reads, pulses)


def levels_at_thresholds(host, console, reads, levels, thresholds, entry_level,
                         pulses, relock_to=None):
    """Every level at every threshold, through the device's own level walk, which
    re-asserts the level per reading where a host-side walk cannot."""
    del levels, entry_level, pulses, relock_to
    for threshold in thresholds:
        if not write_named(host, "SP_H_PULSE_IGNOR", threshold):
            yield 0, threshold, None
            continue
        walked = walk_levels(host, console, reads)
        if walked is None:
            yield 0, threshold, None
            continue
        for level, readings in sorted(walked.items()):
            yield level, threshold, readings


def thresholds_from(spec):
    """Either a list, or low:high:step for a band's width."""
    if ":" in spec:
        parts = [int(p) for p in spec.split(":")]
        low, high = parts[0], parts[1]
        step = parts[2] if len(parts) > 2 else 1
        return list(range(low, high + 1, step))
    return [int(p) for p in spec.split(",")]


HELD_FIELDS = ["SP_SOG_MODE", "PLLAD_MD", "STATUS_SYNC_PROC_HTOTAL",
               "ADC_SOGCTRL", "SP_H_PULSE_IGNOR"]


def latched(got):
    """Whether the divider the registers report is the one the ADC is clocking
    at. STATUS_SYNC_PROC_HTOTAL counts real ADC clocks per line, so it is the
    only thing on the board that can see a missing latch -- reading PLLAD_MD
    back reports what the chip was told, not what it runs."""
    return (got is not None and got["PLLAD_MD"] != 0
            and abs(got["PLLAD_MD"] - got["STATUS_SYNC_PROC_HTOTAL"]) <= 2)


def freeze_when_latched(host, limit_s=120.0):
    """Hold the engine at a moment the divider is latched, and report what it
    was holding.

    A composite source here does not hold: the engine re-solves every couple of
    seconds, installing a divider and losing it again, so a walk that freezes
    whenever it is asked lands on the reference clock about as often as not. The
    threshold being walked is in ADC samples, so that is a walk through a
    different line.

    Gating on `state: acquired` finds nothing -- it reaches acquired for a
    fraction of the time and cycles through a stale sync type as well.
    """
    started = time.monotonic()
    while time.monotonic() - started < limit_s:
        got = gbs_unit.read_fields(host, HELD_FIELDS)
        if got is None:
            return None, "the unit did not answer /getfields"
        if got["SP_SOG_MODE"] != 1:
            return got, ("SP_SOG_MODE is 0, so the separator is out of circuit. "
                         "Put the source on composite sync and let the engine "
                         "find it: printf 'SYNC 1\\n' | nc 192.168.88.10 6502")
        if not latched(got):
            continue
        if not gbs_unit.freeze(host, True):
            return got, "could not freeze automation"

        # Re-read AFTER the freeze: the pass in flight when the request landed
        # can still move the divider, and a walk started on the strength of the
        # earlier read would be scored against a clock it is not running.
        confirmed = gbs_unit.read_fields(host, HELD_FIELDS)
        if latched(confirmed):
            return confirmed, None
        gbs_unit.freeze(host, False)

    return None, (f"the divider was never latched for a whole freeze inside "
                  f"{limit_s:.0f}s. The engine is re-solving: watch the console "
                  "for `installing: divider` and `UNLATCHED`")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", required=True)
    parser.add_argument("--levels", default="",
                        help="separator levels to walk; empty keeps the one in force")
    parser.add_argument("--thresholds", default="8:255:8",
                        help="list, or low:high:step")
    parser.add_argument("--reads", type=int, default=25)
    parser.add_argument("--count-band", default="615:635")
    parser.add_argument("--rate-band", default="59:61.5")
    parser.add_argument("--raw", help="write every reading to this CSV")
    parser.add_argument("--keep", action="store_true",
                        help="leave the last pair in force instead of restoring")
    parser.add_argument("--relock", metavar="HOST:MODE",
                        help="re-issue the source's mode between the write and "
                             "the reading, so the separator locks WITH the "
                             "threshold rather than having it poked underneath "
                             "it, e.g. 192.168.88.10:X800 Y600 C256 F60")
    parser.add_argument("--control", type=int,
                        help="threshold to re-measure between treatments. A "
                             "control that stops reading the source says the "
                             "walk left a state behind, which makes every "
                             "reading after it a reading of that state")
    parser.add_argument("--picture", action="store_true",
                        help="also measure the emitted frame's mean luma off "
                             "the USB HDMI dongle, which sees a failure the "
                             "count cannot")
    parser.add_argument("--pulses", type=int, default=RATE_PULSE_SPAN,
                        help="pulses one rate reading spans; 1 is what the "
                             "engine times")
    parser.add_argument("--at-levels", action="store_true",
                        help="walk the separator LEVEL at each threshold given, "
                             "rather than the threshold at each level")
    parser.add_argument("--sog-src", type=int, choices=(0, 1),
                        help="hold SP_SOG_SRC_SEL across the walk: 0 takes the "
                             "separator's input from the ADC, 1 from the HSync "
                             "pin, which is where a composite-sync RGBHV "
                             "source's sync actually arrives")
    args = parser.parse_args()

    count_band = tuple(int(p) for p in args.count_band.split(":"))
    rate_band = tuple(float(p) for p in args.rate_band.split(":"))
    thresholds = thresholds_from(args.thresholds)

    held, why = freeze_when_latched(args.host)
    if why:
        return fail(why)
    levels = ([int(p) for p in args.levels.split(",")] if args.levels
              else [held["ADC_SOGCTRL"]])
    entry_level, entry_threshold = held["ADC_SOGCTRL"], held["SP_H_PULSE_IGNOR"]
    print(f"frozen with divider {held['PLLAD_MD']} latched, entry level "
          f"{entry_level}, entry threshold {entry_threshold}")

    relock_to = None
    if args.relock:
        where, _, mode = args.relock.partition(":")
        relock_to = (where, mode)
    dongle = hdmi_capture.device() if args.picture else None
    entry_sog_src = gbs_unit.read_named(args.host, "SP_SOG_SRC_SEL")
    if args.sog_src is not None:
        if not write_named(args.host, "SP_SOG_SRC_SEL", args.sog_src):
            gbs_unit.freeze(args.host, False)
            return fail("could not write SP_SOG_SRC_SEL")
        print(f"SP_SOG_SRC_SEL {entry_sog_src} -> {args.sog_src}")

    console = gbs_unit.Console(args.host)
    raw = open(args.raw, "w") if args.raw else None
    if raw:
        raw.write("level,threshold,reading,count,rate_hz,luma,pulse\n")

    walk = (levels_at_thresholds if args.at_levels else thresholds_at_levels)
    results = {}
    try:
        for level, threshold, readings in walk(args.host, console, args.reads,
                                               levels, thresholds, entry_level,
                                               args.pulses, relock_to):
            if not readings:
                print(f"  {level:3}/{threshold:3}  nothing read")
                continue
            luma = picture_luma(dongle)
            pulse = sync_pulse_samples(args.host, held["PLLAD_MD"])
            if raw:
                for index, (count, rate) in enumerate(readings):
                    raw.write(f"{level},{threshold},{index},{count},{rate:.3f},"
                              f"{'' if luma is None else f'{luma:.3f}'},"
                              f"{'' if pulse is None else pulse}\n")
                raw.flush()
            scored = verdict(readings, count_band, rate_band, luma)
            results[(level, threshold)] = scored
            print(f"  {level:3}/{threshold:3}  n {scored['n']:3}  "
                  f"count {scored['count']:7.1f}  distinct {scored['distinct']:3}  "
                  f"rate {scored['rate']:6.2f}  held {scored['held']:5.0%}  "
                  f"run {scored['run']:3}"
                  + ("" if luma is None else f"  luma {luma:7.2f}")
                  + ("" if pulse is None else f"  pulse {pulse:4}"), flush=True)

            if args.control is None or threshold == args.control:
                continue
            if not write_named(args.host, "SP_H_PULSE_IGNOR", args.control):
                continue
            back = read_point(args.host, console, args.reads, args.pulses)
            held_again = verdict(back, count_band, rate_band, picture_luma(dongle))
            if held_again is not None:
                print(f"      control {args.control:3}  "
                      f"count {held_again['count']:7.1f}  "
                      f"held {held_again['held']:5.0%}"
                      + ("" if held_again['luma'] is None
                         else f"  luma {held_again['luma']:7.2f}"), flush=True)
        levels = sorted({key[0] for key in results}) or levels
    finally:
        if raw:
            raw.close()
        console.close()
        if not args.keep:
            write_named(args.host, "ADC_SOGCTRL", entry_level)
            write_named(args.host, "SP_H_PULSE_IGNOR", entry_threshold)
            if args.sog_src is not None:
                write_named(args.host, "SP_SOG_SRC_SEL", entry_sog_src)
            print(f"restored level {entry_level}, threshold {entry_threshold}")
        gbs_unit.freeze(args.host, False)

    report(results, levels, thresholds)
    return 0


def report(results, levels, thresholds):
    print()
    print("held, as a percentage of readings inside both bands:")
    header = "level " + "".join(f"{t:>6}" for t in thresholds)
    print(header)
    for level in levels:
        row = f"{level:5} "
        for threshold in thresholds:
            scored = results.get((level, threshold))
            row += "     -" if scored is None else f"{scored['held'] * 100:6.0f}"
        print(row)

    working = [k for k, v in results.items() if v and v["held"] >= 1.0]
    print()
    print(f"{len(working)} of {len(results)} pairs read the source in every "
          f"reading: {working}")


if __name__ == "__main__":
    sys.exit(main())
