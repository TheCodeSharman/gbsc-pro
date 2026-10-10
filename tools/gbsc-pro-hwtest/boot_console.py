#!/usr/bin/env python3
"""Capture the console across one or more boots, timestamped from the restart.

What a boot does is a SEQUENCE, and the console is the only place a sequence is
visible. The websocket handshake does not complete until about 12 s, which is
after detection, both sync-type probes and the first solve -- so the backlog
/bootlog holds is what carries that window, and the firmware hands it to the
first console that attaches.

**Do not poll port 81 to find out when the unit is back.** A TCP connection to
it counts as a connected client, which sets bootLogDelivered and stops the
recording -- truncating the very window being asked about. Port 80 answers from
the network callbacks and costs the log nothing; --no-console skips the socket
altogether and reads /bootlog over HTTP instead.

    python3 boot_console.py --host <ip> --boots 6
    python3 boot_console.py --host <ip> --no-console --settle 40
"""
import argparse
import sys
import time
import urllib.request

DEFAULT_HOST = "192.168.88.108"


def get(host, path, timeout=8):
    with urllib.request.urlopen(f"http://{host}{path}", timeout=timeout) as f:
        return f.read().decode("utf-8", "replace")


def restart(host, out, label):
    try:
        get(host, "/restart", timeout=5)
    except Exception as exc:
        out.write(f"# {label} restart request: {exc}\n")
    out.write(f"# {label} restart at t=0\n")
    out.flush()


def answering(host):
    try:
        get(host, "/geometry", timeout=2)
        return True
    except Exception:
        return False


def wait_for_restart(host, t0, deadline):
    """Port 80 only, down THEN up.

    ESP.restart() is queued, so the server keeps answering for a moment after
    the request -- and a reader that only waits for an answer gets the outgoing
    firmware's, attaches before the reboot, and with --no-console reads the
    PREVIOUS boot's log.
    """
    went_down = False
    while time.time() - t0 < deadline:
        if answering(host):
            if went_down:
                return time.time() - t0
        else:
            went_down = True
        time.sleep(0.2)
    return None


def follow_console(host, t0, seconds, label, out):
    import websocket  # only needed with a console, so not a hard dependency

    socket_up = None
    ws = None
    while time.time() - t0 < seconds:
        if ws is None:
            try:
                ws = websocket.create_connection(
                    f"ws://{host}:81", subprotocols=["arduino"], timeout=3)
            except Exception:
                time.sleep(0.2)
                continue
            ws.settimeout(0.5)
            socket_up = time.time() - t0
            out.write(f"# {label} socket up at {socket_up:.2f}\n")
            out.flush()
        try:
            message = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        except Exception:
            out.write(f"# {label} socket lost at {time.time() - t0:.2f}\n")
            ws = None
            continue
        for line in message.splitlines():
            line = line.strip()
            if line:
                out.write(f"{label} {time.time() - t0:7.2f}  {line}\n")
        out.flush()
    if ws is not None:
        try:
            ws.close()
        except Exception:
            pass
    return socket_up


def read_boot_log(host, t0, label, out):
    try:
        log = get(host, "/bootlog", timeout=12)
    except Exception as exc:
        out.write(f"# {label} bootlog: {exc}\n")
        return
    out.write(f"# {label} bootlog {len(log)} bytes at {time.time() - t0:.1f}s\n")
    for line in log.splitlines():
        out.write(f"{label} | {line}\n")
    out.flush()


def capture(host, label, seconds, settle, console, out):
    t0 = time.time()
    restart(host, out, label)
    up = wait_for_restart(host, t0, 40.0)
    out.write(f"# {label} http up at {up}\n")

    if console:
        follow_console(host, t0, seconds, label, out)
    else:
        while time.time() - t0 < settle:
            time.sleep(0.5)
        read_boot_log(host, t0, label, out)

    try:
        out.write(f"# {label} geometry {get(host, '/geometry')}\n")
    except Exception as exc:
        out.write(f"# {label} geometry: {exc}\n")
    out.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--boots", type=int, default=1)
    parser.add_argument("--seconds", type=float, default=30.0,
                        help="how long to follow the console, per boot")
    parser.add_argument("--settle", type=float, default=40.0,
                        help="with --no-console, how long before reading /bootlog")
    parser.add_argument("--no-console", action="store_true",
                        help="never touch port 81; read /bootlog over HTTP instead")
    parser.add_argument("--first-label", type=int, default=1)
    parser.add_argument("--out", default="-")
    args = parser.parse_args()

    out = sys.stdout if args.out == "-" else open(args.out, "a")
    for i in range(args.boots):
        capture(args.host, f"b{args.first_label + i}", args.seconds,
                args.settle, not args.no_console, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
