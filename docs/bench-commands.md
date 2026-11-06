# Commands

```sh
nix develop                       # tools: arduino-cli, make, esptool, python3+pytest
make -C build setup               # once: esp8266 core + libs (~440M into build/)
make -C build                     # compile -> build/output/gbs-control.ino.bin
make -C build flash-ota HOST=…    # upload over WiFi — arms the unit itself
make -C build flash               # upload over USB serial (PORT=/dev/ttyUSB0)

make -C test                      # all 54 host suites, ~20 s — ALREADY PARALLEL
make -C test video-path adc       # just the suites under work

pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 -v
pytest tools/gbsc-pro-hwtest/ -q  # no --host: hardware tests skip, unit tests run

ruff check tools                  # BEFORE any hardware run
```

**RUN THE HOST SUITE ONCE AND READ THE STATUS OUT OF THAT RUN.** `test/Makefile`
sets `MAKEFLAGS += -j$(shell nproc)`, so a bare `make -C test` already builds and
runs every suite at once in about twenty seconds. Invoking it a second time in
the same command -- once to count failures, once for the status line -- doubles
the wall clock, and that is where an apparent three-minute suite comes from.
While iterating, name the suites under work instead of running all of them --
but **the full suite runs once before any commit**, because a suite nobody named
is where a change to a shared constant lands.

**The way this rule gets broken is a SECOND PIPELINE, not a second command.**
`make -C test 2>&1 | grep ERROR; make -C test 2>&1 | grep -c SUCCESS` reads as
one line of shell and runs the whole suite twice. So does any `&&` pair that
greps the same target for two different things. **Redirect the run to a file
once and grep the file** -- which also fixes the hazard in
[[never-pipe-make-into-grep]], since grep's exit status otherwise masks a failed
target:

```sh
nix develop -c make -C test >/tmp/test.log 2>&1; echo "make: $?"
grep -E "ERROR|FATAL" /tmp/test.log
```

**`ruff check tools` before every hardware run.** A pytest module referencing a
name it never imported imports fine and fails five minutes in, against a live
unit and a source that has to be put back afterwards; two of those cost two runs
before the linter was in the dev shell, and it found a third the moment it
arrived. `ruff.toml` scopes it to faults rather than style, so a clean run means
something.

**Drive the SOURCE from here too.** ModeServ runs on the RISC PC, TCP 6502, one
command per connection -- the close is the end of the reply. It lives in the
`RiscPc` repo, `tools/video-source/`.

```sh
printf 'MODE X320 Y256 C256 F50\n' | nc 192.168.88.10 6502   # the bench mode
printf 'PATTERN CARD\n'            | nc 192.168.88.10 6502   # green border on all four edges
printf 'ANIM OFF\n'                | nc 192.168.88.10 6502   # stop the flashing, so a still is evidence
printf 'MODES\n'                   | nc 192.168.88.10 6502   # what this monitor definition allows
printf 'SYNC 1\n'                  | nc 192.168.88.10 6502   # 0 separate, 1 composite, 3 auto
printf 'PING\n'                    | nc 192.168.88.10 6502   # OK ModeServ 1
printf 'VERSION\n'                 | nc 192.168.88.10 6502   # which build is RUNNING
```

**`VERSION` FIRST, BEFORE JUDGING ANYTHING THE CARD DOES.** It answers
`OK ModeServ <build> PatLib <build>`, and it names both because the two files
tokenise and load separately -- a `Build` that lands one and not the other
leaves a server whose halves disagree and whose behaviour never says so. Without
it, "the feature is missing" and "the feature is there and broken" are
indistinguishable from this end, and a session has been spent inventing probes
to guess between them.

