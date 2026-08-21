#!/usr/bin/env python3
"""Which half of the ArduinoOTA handshake fails.

    curl 'http://192.168.88.108/sc?c'          # arm first, every time
    python3 tools/gbsc-pro-hwtest/ota_probe.py --host 192.168.88.108

espota does two things, and `make -C build flash-ota` reports both the same
way. First a UDP invitation to port 8266 on the unit, naming a TCP port on
THIS machine. Then it waits for the unit to connect back. The invitation
travels on a flow the host's conntrack already knows about; the reverse
connection is an unsolicited inbound SYN, so a host firewall drops it and the
unit looks dead when it is not.

Sends the invitation and nothing else -- the unit is never flashed. It stays
armed afterwards, so this is safe to run against a working picture.
"""

import argparse
import hashlib
import socket
import sys

DEVICE_PORT = 8266
FLASH = 0


def probe(host, device_port, host_port, timeout):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", host_port))
    listener.listen(1)
    listener.settimeout(timeout)
    port = listener.getsockname()[1]

    payload = b"probe"
    invitation = "%d %d %d %s\n" % (FLASH, port, len(payload),
                                    hashlib.md5(payload).hexdigest())

    print(f"1. UDP invitation -> {host}:{device_port}, "
          f"asking it to connect back to :{port}")
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(timeout)
    try:
        udp.sendto(invitation.encode(), (host, device_port))
        reply, _ = udp.recvfrom(128)
    except socket.timeout:
        print(f"   NO REPLY -- nothing listening on UDP {device_port}.")
        print("      Either it was never armed (curl 'http://%s/sc?c') or"
              % host)
        print("      loop() is not running, so the command was never read.")
        return 1
    except OSError as error:
        print(f"   ERROR: {error}")
        return 1

    if not reply.startswith(b"OK"):
        print(f"   REFUSED: {reply!r}")
        return 1
    print(f"   REPLY {reply!r} -- the unit is armed and listening.")

    print(f"2. waiting {timeout}s for the unit to open TCP back to :{port}")
    try:
        connection, address = listener.accept()
    except socket.timeout:
        print("   TIMED OUT -- the unit accepted, its connection never arrived.")
        print("      That is this machine's firewall or routing, not the unit.")
        print(f"      Open TCP {port} inbound; see modules/nixos/electronics.nix")
        print("      in nix-config.")
        return 2
    connection.close()
    print(f"   CONNECTED from {address[0]}:{address[1]} -- OTA is viable.")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.88.108")
    parser.add_argument("--device-port", type=int, default=DEVICE_PORT)
    parser.add_argument("--host-port", type=int, default=DEVICE_PORT,
                        help="TCP port the unit connects back to; 0 for any. "
                             "Matches build/Makefile's pinned OTA_HOST_PORT, "
                             "which is what the firewall opens.")
    parser.add_argument("--timeout", type=float, default=10.0)
    options = parser.parse_args()
    return probe(options.host, options.device_port,
                 options.host_port, options.timeout)


if __name__ == "__main__":
    sys.exit(main())
