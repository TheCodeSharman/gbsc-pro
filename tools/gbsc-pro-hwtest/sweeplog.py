#!/usr/bin/env python3
"""Log HPERIOD_IF across a whole modesweep run, with trace-alignable markers.

Unlike capture2.py this does not stop at the first break -- the point of a long
sweep is to learn WHICH transitions rail it, so every mode change needs
recording, not just the first bad one.

Writes a marker (s0 0x95 <- its own value, a no-op the firmware never touches)
every 2 s so the host-time log can be aligned against the UART write trace
afterwards, even during the long quiet stretches when the unit is healthy and
the firmware writes almost nothing.

    python3 sweeplog.py <host> <out.jsonl> [--seconds N]
"""
import argparse, json, sys, time, urllib.request

MARKER_SEG, MARKER_REG = 0, 0x95


def http(host, path, timeout=6):
    with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as r:
        return r.read().decode()


def seg(host, s):
    return bytes.fromhex(json.loads(http(host, f"/getregs?s={s}"))["values"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("out")
    ap.add_argument("--seconds", type=float, default=1800)
    ap.add_argument("--no-marker", action="store_true",
                    help="write nothing at all; loses trace alignment but the unit is untouched")
    args = ap.parse_args()

    marker_val = seg(args.host, MARKER_SEG)[MARKER_REG]
    if args.no_marker:
        print("READ-ONLY: no register writes at all")
    else:
        print(f"marker s{MARKER_SEG} 0x{MARKER_REG:02X} <- 0x{marker_val:02X} every 2 s")

    f = open(args.out, "w", buffering=1)
    t0 = time.time()
    last_marker = 0.0
    last_vt = None
    n = 0

    while time.time() - t0 < args.seconds:
        now = time.time()
        marked = False
        if not args.no_marker and now - last_marker >= 4.0:
            try:
                http(args.host, f"/setreg?s={MARKER_SEG}&r=0x{MARKER_REG:02X}"
                                f"&v=0x{marker_val:02X}")
                marked = True
                last_marker = now
            except Exception:
                pass
        # Segment 0 every sample; segment 1 only every 5th. Two /getregs at 5 Hz
        # was 10 requests/second, heavy enough to be a plausible reason the sync
        # watcher never saw the source as stable (continousStableCounter >= 2)
        # and so never escaped bypass. Keep the load off the firmware loop.
        try:
            s0 = seg(args.host, 0)
            if n % 5 == 0 or last_vt is None:
                s1 = seg(args.host, 1)
                main.cache = (s1[0x2B] & 0x7F, (s1[0x2C] >> 1) & 1)
        except Exception:
            time.sleep(0.3)
            continue

        preset, scal = getattr(main, "cache", (None, None))
        raw = int.from_bytes(s0[6:10], "little")
        rec = {
            "t": now,
            "hperiod": raw & 0x1FF,
            "vperiod": (raw >> 9) & 0x7FF,
            "vtotal": int.from_bytes(s0[0x1B:0x1D], "little") & 0x7FF,
            "st00": s0[0x00], "st16": s0[0x16],
            "preset": preset,
            "scal": scal,
            "marker": marked,
        }
        f.write(json.dumps(rec) + "\n")
        n += 1

        if rec["vtotal"] != last_vt:
            print(f"{now-t0:8.1f}  VTOTAL {last_vt} -> {rec['vtotal']}   "
                  f"HPERIOD {rec['hperiod']}  preset {rec['preset'] if rec['preset'] is None else hex(rec['preset'])}", flush=True)
            last_vt = rec["vtotal"]

        time.sleep(0.5)

    print(f"\n{n} samples -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