**A MODE READ BACK WITH `F-1` IS A SOURCE THAT IS NOT EMITTING A USABLE RASTER,
AND EVERYTHING DOWNSTREAM OF IT READS AS A SCALER FAULT.** `SYNC` and `MODE`
answer with the mode read back from the hardware, frame rate included, and a
frame rate of `-1` means VIDC20 has no field rate to report -- so the reply
names the right resolution while the machine is not driving a raster the scaler
can acquire. From this end it looks exactly like a dead board: `own V sync: no
after 1001ms` on a source whose sync type is separate, `STATUS_SYNC_PROC_VTOTAL`
97, `HSACT` 1 with no vertical count, both inputs failing, and neither `/sc?~`,
`/restart` nor a mains and USB power cycle clearing it. **Re-issuing `MODE`
fixes it outright**, and the same source then acquires in about four seconds.
**So read the frame rate back before diagnosing the scaler**, and re-issue the
mode before reaching for the HC32 -- a measurement taken against `F-1` is
evidence about nothing, and a build compared across one is a build compared
across two different sources.

**WHICH MONITOR DEFINITION IS LOADED IS WHAT `MODES` SAYS, AND NOTHING ELSE
SAYS IT.** 28 modes at C256, none above 37.9 kHz, is the stock AKF50; 80 modes
reaching 1280x720 and 1920x1080 is `RetroScaler-Acorn.mdf`. ModeServ has no
command to switch, so a change of file is a bench trip, and
`framing_sweep.py --mdf` refuses a file `MODES` does not match, so a measurement
is never scored against timings the machine is not running. A nameless mode is
still reachable by `MODE`. `docs/bench-sources.md`.

**THE STOCK AKF50 IS THE ACCEPTANCE SET FOR DEFAULT FRAMING, because it is what
a stock RISC PC emits.** Its VESA-rate entries keep VESA's line total and
(nearly) its sync width but start the display a few pixels earlier and fill the
difference with black border -- 640x480@60 at 138 of 800 against DMT's 144,
800x600@56 at 190 against 200, 640x480@75 at 170 against 184. A border is black
active video, indistinguishable from porch on the wire, so the chip forms the
same key and places the picture where DMT puts it, 6 / 10 / 14 px off. Moving
pixels between porch and border on its own moves nothing: the display starts at
sync + porch + border whatever the split, and the card's frame marks the
display, not the border. `published_rasters.py <file>` gives the expected offset
per mode, and a measured offset equal to it is by design.

**`RetroScaler-Acorn.mdf` CARRIES NO VESA DMT SET, AND THE OFFSETS ABOVE APPLY
UNDER IT.** It did carry one, and because `MODE` picks by X/Y/F alone the
colliding Acorn entry was dropped to keep both nameable -- which made the
generated file disagree with the definition installed on the machine, silently,
since both spellings are the same X, Y and F and `MODES` lists names rather than
timings. Every framing scored against the generated file was then scored against
timings the machine does not emit, and 1024x768@60 read 71 source pixels out on
that account alone. **So the machine emits Acorn's rasters, the engine places a
source from the STANDARD where one states the same resolution, and the offset
that costs is by design** -- `published_rasters.py <file>` states it per mode.
The regenerated file is 63 entries against the 63 `MODES` reports, with no key
in one and not the other.

`MODE` replies with the mode read back from the hardware, never with the
request, so a monitor definition that cannot do what was asked does not look
like a fault in the scaler. **THE REPLY IS NOT WHEN THE TIMING CHANGED** --
VIDC20 changes on a register write and the scaler sees the source leave within
~70 ms, while the reply waits on the repaint. Timing a transition from the reply
charges the source for two thirds of a mode change that is entirely the
engine's; measure from the console instead. **It repaints the card too**, because the default
signal after a mode change is black with a flashing cursor, which reads from
here as a scaler with no output and has been diagnosed as one. **A session can therefore change the source without
anyone at the bench**, which is what makes the mode-change recovery below usable
unattended.

