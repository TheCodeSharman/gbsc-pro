"""Watch a source mode change from INSIDE loop(), not over HTTP.

    python3 watch_mode_change.py --host <ip> "MODE X640 Y480 C256 F60"


Register reads through /getreg are queued into loop() and a host polling them
starves the very transition being measured. The sampling log takes one read per
sample inside loop() and streams it to the console, so this is the only honest
view of a transient.
"""
import argparse
import socket
import sys
import time

import gbs_unit

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("mode", help="the ModeServ command to send, e.g. 'MODE X640 Y480 C256 F60'")
parser.add_argument("--host", default="192.168.88.108")
parser.add_argument("--source", default="192.168.88.10", help="the ModeServ host")
parser.add_argument("--ms", type=int, default=30, help="sampling interval")
parser.add_argument("--for", dest="duration", type=int, default=25000, help="how long to sample, ms")
args = parser.parse_args()

HOST = args.host
SOURCE = (args.source, 6502)
mode, interval, duration = args.mode, args.ms, args.duration

console = gbs_unit.Console(HOST)
time.sleep(1.5)
status, body = gbs_unit.get(HOST, f"/samplinglog?ms={interval}&for={duration}")
assert status == 200, f"/samplinglog answered {status}: {body}"
time.sleep(0.5)

s = socket.create_connection(SOURCE, 10)
s.sendall((mode + "\n").encode()); s.recv(200); s.close()
t0 = time.time()
time.sleep(duration / 1000.0 + 3)

header, rows = None, []
for line in console.lines:
    if not line.startswith("smp,"):
        continue
    parts = line.split(",")
    if parts[1] == "header":
        header = parts[2:]
    elif parts[1] == "done" or header is None:
        continue
    else:
        try:
            rows.append(dict(zip(header, [int(p) for p in parts[1:]])))
        except ValueError:
            pass

if not header:
    print("no sampling log; is this a GBS_SAMPLING_LOG=1 build?")
    sys.exit(1)

print(f"{mode}: {len(rows)} samples, columns {header}")
prev = None
for r in rows:
    key = (r["divider"], r["sp_vtotal"], r["hperiod_if"], r["intstatus"], r["pllad_lock"])
    if key != prev:
        print(f"  ms={r['ms']:6}  MD {r['divider']:5}  VTOT {r['sp_vtotal']:4}  "
              f"HTOT {r['sp_htotal']:5}  HPER {r['hperiod_if']:4}  "
              f"lock {r['pllad_lock']}  int 0x{r['intstatus']:02x}")
        prev = key
print("--- console (non-sampling) ---")
for line in console.lines:
    if not line.startswith("smp,") and line.strip():
        print("  " + line)
