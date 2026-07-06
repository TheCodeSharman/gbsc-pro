#!/usr/bin/env python3
"""
End-to-end loopback test for gbsc_pro_flash.py — no hardware required.

Runs the real flasher (run_flash: HCMGBoot handshake + the ymodem library's
YMODEM send) against an emulated HC32 bootloader over an in-memory socket pair,
then checks the bytes the "device" received exactly reconstruct the input
firmware. A pass means the handshake + the library's transfer choreography work
against a receiver that behaves like the real bootloader.

Run:
    python3 tools/gbsc-pro-flasher/test_loopback.py [file.bin]
"""

import binascii
import fcntl
import importlib.util
import os
import socket
import struct
import sys
import termios
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "gbsc_pro_flash", os.path.join(HERE, "gbsc_pro_flash.py"))
fl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fl)

SOH, EOT, ACK = 0x01, 0x04, 0x06


class FakeSerial:
    """Minimal pyserial-compatible shim over one end of a socketpair."""

    def __init__(self, sock):
        self._s = sock
        self.timeout = 1.0
        self.write_timeout = 2.0
        self.dtr = False
        self.rts = False

    def read(self, n=1):
        self._s.settimeout(self.timeout)
        buf = b""
        while len(buf) < n:
            try:
                chunk = self._s.recv(n - len(buf))
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
        return buf

    def write(self, data):
        self._s.sendall(bytes(data))
        return len(data)

    @property
    def in_waiting(self):
        return struct.unpack("I", fcntl.ioctl(self._s, termios.FIONREAD, b"\0\0\0\0"))[0]

    def close(self):
        pass


def _check_packet(pkt, expected_seq):
    assert pkt[1] == expected_seq, f"seq {pkt[1]} != expected {expected_seq}"
    assert (pkt[1] + pkt[2]) & 0xFF == 0xFF, "seq/~seq mismatch"
    payload = pkt[3:131]
    got = (pkt[131] << 8) | pkt[132]
    want = binascii.crc_hqx(payload, 0)
    assert got == want, f"CRC {got:#06x} != {want:#06x}"


def emulator(sock, received):
    """Stand-in HC32 bootloader; records the reconstructed firmware into `received`."""
    def recv_exact(n, timeout=8.0):
        sock.settimeout(timeout)
        buf = b""
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk:
                raise RuntimeError("device: connection closed")
            buf += chunk
        return buf

    # Banner + menu handshake (mirrors the vendor tool's U / 1 sequence).
    sock.sendall(b"\r\nHCMGBoot.\r\n")
    assert recv_exact(1) == b"U", "expected wake char 'U'"
    sock.sendall(b"\r\n1: Download\r\n2: Upload\r\n3: Execute\r\n")
    assert recv_exact(1) == b"1", "expected download key '1'"
    sock.sendall(b"\r\nEnter download\r\n")

    # YMODEM receiver: pump 'C' until block 0 arrives (real receiver behaviour).
    sock.settimeout(0.3)
    first = b""
    for _ in range(200):
        sock.sendall(b"C")
        try:
            first = sock.recv(1)
        except socket.timeout:
            continue
        if first:
            break
    assert first == bytes([SOH]), f"expected SOH block 0, got {first!r}"
    pkt = first + recv_exact(132)
    _check_packet(pkt, 0)
    sock.sendall(bytes([ACK]))       # ACK block 0
    sock.sendall(b"C")               # request first data block

    blocks = []
    expected = 1
    while True:
        lead = recv_exact(1)
        if lead == bytes([EOT]):
            sock.sendall(bytes([ACK]))
            break
        assert lead == bytes([SOH]), f"unexpected leader byte {lead!r}"
        pkt = lead + recv_exact(132)
        _check_packet(pkt, expected)
        blocks.append(pkt[3:131])
        expected = (expected + 1) & 0xFF
        sock.sendall(bytes([ACK]))

    # Batch-end null header (the library writes it without waiting for our 'C').
    tail = recv_exact(133)
    assert tail[0] == SOH and tail[1] == 0 and all(b == 0 for b in tail[3:131]), \
        "malformed batch-end packet"
    try:
        sock.sendall(bytes([ACK]))
    except OSError:
        pass

    received["data"] = b"".join(blocks)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "firmware", "GBSC_PRO_AV_MODULE_v1.3.bin")
    fw = open(path, "rb").read()
    print(f"[test] firmware: {os.path.basename(path)} ({len(fw)} bytes)")

    host_sock, dev_sock = socket.socketpair()
    received, err = {}, {}

    def dev_thread():
        try:
            emulator(dev_sock, received)
        except Exception as e:  # noqa: BLE001
            err["dev"] = e

    t = threading.Thread(target=dev_thread)
    t.start()

    ser = FakeSerial(host_sock)
    send_ok = True
    try:
        fl.run_flash(ser, path)
    except Exception as e:  # noqa: BLE001
        send_ok = False
        print(f"[test] run_flash raised: {e}")
    print(f"[test] run_flash completed: {'OK' if send_ok else 'FAIL'}")

    t.join(timeout=15)
    if err:
        print(f"[test] EMULATOR ERROR: {err['dev']}")
        return 1

    got = received.get("data", b"")
    reconstructed = got[:len(fw)]
    pad = got[len(fw):]
    match = reconstructed == fw
    pad_ok = all(b == 0x1A for b in pad)
    print(f"[test] received {len(got)} bytes; matches firmware: {'OK' if match else 'FAIL'}")
    print(f"[test] trailing padding is 0x1A only: {'OK' if pad_ok else 'FAIL'}")

    passed = send_ok and match and pad_ok
    print("\nLOOPBACK", "PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