`SYNC` changes the machine's sync type and re-applies the mode so the change
reaches VIDC20's external register -- no reboot, no bench trip. It replies with
the type read back and the mode it landed in. **This is what makes the RISC PC a
composite-sync source as well as a separate-sync one**, so the two sync types can
be judged against one input, one cable and one raster, with nothing else moving.

**EVERY HARDWARE FLAG DEFAULTS ON, SO `pytest --host=…` IS THE WHOLE SUITE.**
`--source`, `--preset-load`, `--preset-save`, `--reboot`, `--freeze` and
`--pllad-hostile` are opt-OUT, and `--modeserv` defaults to the bench source --
a run that skips a leg says so rather than a flag nobody passed deciding it.
`--all-modes` is the one addition, judging framing on every mode the monitor
definition offers rather than one per solve behaviour, and costs minutes.
Without `--host` everything hardware skips, so a bare `pytest` stays useful.

### Flashing

**OTA is the everyday path and works.**

```sh
make -C build flash-ota HOST=192.168.88.108     # arms the unit itself
python3 tools/gbsc-pro-hwtest/ota_probe.py --host <ip>   # when an upload hangs
```

**AN UPLOAD THAT DIES PART WAY IS USUALLY TRANSIENT, AND RETRYING IS THE FIRST
MOVE.** Measured in one session: eleven attempts, five of which carried the
image, the failures stopping at 6%, 26% and 38% with the unit acquired, the
network at 0% loss and heap unchanged either side. espota times out after 10 s
without an ack, so anything that holds `loop()` that long drops the transfer and
nothing says which. **That is a different signature from the firewall below** —
a blocked handshake sits at `Uploading` and never advances, where this one
advances and stops. Retry two or three times before diagnosing; a stale
`FIN-WAIT-1` on port 8266 from the previous attempt also makes `ota_probe.py`
fail to bind, which reads as something holding the port.
**BUT RESTART THE UNIT AFTER TWO FAILURES RATHER THAN RETRYING A THIRD TIME.**
Seven consecutive attempts died at 0 to 4% with `ota_probe.py` reporting
`CONNECTED`, and a `/restart` immediately before the eighth carried the whole
image. The unit's live free heap is the candidate and `/bootlog` does not show
it -- 9840 bytes at both boots, against a live 8776 in the failing state.
`docs/ota-flashing.md`.

**What blocks OTA is the HOST firewall, not the unit.** espota opens a TCP
listener on this machine and the unit **connects back** to it, so the image
arrives over an unsolicited inbound connection that a default-drop firewall
discards — espota then sits at `Uploading` exactly as it would against a dead
unit. nix-config's `modules/nixos/electronics.nix` opens the one port
`build/Makefile` pins for it. `docs/ota-flashing.md`.

**Port 8266 on the unit is UDP.** A TCP probe finds nothing whether OTA is
running or not, so a TCP port scan is silent about the thing it appears to
test. That reading is what supported the earlier conclusion that OTA had never
worked; probed with UDP, an armed unit replies `OK`.

USB stays the recovery path, and is required when the change touches early boot
or WiFi setup, and when the unit has stopped answering HTTP — which is when it
cannot be armed at all:

```sh
ls /dev/ttyUSB*                          # CH340 on this board; enumerate before flashing
make -C build flash                      # PORT=/dev/ttyUSB0 by default
make -C build flash PORT=/dev/ttyUSB1
```

**A 200 from `/sc?c` is not evidence that the unit armed**, which is the trap:
the route only queues the command, and `loop()` has to run to read it. See the
async-server note in `bench-traps.md` for why HTTP answers even when the loop
does not run.
`ota_probe.py` is what distinguishes the two.

Two things that bite regardless of route, both detailed under the traps below:
**opening the serial port resets the board** (`stty -F /dev/ttyUSB0 115200 -hupcl
raw -echo` first), and **USB backfeeds power**, so leaving the cable attached
means later "power cycles" are not power cycles. Flashing preserves the filesystem
(`wipe=none` in the FQBN), so stored timings and preferences survive.
