#!/usr/bin/env python3
"""
gbsc_pro_flash.py — native Linux flasher for the RetroScaler GBSC Pro AV module.

Replaces the Windows-only GBSC_PRO_Programmer.exe. The AV module is an HDSC
HC32F460 whose resident bootloader speaks a lightly-customised YMODEM over a
USB-CDC serial port. This is the firmware that changes between GBSC Pro
releases' AV-module bins (e.g. the v1.3 "1X crop" / 525p-625p autoswitch fix).

Design: the YMODEM transfer itself is done by the vendored, unmodified
`ymodem` library (MIT, alexwoo1900) in ./vendor — a tested base. This file is
only the thin glue the library can't cover:
  * the `HCMGBoot.` -> U -> 1 -> "Enter download" bootloader handshake, and
  * selecting the library's "CP/M YAM" style + 128-byte packets, which yields
    the bootloader's non-standard filename-only block 0 (no size field).

See PROTOCOL.md for the reverse-engineered wire spec and provenance.

Usage:
    ./gbsc_pro_flash.py firmware/GBSC_PRO_AV_MODULE_v1.3.bin
    ./gbsc_pro_flash.py --port /dev/ttyACM0 firmware.bin
    ./gbsc_pro_flash.py --list       # list serial ports
    ./gbsc_pro_flash.py --probe      # read-only: print the bootloader banner
    ./gbsc_pro_flash.py --selftest   # verify config/CRC without hardware

The ymodem library comes from
`pip install -r requirements.txt`.
"""

import argparse
import os
import sys
import time

from ymodem.Protocol import ProtocolType
from ymodem.Socket import ModemSocket

# USB identity the HC32 bootloader enumerates with (VID:PID).
GBSC_VID = 0x2E88
GBSC_PID = 0x4603
BAUD = 115200

# Library style whose YMODEM feature set is filename-only (no length/date/mode/SN),
# which is exactly what the HC32 bootloader's block 0 expects.
YMODEM_STYLE = "CP_M_YAM"


class FlashError(Exception):
    """A recoverable protocol/connection failure while flashing."""


# --------------------------------------------------------------------------- #
#  Port discovery                                                             #
# --------------------------------------------------------------------------- #

def find_port():
    """Return the /dev path of the GBSC Pro bootloader, or None."""
    from serial.tools import list_ports
    for p in list_ports.comports():
        if p.vid == GBSC_VID and p.pid == GBSC_PID:
            return p.device
    return None


def list_ports_cmd():
    from serial.tools import list_ports
    found = False
    for p in list_ports.comports():
        vid = f"{p.vid:04X}" if p.vid is not None else "----"
        pid = f"{p.pid:04X}" if p.pid is not None else "----"
        tag = "  <-- GBSC Pro" if (p.vid == GBSC_VID and p.pid == GBSC_PID) else ""
        print(f"  {p.device}  VID:{vid} PID:{pid}  {p.description}{tag}")
        found = True
    if not found:
        print("  (no serial ports found)")


# --------------------------------------------------------------------------- #
#  Serial + handshake (condition waits, no fixed sleeps)                       #
# --------------------------------------------------------------------------- #

def _open_serial(port):
    import serial
    try:
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = BAUD
        ser.bytesize = serial.EIGHTBITS
        ser.parity = serial.PARITY_NONE
        ser.stopbits = serial.STOPBITS_ONE
        ser.timeout = 0.3
        ser.write_timeout = 2.0
        ser.dtr = True
        ser.rts = True
        ser.open()
        return ser
    except serial.SerialException as e:
        raise FlashError(f"could not open {port}: {e}") from e


def _read_until_any(ser, needles, timeout):
    """Accumulate input until one of `needles` appears, or raise on timeout."""
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            buf += chunk
            if any(n in buf for n in needles):
                return bytes(buf)
    raise FlashError(
        "cannot enter upgrade mode — no expected banner seen. Connect the AV "
        "module in bootloader mode and power-cycle it, then retry. "
        f"(received: {bytes(buf)!r})")


def _enter_download_mode(ser):
    """Drive the HC32 bootloader into YMODEM download mode.

    The bootloader emits its `HCMGBoot.` banner once on entry; by the time we
    open the port it is usually already gone and the bootloader waits silently
    for input. So we read whatever banner is still buffered but do NOT require
    it — presence on USB 2E88:4603 already means bootloader mode — and actively
    drive the menu (`U` then `1`), requiring the `Enter download` prompt in
    reply. Only menu characters are sent here; no firmware is written until the
    prompt is seen, so a failed handshake is harmless.
    """
    banner = bytearray()
    deadline = time.monotonic() + 1.5
    while time.monotonic() < deadline:
        if ser.in_waiting:
            banner += ser.read(ser.in_waiting)
            if b"Enter  1" in banner or b"HCMGBoot." in banner:
                break
        else:
            time.sleep(0.05)
    if b"Enter  1" in banner:
        return
    if ser.in_waiting:
        ser.read(ser.in_waiting)
    ser.write(b"U")
    time.sleep(0.2)
    if ser.in_waiting:
        ser.read(ser.in_waiting)
    ser.write(b"1")
    _read_until_any(ser, [b"Enter download"], timeout=3.0)


def _channel(ser):
    """Return (read, write) callbacks in the shape ModemSocket expects."""
    def _read(size, timeout=1):
        ser.timeout = timeout
        return ser.read(size)

    def _write(data, timeout=1):
        ser.write_timeout = timeout
        return ser.write(data)

    return _read, _write


# --------------------------------------------------------------------------- #
#  Flash                                                                       #
# --------------------------------------------------------------------------- #

