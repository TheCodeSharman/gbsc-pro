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

## A transfer that keeps dying with the path good

`CONNECTED` and an upload that still stops at a few per cent is the third case,
and **IT IS THE LINK. ASK THE PACKET LOSS BEFORE RETRYING ANYTHING.** An armed
unit that replies `OK`, connects back, and then drops the image is the
discriminator for a degraded association -- not the heap and not the firewall.
`known-issues.md` carries fifteen consecutive failures under it, thirteen at
0 to 5% and one at 66%, **across four `/restart`s**, with the link at 68 to
100% loss throughout.

```sh
ping -c 20 <ip>                 # the first thing to run, not the last
ssh router "iw dev phy1-ap0 station dump"      # signal, bitrate, tx retries
```

Measured 2026-11-07: five attempts failed at 0% and 9% and three times with
`Error Uploading`, `ota_probe.py` answering `OK` then `CONNECTED` either side,
while the same window gave **100% ICMP loss once and HTTP dead for 32 s once**,
both recovering unaided, with the station dump reading **signal -49 dBm over a
range of [-83, -49] and 19 tx retries in 101 packets**. Layer 2 associates and
holds; the transfer does not survive the fades.

**THE HEAP IS NOT THE CANDIDATE, AND THE EXPERIMENT THAT NAMED IT CANNOT
ATTRIBUTE ONE.** Measured 2026-10-14, seven attempts died at 0 to 4% and a
`/restart` before the eighth carried the whole image; `/bootlog` read 9840
bytes at both boots against a live 8776 before the restart. **A restart
re-associates the WiFi as well as clearing the heap**, so a recovery after one
is explained equally by a fresh association on a flaky link -- and the four
restarts above did not rescue it, which is the direction that settles it. The
8776 reading is real and costs `/fs/download`, which needs 10000; it is not
evidence about the transfer.

So **measure the loss first**. Over a clean link, retrying two or three times
is the move; over a bad one, no number of retries or restarts will carry the
image and USB is the route.

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
