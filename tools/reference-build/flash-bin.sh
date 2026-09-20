#!/usr/bin/env bash
# Flash a prebuilt image over OTA, for switching between firmwares to compare
# them. `make -C build flash-ota` always builds first, which is the wrong shape
# when the image already exists and is the point of the exercise.
#
#   tools/reference-build/flash-bin.sh <host> <image.bin>
#
# The unit arms itself: /sc?c queues the command and loop() has to run to read
# it, so a 200 is not evidence it armed -- ota_probe.py is what distinguishes
# an unarmed unit from a blocked one. Port 8266 on the unit is UDP; espota
# opens a TCP listener HERE and the unit connects back to it, so a default-drop
# firewall on this machine looks exactly like a dead unit.
set -euo pipefail

host=${1:?usage: flash-bin.sh <host> <image.bin>}
image=${2:?usage: flash-bin.sh <host> <image.bin>}
[ -f "$image" ] || { echo "no such image: $image" >&2; exit 1; }

here=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
espota=$(ls "$here"/build/data/packages/esp8266/hardware/esp8266/*/tools/espota.py 2>/dev/null | head -1)
[ -n "$espota" ] || { echo "espota.py not found -- run: make -C build setup" >&2; exit 1; }

echo "arming $host"
curl -fsS -o /dev/null "http://$host/sc?c"
sleep 1
python3 "$espota" --ip "$host" --port 8266 --host_port 8266 --file "$image" --progress