def run_flash(ser, path, callback=None):
    """Handshake + YMODEM send over an already-open serial-like object.

    Raises FlashError on failure. Split out from flash() so it can be driven by
    the loopback test against a fake serial channel.
    """
    _enter_download_mode(ser)
    modem = ModemSocket(*_channel(ser), protocol_type=ProtocolType.YMODEM,
                        packet_size=128, style_id=YMODEM_STYLE)
    if not modem.send([path], callback=callback):
        raise FlashError("YMODEM transfer did not complete")


def _progress(_task_index, name, total, sent):
    pct = sent * 100 // max(total, 1)
    print(f"\r    {name}: {sent}/{total} bytes ({pct}%)", end="", flush=True)


def flash(port, path):
    size = os.path.getsize(path)
    if size == 0:
        print("[!] firmware file is empty")
        return 1
    print(f"[*] Firmware: {os.path.basename(path)} ({size} bytes)")
    print(f"[*] Port:     {port} @ {BAUD} 8N1")
    try:
        ser = _open_serial(port)
    except FlashError as e:
        print(f"[!] {e}")
        return 1
    try:
        print("[*] Entering upgrade mode...")
        _enter_download_mode(ser)
        print("[*] In download mode. Sending firmware...")
        t0 = time.time()
        modem = ModemSocket(*_channel(ser), protocol_type=ProtocolType.YMODEM,
                            packet_size=128, style_id=YMODEM_STYLE)
        ok = modem.send([path], callback=_progress)
        print()
        if not ok:
            raise FlashError("YMODEM transfer did not complete")
        print(f"[+] Update success! Time-consuming: {time.time() - t0:.1f} seconds")
        return 0
    except FlashError as e:
        print(f"[!] {e}")
        return 1
    finally:
        ser.close()


# --------------------------------------------------------------------------- #
#  Probe (read-only)                                                          #
# --------------------------------------------------------------------------- #

def probe(port):
    """Print whatever the bootloader emits, sending nothing (cannot write flash)."""
    import re
    try:
        ser = _open_serial(port)
    except FlashError as e:
        print(f"[!] {e}")
        return 1
    try:
        print(f"[*] Listening on {port} @ {BAUD} for ~3s (sending nothing)...")
        buf = bytearray()
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            chunk = ser.read(max(1, ser.in_waiting))
            if chunk:
                buf += chunk
        if not buf:
            print("[!] No output. The AV module is almost certainly NOT in bootloader "
                  "mode. Power-cycle it with USB connected and retry --probe promptly.")
            return 1
        text = buf.decode("latin1", errors="replace")
        print("=== bootloader output (verbatim) ===")
        print(text)
        print("=== hex ===")
        print(buf.hex(" "))
        hits = sorted(set(re.findall(r"[Vv]?\d+\.\d+[\w.-]*", text)))
        print("[i] possible version token(s):", ", ".join(hits) if hits else "none")
        return 0
    finally:
        ser.close()


# --------------------------------------------------------------------------- #
#  Self-test (no hardware)                                                    #
# --------------------------------------------------------------------------- #

def selftest():
    from ymodem.CRC import calc_crc16
    from ymodem.Protocol import ProtocolStyleManagement, YMODEM

    ok = True

    v = calc_crc16(b"123456789")
    print(f"ymodem CRC-16 check 0x{v:04X} (expect 0x31C3) {'OK' if v == 0x31C3 else 'FAIL'}")
    ok &= v == 0x31C3

    # The chosen style must NOT include the length field -> filename-only block 0.
    style = ProtocolStyleManagement().get_available_style(YMODEM_STYLE)
    feats = style.get_protocol_features(ProtocolType.YMODEM)
    no_len = not (feats & YMODEM.USE_LENGTH_FIELD)
    print(f"style {YMODEM_STYLE}: filename-only block 0 (no length field) "
          f"{'OK' if no_len else 'FAIL'}")
    ok &= no_len

    # A 128-byte SOH modem must build header [SOH, 0, 255] for sequence 0.
    modem = ModemSocket(lambda *a: b"", lambda *a: 0,
                        protocol_type=ProtocolType.YMODEM, packet_size=128,
                        style_id=YMODEM_STYLE)
    hdr = modem._make_send_header(modem._packet_size, 0)
    hdr_ok = bytes(hdr) == bytes([0x01, 0x00, 0xFF]) and modem._packet_size == 128
    print(f"SOH/128 header for seq 0 {'OK' if hdr_ok else 'FAIL'}")
    ok &= hdr_ok

    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(
        description="Flash the RetroScaler GBSC Pro AV module (HC32) over YMODEM.")
    ap.add_argument("firmware", nargs="?",
                    help="AV-module .bin (e.g. GBSC_PRO_AV_MODULE_v1.3.bin)")
    ap.add_argument("--port", help="serial device (default: auto-detect USB 2E88:4603)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--probe", action="store_true",
                    help="read-only: print the bootloader banner (sends nothing)")
    ap.add_argument("--selftest", action="store_true",
                    help="verify config/CRC without hardware")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if args.list:
        list_ports_cmd()
        return 0

    if args.probe:
        port = args.port or find_port()
        if not port:
            print("[!] GBSC Pro bootloader not found (USB 2E88:4603). "
                  "Connect the AV module in bootloader mode, or pass --port.")
            return 1
        return probe(port)

    if not args.firmware:
        ap.error("firmware .bin path is required (or use --list / --probe / --selftest)")

    port = args.port or find_port()
    if not port:
        print("[!] GBSC Pro bootloader not found (USB 2E88:4603). "
              "Is the AV module connected and in bootloader mode? "
              "Try --list, or pass --port explicitly.")
        return 1
    return flash(port, args.firmware)


if __name__ == "__main__":
    sys.exit(main())
