# Flashing over WiFi

`make -C build flash-ota HOST=<ip>` reflashes the unit without a cable. It
builds, arms the unit, and uploads.

## Why it needs an inbound port on the host

espota is not an upload to the unit. It is an invitation:

1. espota opens a **TCP listener on the host** and sends a UDP datagram to the
   unit's port 8266 naming that port.
2. The unit replies `OK` on the same UDP flow.
3. The unit **opens a TCP connection back to the host** and the image travels
   over that.

Step 2 crosses the host firewall on a flow conntrack already knows about, so it
always works. Step 3 is an unsolicited inbound SYN, and a default-drop firewall
discards it. Nothing on either side reports an error: espota sits at
`Uploading` until it times out, which is indistinguishable from a unit that is
not listening at all.

espota otherwise picks its host port at random in 10000-60000. `build/Makefile`
pins it (`OTA_HOST_PORT`, 8266) and calls espota directly instead of going
through `arduino-cli upload --protocol network`, so the firewall opens one port
rather than the range. The rule is in nix-config,
`modules/nixos/electronics.nix`.

## Telling the two failure modes apart

`ota_probe.py` sends the invitation and nothing else, so it is safe to run
against a working picture:

```sh
curl 'http://192.168.88.108/sc?c'
python3 tools/gbsc-pro-hwtest/ota_probe.py --host 192.168.88.108
```

| what it prints | what is wrong |
|---|---|
| no reply on UDP 8266 | not armed, or `loop()` is not running to read the command |
| `OK`, then the TCP connection never arrives | the host firewall, or routing |
| `CONNECTED` | the path is good; a failing upload is something else |

**Port 8266 on the unit is UDP.** A TCP probe of it finds nothing whether OTA is
running or not, so `nc -z` and a TCP port scan are both silent about the thing
they appear to be testing.

## Arming

`rto->allowUpdatesOTA` defaults to false and `ArduinoOTA.handle()` runs only
when it is true. `/sc?c` sets it, via the serial command queue — which is read
from `loop()`, so arming needs a running firmware loop, not just a live HTTP
stack. `flash-ota` arms the unit itself and waits for the queue to be read.

A reboot disarms it again.

## What still needs a cable

- A unit that has stopped answering HTTP cannot be armed, which is exactly when
  a reflash is wanted.
- Changes to early boot or to WiFi setup, where a bad image leaves nothing to
  reflash through.

A failed OTA is not a brick: the image is staged and the checksum verified
before the swap, so a truncated transfer leaves the running firmware in place.

`ArduinoOTA.onStart()` calls `LittleFS.end()`, so stored timings and preferences
survive an upload.

Attached tooling does not need closing first. A `regpanel.py` open for four
hours sat through two full uploads; the unit reboots at the end, and the panel
reconnects.
