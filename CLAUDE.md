# GBSC-Pro / RetroScaler

Fork of gbs-control for the GBSC-Pro board: a TV5725 video scaler driven by an
ESP8266, taking analog RGB/YPbPr/composite in and HDMI out. The everyday bench
source is a RISC OS RiscPC at 320x256@50 (VTOTAL 311), on `vga`.

**It is not the only source, and a change judged only against it is judged
against one input, one sync type and one scan mode.** A **Wii on the YPbPr
input** is the only source here for sync on green, for interlace, and for the
component colour path -- and YPbPr is a *direct analog* path, so its timings are
its own. Only composite and S-Video go through the ADV7280/ADV7391 chain, which
regenerates them to broadcast standard, so any argument resting on a source being
standard-conformant reaches those two and nothing else.
**BOTH ARE PLUGGED IN AND POWERED AT ONCE, ALL THE TIME, AND SWITCHING NEEDS NO
BENCH TRIP** -- `/input?src=vga` and `/input?src=ypbpr`, so a session judges a
change against both inputs without anyone touching a cable and without asking
first. **Neither source has to be arranged**: assuming the Wii needs setting up,
or proposing it as an experiment rather than reaching for it, is the same
mistake as not using it at all.

**ACQUISITION IS SECONDS ON BOTH INPUTS, SO A WAIT OF MINUTES IS A FAULT AND NOT
A SETTLE.** Measured on `vga`: `/input?src=vga` to `sampling: 311 lines x
50.08 Hz` in 7.2 s, `state: acquired` at 8.1 s, FrameSync steering by 10.4 s,
and the sync-type probe answering `own V sync: yes after 2ms`. Measured on
`ypbpr` with the Wii in 480p: `/input?src=ypbpr` to `sampling: 524 lines x
59.80 Hz -> line rate 31395` in 15.2 s, acquired and holding, `PLLAD_MD` 1096
against `STATUS_SYNC_PROC_HTOTAL` 1096, `SP_SOG_MODE` 1, and a clean legible
picture.
**Do not budget minutes for a source to appear**; a source that has not solved
in about ten seconds is not settling.

**ACQUISITION ON `ypbpr` DEPENDS ON THE OUTPUT MODE, BECAUSE THE OUTPUT DECIDES
THE DIVIDER AND SO DECIDES WHETHER THE SOURCE CAN BE MEASURED.** A ypbpr source
retrying for ever is the open defect in `docs/known-issues.md` rather than a
regression to diagnose. **Read which output is set before judging an
acquisition**, and compare a change against the same one at both ends.
**1920x1080 IS NOT ITSELF THE BAR**: measured 2026-10-20, the Wii at 480p
selected with `/input?src=ypbpr` acquires into 1920x1080 and holds --
`STATUS_SYNC_PROC_VTOTAL` 524, `STATUS_SYNC_PROC_HTOTAL` 1212 against
`PLLAD_MD` 1212, `SP_SOG_MODE` 1, coast 7/6 -- holding across a 183 s run of
27708 samples. An earlier reading that it never acquires there is superseded.

**AND `/geometry`'s `state` IS STALE ACROSS AN INPUT CHANGE.** Four seconds
after `/input?src=ypbpr` it answered `acquired` while
`STATUS_SYNC_PROC_VTOTAL` and `HTOTAL` both read 0 -- the field left over from
the source that just went away. **Poll the COUNT, not the state**, or an
acquisition is judged before detection has run.

**ESTABLISH WHICH WII OUTPUT MODE IS SET BEFORE JUDGING THE INPUT.** All three
solve and hold. 480p solves in 15 s. 480i alternates 259/260 and still acquires,
because `SteadyRun::agree()` treats a pair alternating by one as agreeing -- an
earlier reading that it never reaches `state: acquired` predates that. 576i
holds a steady count of 310 and solves in **about three seconds**, measured from
the `/input` request to `sync pad: driven`; an earlier reading of 40 s is
superseded.

**AND ONLY 480i IS DEINTERLACED.** 576i is steered as progressive and shown as a
single field, line doubled -- correct in shape and aspect at half the vertical
resolution, which is invisible without a reference. The same is true of the RISC
PC's `INTERLACE ON` modes. `docs/investigations/interlaced-source-measurement.md`.
`docs/investigations/mode-detect-answers-before-any-measurement.md`,
`docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md`.

**ALL THREE SYNC ARRANGEMENTS ARE ON THE BENCH AND ALL THREE ARE SCRIPTABLE.**
The RISC PC's sync type is one CMOS value rather than a mode-file setting, and
ModeServ sets it -- `SYNC 0` separate, `SYNC 1` composite, `SYNC 3` auto -- so
**a COMPOSITE-SYNC RGBHV source exists here**, on `vga`, and a branch keyed on
csync with RGBHV is bench testable rather than host-test-only. Sync on green is
the Wii on `ypbpr`. No sync arrangement is out of reach from a session.

**`docs/bench-sources.md` is what each source can prove; read it before
concluding a branch is untestable**, and it carries what each one is currently
known to do.

**THE OUTPUT IS READ BY A USB HDMI CAPTURE, NOT BY PHOTOGRAPHING THE PANEL.**
`hdmi_capture.py` gives the emitted frame itself, so a margin is a number rather
than a judgement and two states compare without controlling the room. The
television runs beside it off the dongle's loop-out, so both views are live at
once. **A BLACK CAPTURE IS A BAD SCALER CONFIGURATION UNTIL THE BOARD HAS BEEN
RULED OUT.** Measured: the blocks the low-power teardown holds down
(`SFTRST_MEM_RSTZ` and its four neighbours in `s0_46`) were left held after the
source acquired, and the part emitted nothing while every configuration register
read correct -- a clean `/getregs` is no evidence here. Ask `s0_46`, then `s0_45`
and `s0_49`, then the console's `frame time lock` line, whose `out` rate proves
the scaler is feeding the encoder.
**An unseated HDMI input at the dongle does the same and neither power cycle
clears it -- but it is the LAST resort rather than the first**, because a
connector that has been seated stays seated, and reaching for it ahead of the
board is how a real fault gets a session spent on the cable.
`docs/bench-output-capture.md`.

## Layout

| Path | What |
|---|---|
| `GBSC-Pro-Source code/gbs-control/` | the firmware. `gbs-control.ino` is ~10k lines; `src/tv5725/FrameSync.*` is frame time lock and `src/videosource/FrameTimeLock.*` is the gate deciding when it may run; the register map is declared by the subsystem that owns each block, under `src/tv5725/`, with whatever has no owner yet left in `Tv5725::Tv5725` |
| `build/` | `make`-driven arduino-cli build. `data/`, `output/`, `user/` are gitignored and large |
| `tools/gbsc-pro-hwtest/` | Python: pytest suite against a live unit, plus register/geometry/soak tooling |
| `docs/` | **`known-issues.md` is the register of open defects and unsettled questions -- read it before diagnosing anything, and put a new one there rather than in a handover. `chip-initialisation.md` is the design — code first, one class per subsystem in `Tv5725::`, and why the preset blobs are being deleted rather than tidied. Read it before adding a register write. `testing.md` is which test layer to use, the fake-Wire seam that makes firmware register code host-testable, and the poison/mutation disciplines.** TV5725 datasheet and register definitions; `scaler-geometry-model.md` is the measured arithmetic from capture window to output blanking registers — **read it before touching geometry**; `firmware-geometry-engine.md` is how `src/tv5725/` uses it and the rules that keep it correct; `aspect-ratio.md` is the shape a source is shown in — declared rather than measured, defaulted from the raster it matched, stored per source, and applied by narrowing the ROOM on one axis — **read it before touching `OutputWindow`'s room or `placePicture`**; `vesa-gtf.md` settles the capture-window default — select PAL or NTSC on field rate, no curve — and records why GTF was rejected. Read before proposing a blanking formula. `capture-limits.md` is the one bound on what arrives intact — the horizontal write limit — and the trade `PLLAD_MD` makes between sampling density and reaching the end of the line; `rgbhv-bypass-trap.md` is what decides whether an RGBHV source is scaled or bypassed, and which measurements bypass invalidates; `osd-menu.md` is the menu the remote drives — read it before touching anything called OSD, because three subsystems answer to that name and only the STV9426 reaches the television, and **`/menu?key=…` now drives the described tree and answers the page it would draw, so a menu change no longer needs a remote**; `sync-type-selection.md` is why the csync/separate-sync choice is circular and latches — read it before touching `syncTypeCsync` or believing `VSACT`; `preset-load-clobber.md` is what to read before rewriting preset loading; `whole-byte-convenience-names.md` is the inventory and order for removing the 25 non-datasheet byte-wide names; `preset-gap-datasheet-map.md` is every field the preset still owns, resolved against RD-5725-1.1. `MS9288A-Datasheet-Rev-B0.pdf` is the HDMI encoder's datasheet, in Chinese — pinout, application circuit and electrical characteristics, and **no register map**, which Macrosilicon release under NDA only; `EM638325-Industrial_Rev-3.2.pdf` is the **SDRAM part's** datasheet — the frame buffer is one EM638325TS-6, a 166 MHz bin, and it is what bounds the memory clock; `webui-build-chain.md` is the four-file UI chain, three of them checked-in artefacts — read it before editing anything under `public/`; `ota-flashing.md` is the espota handshake, why it needs an inbound port on the host, and how to tell an unarmed unit from a blocked one; `bench-output-capture.md` is the USB HDMI capture that reads the emitted frame, what it retires, and why a stream of black frames is the connector rather than the signal |
| `GBSC-AV-IR-v1.1-20240923.pdf` | the board schematic (KiCad, 14 sheets) |
| [gbsc-pro-bench-photos](https://github.com/TheCodeSharman/gbsc-pro-bench-photos) | **a separate repo** — the 67 bench photographs, 146 MB. Mirrors this repo's paths, so its tree drops into a checkout and lands ignored. *What each one shows stays here*, in `docs/photos/*/README.md` and `snapshots/LOG.md` |
| `gbsc-pro-bench-tools` | **a separate repo**, a sibling checkout — tooling for the bench *instruments*: a DSO Nano and a Rigol DS1000Z. **The dividing line: anything talking to the RetroScaler belongs here, anything driving one person's test gear belongs there.** |
| `RiscPc` | **a separate repo**, a sibling checkout — the bench *source*. `tools/video-source/` is the RISC OS BASIC that drives it: `ModeServ` sets the screen mode over TCP 6502, `ModeSweep` cycles the AKF50 modes, `TestPat`/`PatLib` draw the cards. **The dividing line: this scaler is not RISC PC specific, so anything that only makes sense on the RISC PC belongs there.** Read its `README.md` before running one — the sources need tokenising on the RISC OS side, and no `SYS` in them is proven except on hardware. |

## Commands

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

**`RetroScaler-Acorn.mdf` CARRIES THE VESA DMT SET, EXACTLY AS THE STANDARD
STATES IT**, and where an Acorn entry collides with a DMT one on resolution and
field rate the Acorn one gives way, because `MODE` picks by X/Y/F alone. A
source running a DMT entry has the raster the engine's published-raster
arithmetic assumes, which is what makes a framing testable rather than merely
plausible -- and is why a measurement taken under that file does not carry the
offsets above.

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

`--source` opts into tests needing a locked signal; `--preset-save` opts into
tests that write flash. Without `--host` everything hardware skips, so a bare
`pytest` stays useful.

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
async-server note below for why HTTP answers even when the loop does not run.
`ota_probe.py` is what distinguishes the two.

Two things that bite regardless of route, both detailed under the traps below:
**opening the serial port resets the board** (`stty -F /dev/ttyUSB0 115200 -hupcl
raw -echo` first), and **USB backfeeds power**, so leaving the cable attached
means later "power cycles" are not power cycles. Flashing preserves the filesystem
(`wipe=none` in the FQBN), so stored timings and preferences survive.

## READ THE CONSOLE FIRST. IT SAYS WHAT THE ENGINE IS DOING.

**The status WebSocket is the cheapest instrument on the board and it answers
before any register can.** A register dump is a snapshot of a machine that
re-solves itself every few seconds; several faults here are a SEQUENCE rather
than a state, and the console is the only place a sequence is visible. Reach for
it first, ahead of `/getregs`, `/geometry` and a photograph.

```sh
nix develop -c python3 - <<'PY'
import websocket, time
ws = websocket.create_connection("ws://192.168.88.108:81", subprotocols=["arduino"], timeout=5)
ws.settimeout(1); t0 = time.time()
while time.time() - t0 < 90:
    try: print(f"{time.time()-t0:7.2f}  {ws.recv().strip()}", flush=True)
    except Exception: pass
PY
```

**TIMESTAMP EVERY LINE, AND CAPTURE AT LEAST 60 SECONDS.** The cadence is the
finding. Printed bare, a solve every five seconds that nothing asked for reads
as a healthy engine reporting a correct measurement, over and over.

| line | what it says |
|---|---|
| `sampling: 311 lines x 50.08 Hz -> line rate 15625` | **the engine's whole measurement of the source in one line** — the count, the field rate, and the line rate derived from them. `line rate 0` is a reading `rateFollowsCount()` rejected. A first sample of 60..160 Hz followed by a good one is `getSourceFieldRate()` settling, not a fault |
| `source moved: interrupt \| count \| rate (N lines, solved M)` | why a solve was armed. `interrupt` with `N == M` is the count saying nothing moved, which is the case the latch exists for -- the same count at a different field rate. **An arm SPENDS the latch**, so one taken during a change the engine armed for another reason cannot fire again afterwards |
| `own V sync: yes\|no after Nms` | the sync type probe ran. **It writes `SP_EXT_SYNC_SEL`, and the chip latches that as a SOG switch**, which the probe acknowledges itself -- so an `interrupt` arm after one is a disturbance the probe did not cause |
| `no INPUT vsync` / `no OUTPUT vsync` | which FrameSync sample timed out. Do not infer which — it says |
| `input selected: ypbpr, reference divider 2506, reset +18ms, …` | the selection edge, said once the sequence is complete. **It must arrive BEFORE the first `DETECT`** — that ordering is what says the arriving source will be counted through a reference clock rather than the previous source's, and it is the only part of that fault which repeats reliably enough to test. It lands ~250 ms after the request, `/input` being queued for `loop()` |
| `h:%4u … ht:%4d vt:%4d … u:%3x s:%2x S:%2d` | `printInfo()`. `h:` is `HPERIOD_IF`, `ht:`/`vt:` are `STATUS_SYNC_PROC_HTOTAL`/`VTOTAL`, **`u:` is `VideoSourceAcquisition::unmeasuredPasses()` IN HEX**, `s:` is `acquiredPasses()`, `S:` is the SOG level |

**`u:` climbing with `s: 0` is a diagnosis on its own.** One of the two is always
zero: `u:` counts the passes since the engine could last measure the source and
`s:` the unbroken run since it could. `u:` pinned in the nineties is the ladder
cycling its recovery on a source it never acquires, roughly every five seconds,
for ever. `vt:` beside it says whether the sync processor is counting at all,
which separates a dead sync path from a source the engine is refusing.

**`printf()` IN THE SKETCH DOES NOT REACH THE CONSOLE. `debugPrintf()` DOES.**
The console mirror is `SerialM`, and `debugPrintf` is
`SerialM.printf_P(PSTR(fmt), ...)`; a bare `printf` goes to stdout and is
invisible over the websocket. Several existing messages are bare `printf` --
the colour-offset readback among them -- so **a missing line is not evidence the
branch did not run**, and a new diagnostic written with `printf` produces a
silent route that answers 200.

**A quiet console is not a quiet firmware.** Silence with a live HTTP stack
means the loop is not running, or the heap gate is shut — read `/bootlog`'s
`free heap:` line before believing it.

**AND IT IS DEAF FOR THE FIRST ~15 SECONDS OF A BOOT**, which is where
detection, the sync-type probe, the first solve and the rate match all happen.
`/bootlog` holds them, and **everything printed through `SerialM` is already in
it** — `SerialMirror::write()` appends there itself, so no line needs anything
added to survive a boot and a second path alongside it double-writes. The whole
of what is needed is `BOOTLOG_BYTES=2048` on the flash line. It stops recording
once a websocket client takes delivery, so open no console until after the read.
`docs/gbs-control-debug-interface.md`. And the console DROPS BURSTS under
FrameSync spam, so a missing line is not evidence the step did not run: judge by
outcome, and by what the next line implies.

### WHERE A SIGNAL REACHES IS `/testbus`, AND ONLY THE DEVICE CAN TIME IT

`TEST_BUS_SEL` (`s0_4d[4:0]`) picks which block drives `DEBUG_IN_PIN`, and the
transition count over one window separates a field-rate signal from a line-rate
one and both from a dead bus. The pin is an ESP GPIO, so no HTTP read can see
it; `/testbus` queues a sweep for `loop()` and prints CSV to the console.

```sh
curl 'http://<ip>/testbus?ms=25'            # every selector
curl 'http://<ip>/testbus?ms=25&sp=4'       # with a sync-processor stage out
curl 'http://<ip>/testbus?ms=25&if=0'       # with an input-formatter signal out
```

At 50 Hz expect single-digit transitions in 25 ms, at 15.6 kHz several hundred.
`SP_TEST_MODULE` selects one sync-processor stage (4 `vs_act_det`, 6 retiming, 7
out proc) and `IF_TEST_SEL` one input-formatter signal, so **a sweep taken on
each sync type says which stage stops carrying vertical sync.**

### AN HTTP READ IS NOT A SAMPLE. `/samplinglog` IS.

`/getreg` is deferred to `loop()` and answers at tens of hertz at best, so it
cannot tell a value that dithers from one read torn across two states, and it
cannot see a transient at all. `Tv5725::SamplingLog` samples from inside
`loop()` and prints CSV to the console.

```sh
make -C build flash-ota HOST=… GBS_SAMPLING_LOG=1     # off in the default build
curl 'http://<ip>/samplinglog?ms=25&for=30000'        # follow the source
curl 'http://<ip>/samplinglog?low=1600&high=2900&step=100&dwell=400'   # walk the divider
curl 'http://<ip>/samplinglog?rates=120'              # time the field rate, N times
```

**`rates=` IS WHAT ASKS WHETHER A MEASUREMENT IS STEADY WITHOUT THE ENGINE IN
THE WAY.** It times the source's field rate off `DEBUG_IN_PIN` exactly as
`SourceMeasurement` does and prints `rate,ms,sp_vtotal,field_rate_mhz`, and it
runs from `loop()` rather than from the acquisition tick — so it still answers
with automation FROZEN. That is the only way to ask whether a register a solve
writes disturbs what the solve then measures: frozen, nothing rewrites the
register, and a spread belongs to the register rather than to a loop closed
through the engine. 120 readings returning one value at sd 0.000 is what a
steady source looks like.

**`flash-ota` REBUILDS, so the flag has to be on THAT line.** A `GBS_SAMPLING_LOG=1`
passed to a bare `make -C build` does not reach the unit — the `flash-ota` target
rebuilds without it and uploads the default image. **An unregistered route
answers 500, not 404**, which reads as a crash in the handler rather than as a
build missing the flag, and the difference is one word on the upload line.

One line per sample carries the divider, `STATUS_MISC_PLLAD_LOCK`,
`STATUS_SYNC_PROC_VTOTAL`, `STATUS_SYNC_PROC_HTOTAL`, `HPERIOD_IF`,
`VPERIOD_IF`, `HSACT`, the IF status bits and the latched interrupt byte — read
adjacently in one pass, which is what makes two of them comparable to each other.
`SamplingLog::event()` logs a decision as the branch takes it, which no dump
afterwards can show.

**QUEUE IT FROM THE PROCESS THAT HOLDS THE WEBSOCKET.** The log prints to the
console, so a capture has to be listening before it starts -- and queuing from a
shell while a separate capture process starts up races both ways: the request
answers `{"error":"already running"}` against a previous run, or the log finishes
before the socket is attached. Either gives an empty capture, which reads as a
dead route rather than as a missed window. Open the socket, poll the route until
it answers `queued`, then read until `smp,done`.

**A `sol,` line says where the engine SOLVED**, emitted when it moves rather
than per sample: the raster, both scales, both display windows, both output
sync pulses, `IF_HSYNC_RST` and `IF_HBIN_SP`. That is the set two runs are
compared on, and taking it over HTTP is what changes the outcome — a full dump
per return is hundreds of `loop()`-deferred requests through the loop being
measured, and the affordable subset used instead left out `VDS_HS_ST`, which is
a pan.

`STATUS_SYNC_PROC_VTOTAL` on the Wii is the measurement that made the case:
HTTP point reads gave 149 / 160 / 230 / 299 among 310s where the log gave
**310 in 1050 of 1050**, so HTTP over-reported a steady register as
intermittent. **A count of agreeing HTTP samples is not evidence of stability**
— a dozen reads at five-second spacing say nothing about the 5.99 seconds
between each pair. The console also drops bursts, so ask for an interval the
link can carry: `ms=25` over 30 s lands ~1050 lines and reports its own
effective rate.

**AND A READING TAKEN TWO SOURCE EXCURSIONS LATER IS NOT A SECOND INSTRUMENT.**
An input change or a sync-type round trip re-rails the register, so a healthy
HTTP sample before one and a railed on-device sample after it compare two
states, not two instruments. That is how the refuted claim above was arrived at,
twice. Sample both in ONE window, or say which state each belongs to.

## The system has three control domains, and you can only see one

This is the single most expensive thing to not know. An evening was spent
diagnosing "the unit" while able to observe roughly a third of it.

| Domain | Reaches | Visible to you? |
|---|---|---|
| ESP8266 | TV5725 registers, Si5351, STV9426 (0x5D), audio | `/getregs` — **this is all you can see** |
| HC32F460 | `ASW_01`-`ASW_04` analog input routing (pins PB12-PB15), the ADV7280 | **write-only** — commandable over UART, never readable |
| MS9288A | HDMI encoding, EDID, output link | no — on nobody's I²C bus |

- **The HC32F460's firmware source is in this repo**, under
  `GBSC-Pro-Source code/usart_uart_dma - （IapApp）/usb_dev_cdc/source/`. The
  directory name and `main.c`'s header are a stock Xiaohua UART-DMA example;
  `videoprocess.c`, `uart_dma.c` and `flash.c` are the AV module. Version-tagged
  in git history (`V1.3`, `v1.2.3`, `v1.2.2`). These files are ISO-8859, so
  `grep -r … | grep -v Binary` hides them — **use `grep -a`.**
- **The ESP commands the HC32 over its UART**, ESP TX → HC32 `USART4` RX (PB7,
  pin 43), 115200 8N1. 7-byte frame: `41 44 <cmd> <arg> <val|nonce> FE <sum of
  bytes 0-5>`. `'S'` selects input: `0x4n` RGBs, `0x5n` RGsB, `0x6n` VGA,
  `0x70` YPbPr, `0x1n` S-Video, `0x2n` composite; `0xA0`/`0xA1` toggle
  `asw_02`. **NO REPLY REACHES THE ESP, AND THE REASON IS THE BOARD RATHER THAN
  THE FIRMWARE.** `ESP_RXD` is driven only by the CH340T's TXD through R60, and
  the HC32's `USART4` TX (PB6, pin 42) goes to J18 pin 2 and to SW2, never to
  the ESP — so no firmware change creates that path, and the commented-out
  `'I'` INFO handler follows from the missing conductor. Schematic sheet 12.
- **SW2 IS NOT A BOOTLOADER BUTTON.** `Key_Init()` configures PB6 as a
  pulled-up input and `Key_Read()` polls it, so SW2 is an application key
  grounding PB6 through R106 — which also means the application claims the pin
  `USART4` TX would otherwise drive.
- **THE HC32 IS REACHABLE AND ITS FIRMWARE IS UPDATABLE, AND NO ROUTE NEEDS A
  BOARD MODIFICATION.** All three reach a HOST rather than the ESP, so they buy
  visibility and a way to flash the part — never a readback the ESP can act on.
  The part is an `HC32F460JEUA-QFN48TR`, so pin numbers are QFN48.

  | route | where | state |
  |---|---|---|
  | SWD | J19: 1 `DVDDIO_3.3V`, 2 `SWDIO` (PA13, pin 34), 3 `SWDCLK` (PA14, pin 37), 4 GND | the vendor's own MDK project ships `JLinkSettings.ini` and a `debug_init.ini` that loads the hex, so this is the route the board author used |
  | `USART4` | J18, a 6-pin header carrying PB7, PB6, PB5, PB4, PB3, GND | TX is already on pin 2, so a USB-serial adapter is a read channel. `BSP_PRINTF_DEVICE` points at USART3, so `printf` needs re-pointing to come out of it |
  | USB device | USB1, a USB-C receptacle, straight to PA12/`USBFS_DP` and PA11/`USBFS_DM` with CC1/CC2 pulled down 5.1K | wired and UNUSED — `LL_USB_ENABLE` is `DDL_OFF`, no USB source file is in the MDK project, and `main()` never touches it |

  **The USB-C port is the HC32's, not the ESP's.** The ESP's serial is a
  separate micro-USB, J5 → CH340T, on sheet 1, so the two enumerate
  independently and the cable that powers the board is a free channel to the
  HC32. **The MDK project is not an IAP application despite the folder name**:
  the scatter file maps the image at `0x00000000` across the whole `0x40000`
  with no reserved boot region.
- **THE HC32 HAS THREE TWO-WIRE BUSES AND ONLY ONE CARRIES A CHIP**, which is a
  trap because the chips are NOT on the pins named `I2C_*`. Schematic sheet 12:

  | HC32 pins | net | on it |
  |---|---|---|
  | 17 / 16 (PA7/PA6) | `SDA` / `SCL` | the ADV7280 (0x42, ALSB high) and the ADV7391 |
  | 45 / 44 (PB8/PB11) | `I2C_SDA` / `I2C_SCL` | nothing -- out to J16, a 4-pin header, with 2.2K pull-ups. **PB11 is the `MD` boot-mode pin**, so J16 pin 1 is also the ROM-bootloader strap -- which UART the ROM then listens on is unconfirmed, the HC32F460 manual not being in this repo. PB9, pin 46, is unconnected |
  | 15 / 14 (PA5/PA4) | `SCREEN_SDA` / `SCREEN_SCL` | nothing -- out to J17, for an external screen |

- **The OLED menu is on the ESP**, not the HC32. Picking an input there works
  because that handler transmits the frame above.
- **`ADC_INPUT_SEL` is only half the input path.** It selects which TV5725 ADC
  input is used. Whether the HC32F460 has actually *connected* anything to it is
  `ASW_01`-`ASW_04`, which appear in no register dump. Two muxes in series.
  **VGA is the only input that raises `asw_01`** (the schematic's
  `ASW_01 = 0, HS_IN = SOGIN` — it selects the dedicated HSync pin over
  sync-on-green), and the only one raising `asw_01` and `asw_04` together.
- **Both MCUs persist "which input", separately, and neither can read the other
  back.** The ESP keeps the `input` setting in `/preferences.txt`; the HC32 keeps
  `asw_01..04` in its own flash and restores them via `Video_ReadNot2()`. The ESP
  reconciles them at boot: `applySavedInputSource()` (
  after `calibrateAdcOffset()` and `setResetParameters()` so neither overwrites
  it) points `ADC_INPUT_SEL` at the saved input and calls
  `sendSavedInputToAvModule()`, which transmits the frame.
  **The hole left is `SeleInputSource == 0`** — nothing meaningful saved, which
  is also what a short preferences read produces — where the `default:` case
  sends nothing and the two can still come up disagreeing. Picking the input
  repairs that, and saves the preference so the next boot sends it.
  **`/input?src=…` does it without a bench trip** — `rgbs rgsb vga ypbpr sv av`,
  queued for `loop()`, so a 200 means understood rather than selected. A unit
  that comes back from a flash showing no picture, with `STATUS_SYNC_PROC_VTOTAL`
  and `HTOTAL` at 0 and `DAC_RGBS_PWDNZ` 0, is usually this and not a fault worth
  diagnosing. **The bench RiscPC is on `vga`**, which is the only input raising
  `asw_01`; guessing `rgbs` because the source has composite sync wastes a
  detection sweep per guess.
  `applySavedInputSource()` records what it did as the boot log's `INPUT:` line,
  **which the default build does not keep**: `BOOTLOG_BYTES=0`, so rebuild with
  `BOOTLOG_BYTES=2048` before trying to tell "no frame sent" from "frame sent and
  ignored".
- **THE HC32 CAN STOP FOLLOWING INPUT SELECTIONS, AND NOTHING ON THE BOARD SEES
  IT.** `asw_01` left low on VGA puts `HS_IN` on `SOGIN`, so an RGB source with
  separate sync delivers vertical and no horizontal: a correct field rate beside
  a doubled line count and `NO EDGE`. `/sc?~`, `/input`, `/restart`, a sample
  clock restart and a source mode round trip all fail to reach it; **a mains AND
  USB power cycle recovers it at once**. The cheap discriminator is the OTHER
  input -- one `/input` request, and a source that acquires proves the board.
  `docs/known-issues.md`.
- **AV module v1.3 changes only the ADV7280/ADV7391 composite path** (525p vs
  625p encoder config). `uart_dma.c` and `flash.c` are byte-identical to v1.2.3.
  It cannot affect RGB/VGA routing — don't reach for it to fix a VGA fault.
- **A register dump is not the state of the machine.** `/getregs` reads the
  TV5725 and nothing else.
- **The MS9288A cannot be reset, queried or configured** by anything on the
  board. Only removing power clears it. Its I²C slave pins are wired to
  nothing — SDA (29) and SCL (28) carry no-connect flags on schematic sheet
  4/13 — so an NDA register map would still not reach it, and neither MCU's
  firmware addresses it: the HC32 drives the ADV7280 and the ADV7391, and the
  ESP drives the TV5725, Si5351, STV9426, PT2257 and the EEPROM. The
  encoder configures itself from its own MCU and ROM — `MCUSEL` (pin 4) is
  strapped low through R56, and the firmware is on-die mask ROM, not an
  external part — and reads the sink's EDID over its own DDC master.

## "No HDMI" with every register perfect

Seen four times in one evening. The scaler can be locked, preset loaded, DACs
powered, sync output enabled — and the TV still says no signal. Registers cannot
distinguish these:

1. **The output clock is not running.** `PLL648_CONTROL_01 == 0x75` is a
   *sentinel the firmware wrote* meaning "the Si5351 drives the display", not a
   measurement that it does. Diagnostic: watch the console. **Do not infer which
   vsync sample failed — the firmware now says.** `vsyncPeriodAndPhase()` prints
   `no INPUT vsync` or `no OUTPUT vsync`, and `runFrequency()` names its failing
   check. A stream of bare headers with no outcome means a pre-`f5bb2b0` build.
   *"The input is locked, so it must be the output sample"* was inferred twice
   and is wrong: measured on the bench unit with the source locked and the TV
   dark, it is the **input** sample that times out — a measurement-path fault at
   `DEBUG_IN_PIN`, not a video-path one. And because `runFrequency()` returns
   early, the Si5351 never gets adjusted at all.
2. **The encoder is transmitting the PREVIOUS timing.** It samples the analog
   output and does not always notice the timing under it moved, so it carries on
   with the mode it locked to before. **What identifies it is the rate the TV
   reports** — a sink that rejected a mode reports no mode, a sink holding a
   stale one names the *old* rate.

   **Toggle `PAD_SYNC_OUT_ENZ` (s0_49 bit 2) before pulling the rails.** Set it
   to 1, pause, set it back to 0: dropping HSOUT/VSOUT makes the encoder
   re-acquire, and the picture comes back at once with no power cycle. Entering
   RGBHV bypass does the same the long way round. **The firmware already does
   this on every source mode change and every output change**: the arm takes
   the aperture and the pad away before the first register the setup moves,
   and `VideoSourceAcquisition::presentWhenSettled()` gives both back once,
   after the divider has latched, the caller's rate match has landed and the
   sampling phase has been searched, and never inside `MinimumSyncAwayMs` of
   the drop. Nothing else presents the output: a solve that completes while it
   is away leaves presenting to that pass. So `sync pad: away` on the console
   is the arm and `sync pad: driven` is the end of the setup, and a divider,
   rate match or phase line AFTER the driven line is a sequencing fault.
   `docs/investigations/encoder-stale-timing.md`,
   `docs/investigations/a-transition-and-a-settled-toggle-place-the-window-alike.md`.

   A power cycle — mains *and* USB, since USB backfeeds the rails — remains the
   fallback for a state the toggle does not clear.
3. **The TV timed out** and dropped the input.

**"IT IS THE ENCODER" IS THE STANDING WRONG ANSWER ON THE SCALING PATH.** Two
occasions where it genuinely was the encoder bound how far that reaches, and
neither licenses reaching for it again:

- **A raster that is not a standard one confuses it**, and the symptom is
  framing that JUMPS WITH NO REGISTER CHANGE. That is what `Tv5725::OutputMode`
  stating each mode's own standard timings fixed: emitting CEA, or close to it,
  stopped the jumping. So a standard raster is the CURE, and a non-standard one
  is the condition -- not the other way round.
- **In bypass it locks to the actual image**, which turning the source's border
  on is enough to move. **Why bypass and not the scaling path is not known.**

**The second of those is itself the argument that it is not the encoder while
scaling: if it locked to image content there, PAN WOULD NOT WORK.** Panning
moves the picture inside an unchanged raster every day, and the lock does not
follow it.

**Refute it before carrying it**, because it is cheap to: FREEZE automation and
change one output register. If the picture moves as a whole, the placement is
downstream; if a strip is blanked and the rest correlates at zero shift, it is
the board. A row-averaged profile of two photographs settles it in one command.

**A COMPARISON ACROSS TWO ACQUISITIONS CANNOT ANSWER THAT QUESTION**, and it is
what makes the encoder look guilty. Two frames either side of a mode change, an
input change or a sync-type round trip differ by a displacement nobody wrote.
Freeze, take both frames inside ONE acquisition, and the displacement is gone.

**THE DISPLACEMENT IS REAL, BUT "THE WINDOW FOLLOWS OUR BLANKING" IS REFUTED --
DO NOT REACH FOR IT.** What holds is that the shown window is established when
the link ACQUIRES and held until it acquires again, downstream of the board:
across one such lock the STV9426 overlay -- generated on the board and keyed in
at U13 -- moved 124 photo columns with the board frozen and no sync register
touched, which nothing on the board can do.

**What is refuted is that it takes its origin from `VDS_DIS_HB_SP`.** Measured
at 320x256@50 into 1080p, the window sits at our unit 161.6 whether that
register is 143 or 200, across a source mode round trip and across a
`PAD_SYNC_OUT_ENZ` toggle; the toggle moves it about 11 columns between two
landings and onto neither value. **The black at the LEFT is the discriminator**
-- a window that followed would paint our first column at column 0 and show
none, and a reading that only asks where the picture starts is reading our own
aperture. That confound is what cut `OutputMode::TransmittedWindowDelayPx` from
20 to 3 and left 25 of 1920 columns black at the right.
`docs/investigations/the-transmitted-window-is-latched-from-our-blanking.md`,
`docs/investigations/the-shown-window-is-latched-at-lock.md`.

**THE SHOWN WINDOW MOVES WITH SOMETHING THE OUTPUT REGISTERS DO NOT DESCRIBE,
AND WHAT IT IS REMAINS OPEN.** Latching to image content is the obvious reading
and it does not survive the arithmetic, so do not carry it as settled either
way.

Measured on `vga` at 320x256@50 into 1080p with the RASTER, the APERTURE and the
SCALE all held -- `VDS_DIS_HB_SP`/`ST` 161..1831 and `VDS_HSCALE` 421 at every
reading, `VDS_HB_SP` 42, only `IF_HB_SP2`/`ST2` panned ten units. Our blanking at
the far end, read with the picture lifted: **15, 2, 14, 2** across four sync-pad
re-latches at pans of 237, 247, 237, 247. Reproducible to a unit, and twelve
units of shown window on a pan the emitted timing cannot carry.

The same pan on 800x600@60, again every output register identical, moves the
LEFT instead: 2 columns of ours at the default pan and 17 ten units in, and the
lift raises none of the 17.

**What rules out the easy explanation is the scale.** A pan of ten capture units
displaces the CONTENT by ten times the magnification -- 24.3 output units on the
doubled 320x256 at 2.43x, 12.7 on 800x600 at 1.27x -- while the window moved
about ten output units on BOTH. A sink choosing its window from the content it
receives cannot land on a figure that tracks the pan in CAPTURE units, because
nothing downstream of the VDS can see them.

**So the candidates are still open**, and two of them are instruments rather
than the board: the MS9288A, the television, and the USB capture dongle, which
is itself a sink with a latch of its own. Splitting them needs the same two
states read on two sinks at once.

**Bypass is NOT a way to get a picture out of an unknown source.** It passes the
source's own timing to the encoder, so it only works where the DISPLAY can show
that timing -- the bench 800x600 does, and a 15 kHz mode does not. Reaching for
`/sc?k` to recover a source that would not scale gives no signal at all, which
reads as the bypass having failed rather than as the TV refusing 15 kHz.
`/sc?~` leaves bypass and re-detects.

**`VDS_ENABLE == 0` is not evidence of no output.** In RGBHV bypass the video
path does not go through the VDS at all, so an empty segment 2/3 is expected and
the unit is still driving the encoder. Reading it as "nothing is being sent" is a
mistake that has been made and cost a wrong diagnosis — bypass produces a working
800x600 picture. See `docs/rgbhv-bypass-trap.md`.

## Things that will cost you an hour if you don't know them

- **THE TELEVISION IS SET TO FULL AND DOES NOT OVERSCAN, ON EVERY MODE.** A
  photograph shows the whole HDMI active area, so picture missing from one was
  never sent and "the panel is hiding it" is not available as an explanation.
  The measurement section below has what follows from that.
- **THE ADC PLL GROUP CANNOT BE BISECTED BY HAND, AND TRYING COSTS THE LOCK.**
  `PLLAD_MD`, `PLLAD_KS`, `PLLAD_CKOS`, `PLLAD_ICP`, `PLLAD_FS` and the two
  decimators are ONE setting: `PLLAD_LAT` loads several of them on a rising edge
  and the loop filter has to suit the tap. Writing two or three of them over HTTP
  to compare configurations leaves the PLL unlocked -- measured twice in one
  session, `STATUS_SYNC_PROC_HTOTAL` wandering a few counts under the divider in
  states that reported it back exactly before the poking started. The wandering
  count is the evidence; the lock bit beside it is not, for the reason below.
  **`/sampleclock` IS THE INSTRUMENT**, behind `GBS_DEBUG` and pass-through only:
  it writes the whole group through the call the bypass switch makes, moves the
  channel's played-out raster with the divider, and restarts the PLL afterwards
  — which is itself required, because applying the group alone leaves it
  unlocked even at the value it already held.

  ```sh
  curl 'http://<ip>/sampleclock'                 # report, change nothing
  curl 'http://<ip>/sampleclock?md=2039&os=2'    # divider and oversampling
  ```

  It answers on the console, not in the reply. `/sc?~` recovers.
- **`/sc?~` IS THE RECOVERY FOR A UNIT THAT COMES BACK FROM A FLASH WITH NO
  PICTURE, and reflashing is not.** The signature is `SP_SOG_MODE` 1 on a
  separate-sync source with `SP_VTOTAL` 0 or 97, `ADC_SOGCTRL` walked to 1 or 2,
  `DAC_RGBS_PWDNZ` 0, `/geometry` all zeroes, and the console printing
  `own V sync: yes` every ~7 s while nothing changes. The chip keeps its
  registers across an ESP reset, so detection restarts against a sync path left
  from before and settles on csync. `/sc?~` runs
  `goLowPowerWithInputDetection()`, which forgets the sync type, and the source
  is back in under a minute -- measured four times. **Flashing a different image
  appears to fix it and is only the reset**, which costs four minutes and reads
  as evidence about the build.
  **THE SAME SIGNATURE REACHED BY AN INPUT CHANGE DOES NOT ANSWER TO IT.** An
  `ypbpr` to `vga` change leaves the separator in circuit on the VGA input --
  `SP_SOG_MODE` 1 and `ADC_SOGEN` 1 -- so hsync is sought on green, where the
  RiscPC puts none, and `STATUS_SYNC_PROC_HSACT` reads 0 beside
  `own V sync: no after 1000ms` on a separate-sync source. `/sc?~` failed to
  clear that twice where a `/restart` cleared it at once, in about twelve
  seconds. So the TRIGGER decides the recovery and the registers do not say
  which trigger it was. `docs/known-issues.md`.
- **Power-cycle the SOURCE too, before spending a session on the scaler.** The
  intermittent shear glitch — a few scanlines high, content compressed and
  diagonally sheared, ~3% of frames — cost several sessions on this board and
  was closed on 2026-08-15 by powering down the RiscPC as well as the unit. It
  has not returned. Every scaler-side hypothesis was refuted in turn (sync
  processor miscount, boot state, accumulated state, `MEM_FF_STATUS`, the four
  separate-sync registers), and the one thing never varied was the source.
  The source had been ruled out the day before and should not have been. **Do
  not re-open this without new evidence**, and if a comparable artefact appears,
  cold-boot both ends first — it is one minute against an evening.
  Paired artefacts are in the archive: `CLEAN-*-2026-08-15` and
  `glitching-2026-08-14`.
- **THE LINE DOUBLER FOLLOWS THE MEASURED COUNT, AND MUST STAY THAT WAY.** A
  preset load used to turn a 15 kHz RGBHV source progressive -- `IF_PRGRSV_CNTRL`
  0 -> 1, `IF_LD_RAM_BYPS` 0 -> 1, `IF_HS_DEC_FACTOR` 1 -> 0, `PLLAD_MD`
  2250 -> 1124, `SP_VTOTAL` collapsing from 311 to noise -- because one number
  carried two facts: the standard byte was forced to 3 for a scaling RGBHV
  source, and 3 also meant progressive SD. The divider that followed was
  arithmetically right for the wrong premise, so every register read
  self-consistent. `SourceMeasurement::lineDoublingFor()` answers off the count
  now and nothing classifies, but **anything that re-introduces a value carrying
  both the source's scan and the output chosen for it brings this back**, and a
  register dump cannot see it.
- **DO NOT PURSUE `HPERIOD_IF`'S UNRELIABILITY. IT IS ACCEPTED AS UNRECOVERABLY
  BROKEN AND NOTHING RELIES ON IT.** The engine reads it in exactly one role --
  a change detector, asking whether the reading MOVED. That comparison is
  against its own earlier value, so a bias cancels and a rail compares equal to
  itself, which is why the role is safe where every other use is not.
  `SourceMeasurement.cpp` states this at the head of the file and
  `SourceMeasurement::measureLineRate()` derives the line rate from the count
  and the field rate instead.

  **So asking what it IS is not a diagnostic, and checking it against the value
  a mode should give is how a session gets spent.** It reads wrong on separate
  sync as a matter of course, it reads wrong in bypass, and it reads a stable
  wrong value often enough that every stability check passes it. A wrong
  reading is expected and is evidence of nothing.

  **The recoveries were exhausted and the question is closed**: every
  `SFTRST_*_RSTZ`, the analog bias resets, every clock reset in BOTH domains,
  `PLLAD_VCORST`, `PLLAD_PDZ`, `ADC_POWDZ`, `PLL_VCORST`, `SDRAM_RESET_SIGNAL`,
  `PLL_LEN`, `MEM_CLK_DLY_REG`, a divider sweep over 1000..2900, a cold boot,
  and an `ADC_INPUT_SEL` bounce that clears it sometimes and CAUSES it other
  times. The measurements are in `docs/investigations/hperiod-if-railing.md` and
  they stay there.

  **What it can still do is strand the HELD rate.** A rate once accepted becomes
  the held good one and `rateFollowsCount()` rejects correct readings against it,
  so the engine can sit at `state: absent` with the sync processor counting the
  source perfectly beside it. `HeldRateRejectionLimit` is what lets it out. That
  is a fault in the held rate, not a reason to go back to the register.
- **A DIVIDER STUCK ON ANOTHER MODE'S VALUE IS A DIFFERENT FAULT, and `/sc?~` is
  what clears it.** A mode-change round trip does not: a stuck `PLLAD_MD`
  survived 311 -> 524 -> 311 unchanged at 1822 with the picture rolling, and
  `/sc?~` restored 2250 at once. **Judge the divider against the source**, not
  against whether it moved.
- **Check the settings before diagnosing anything.** One evening produced three
  separate investigations with a single cause -- a short read of the preferences
  yielding a full set of defaults: the custom preset "not loading", FrameSync
  "broken" (`frame-time-lock` 0 means it never ran), and the input not applying.
  `/preferences.txt` is **keyed text** now, so it is read rather than decoded --
  `read_settings(host)` for a dict, `setting(host, "input")` for one value -- and
  a missing key costs its own setting only. **A file that does not reach its `end`
  line is one the boot refused**, and a refused boot refuses every save
  afterwards, "restore defaults" included. `docs/preferences-file.md`.
- **USB backfeeds power.** A "power cycle" with the USB cable attached does not
  drop the rails, so the MS9288A and HC32F460 never reset. Pull mains *and* USB,
  and wait. Several apparent power-cycle results were nothing of the kind.
- **Cold boot and warm reset are different tests.** The preferences bug is a
  power-up race on the SPI flash — `LittleFS.begin()` returning true does not mean
  reads work yet. Reflashing tests nothing; only a true cold start does.
- **A connected-but-silent console means the heap gate is shut, not that the
  firmware is quiet.** `SerialMirror` only calls `broadcastTXT` when free heap
  clears `CONSOLE_BROADCAST_MIN_HEAP`. That was **20000**, which this fork can
  never reach: globals take 50868 bytes of 81920, and once WiFi, the async server
  and the websocket server have taken theirs, free heap is in the teens of KB.
  The socket then accepts clients and delivers nothing for the whole
  session — the web UI loads its shell and sits on the splash with the red
  disconnected indicator. Lowered to 8000 on 2026-08-05. **Read `/bootlog`'s
  `free heap:` line before believing a silent console.** Guarded by
  `test_the_console_delivers_anything_at_all`.
- **FREE HEAP IS NOT A CONSTANT OF THE BUILD, SO ONE READING PROVES NOTHING
  ABOUT A CHANGE.** Measured on an unchanged unit, 13200..17192 purely by what
  is attached — about 1.4 KB per websocket client against a cap of 5 — so a
  reading taken with a console open and one taken idle differ by more than most
  changes cost. Compare the build's `Global variables use` line, which is
  exact, and keep the clients constant if a live figure is the one being
  compared. `docs/known-issues.md`.
- **It is not a one-client limit, and the console does not hang up any more.**
  `WEBSOCKETS_SERVER_CLIENT_MAX` is **5**
  (`3rdparty/WebSockets/src/WebSocketsServer.h:31`), nothing crashes, and six
  clients have been attached at once. The one-client story came from the old
  `else webSocket.disconnect()` — the no-argument form drops *all* clients, so a
  single heap dip during one console write killed every session at once. Removed
  in `d3a4426`. **Leave the panels open.** Nothing has ever gone wrong from a
  regpanel or a web UI being left running, and the cap is five.
- **HTTP answering does not mean the firmware is running.** The web server is
  `ESPAsyncWebServer`, which serves from network-stack
  callbacks, **not** from `loop()`. So `/getreg`, `/setreg`, `/freeze` and the
  200 from `/sc?c` keep working while `loop()` is stalled — and `webSocket.loop()`,
  the OSD, the IR handler and FrameSync's steering of the Si5351 all stop. The
  signature is exactly that split: HTTP fine, websocket connects but delivers
  nothing, OSD dead, picture dead. **Do not read "HTTP responds" as "the unit is
  healthy".** The serial console is the honest test — silence there with a live
  HTTP stack means the loop is not running.
- **But `/getreg` timing out while `/freeze` answers instantly is a *useful*
  signal, not a dead unit.** Register access is deferred to `loop()` through
  `RegisterQueue`, so a read blocks for as long as `loop()` is busy, while the
  plain-JSON routes keep answering from the network callback. Intermittent empty
  `/getreg` replies with ping at 2 ms therefore mean the firmware is *inside*
  one of detection's long searches — the two 6000 ms line-count waits in
  `detectAndSwitchToActiveInput()`. Used exactly that way on 2026-08-13 to tell
  "unit wedged" from "unit hunting", which are opposite diagnoses.
- **What is attached is worth KNOWING, not closing.**
  `ss -tanp | grep <ip>` says what is actually connected, and
  `ps -eo pid,etime,cmd | grep -E 'soak_watch|regpanel|sweeplog'` says what
  started it. That is context for reading a result, not a hazard to clear: a
  panel left running is not a fault and closing one has never fixed anything.
  A **polling** loop is the thing to look at, because it changes what the unit
  is doing between your reads.
- **Never flash `GBS_DEBUG=0` while diagnosing.** It is the flag gating
  `debugPrintf`, which `tv5725Log()` expands to, so it silences `no INPUT vsync`,
  `no OUTPUT vsync` and every `runFrequency()` reason — precisely the messages
  the "no HDMI with every register perfect" section tells you to read. It buys
  about a kilobyte of globals and costs the diagnosis. Use
  `GBS_DEBUG=1 GBS_TRACE_WRITES=0`: full diagnostics, none of the
  per-write trace flood.
- **The filesystem is LittleFS, and the routes are `/fs/*`.** Migrated from
  SPIFFS on 2026-08-05, because SPIFFS updates directory metadata in place, is
  not power-loss safe and has no fsck — and this unit is hard-power-cycled
  mid-write routinely. It had left `/slots.bin` listed twice with one entry
  serving 0 bytes and hanging, which stalled the web UI on its splash for two
  days. Endpoints were renamed `/spiffs/*` → `/fs/*` at the same time; the Python
  helpers are `fs_dir()` and `fs_read()`. Two LittleFS behaviours differ and are
  handled in `/fs/dir`: `lfs_dir_read()` emits synthetic `.` and `..` entries,
  and `fileName()` returns the bare name where SPIFFS returned the full path.
  **The on-flash format differs**, so a first boot after the switch auto-formats
  and the old contents are gone — `wipe=none` preserves the region, not the
  meaning of what is in it. Free heap went *up* (~20.5 K → ~22 K): SPIFFS's
  mount-time page buffers were larger.
- **Filesystem access blocks the firmware loop.** `/fs/dir` calls `delay(1)` in a
  loop. Hammering it can make the sync watcher see instability. Read-only over
  HTTP is not the same as zero-impact.
- **Opening the serial port resets the board** (DTR/RTS). `stty -F /dev/ttyUSB0
  115200 -hupcl raw -echo` first. The boot ROM prints at 74880 baud.
- **`make -p | grep VAR`** to check what Make really assigned. A bare `#` starts a
  comment mid-assignment, which silently dropped a library commit pin and cost a
  build failure that looked like a library incompatibility.
- **A renamed or deleted firmware source keeps compiling from the build cache.**
  arduino-cli's `--build-path` copies the sketch into `build/output/sketch/` and
  copies changed files in, but never removes one that vanished from the source.
  The stale copy is still compiled, and **the error names the SOURCE path**,
  where the file no longer exists -- so it reads as a ghost include or an editor
  writing a deleted file back. `make -C build clean` after any rename, or list
  what the cache has that the tree does not:

  ```sh
  comm -23 <(ls build/output/sketch/src/tv5725/*.cpp | sed 's|.*/||' | sort) \
           <(ls "GBSC-Pro-Source code/gbs-control/src/tv5725/"*.cpp | sed 's|.*/||' | sort)
  ```
- **ALWAYS ENTER THE DEV SHELL FROM THE REPO ROOT.** `NIX_LDFLAGS` carries
  `-rpath $out/lib`, and `$out` is derived from the directory `nix develop` was
  invoked in. Enter it from inside `GBSC-Pro-Source code/` and `$out` contains
  the space, so the link line word-splits and g++ is handed a nonexistent
  `-L code/gbs-control/outputs/out/lib`:

  ```
  ld.bfd: cannot find code/gbs-control/outputs/out/lib: No such file or directory
  ```

  **It reads as a source that vanished**, which is the trap: it looks exactly
  like the stale-build-cache entry above, so a rename in flight gets the blame
  and the next move is a pointless `make -C build clean`. Nothing is wrong with
  the tree. `cd` back to the root and the same command passes. A `cd` in an
  earlier compound command is enough to cause it, because the shell's working
  directory persists between calls.
- **Nix copies the working tree into the store to evaluate the flake from a
  dirty git checkout, and it copies TRACKED FILES ONLY.** One copy per distinct
  tree state, so an editing session is several GB — this is what took `/nix/store`
  to 233 GB and the root filesystem to 100% on 2026-08-13. Untracking a large
  path is what stops it; `.gitignore` is hygiene. The opposite was written down
  twice ("untracked-and-unignored is still copied") and is **refuted**: a 5 MB
  untracked, unignored file at the repo root, with a tracked file touched to
  force a fresh evaluation so it could not be a cache hit, produced a new store
  path that did not contain it. Untracking the photographs took the copy from
  211 MB to 65 MB. Weekly GC and `auto-optimise-store` are configured in
  `~/Projects/nix-config`; **stop invoking `nix develop -c` once per command.**
- **LFS is not available in this repo, and retrying will not change that.** It
  is a public fork of `RetroScaler/gbsc-pro`; LFS objects on a fork bill to the
  parent, so GitHub rejects the push outright — *"can not upload new objects to
  public fork"*. And `git lfs migrate` rewrites from the **root commit** whatever
  you scope it to, which moves upstream's SHAs and the six vendor tags. Large
  binaries go in a separate repo; see the photo repo in the table above.
- **Flashing preserves the filesystem** (`wipe=none` in the FQBN), so stored timings and
  preferences survive.
- **The vsync-sampling hang is fixed — do not diagnose with it.**
  The sampler used to spin 3,000,000 passes with `ESP.wdtDisable()`,
  exiting only on a vsync pulse, so a `PLLAD_MD` write that broke sync killed
  serial, ping and HTTP while the picture kept running. `38df4e5` (2026-08-03)
  bounded the wait in *time* (`FS_SAMPLE_TIMEOUT_MS`) and left the watchdog
  running and fed. It is in every build since.
  **The old rule of thumb inverted with it**, and the stale note cost a wrong
  diagnosis on 2026-08-05 within minutes of a session starting: *station present
  with low inactive time but no ping* no longer means wedged. It now means the
  fault is below the firmware loop — WiFi association without a working IP path.
  A ~2 minute unexplained dropout that recovered unaided was seen the same day.
  The check itself is still the right non-invasive first move, only read the
  other way:
  `ssh router "iw dev phy1-ap0 station dump | grep -A6 fc:f5:c4:b1:f2:38"`, and
  ping *from the router* to rule out your own host's path before concluding
  anything about the unit.

## The datasheet contradicts itself, and the header is the survivor

**Rediscovered three times now, so it is written down.** RD-5725-1.1 gives a wide
field's bit slices in three places, and they disagree:

1. the **bit diagram** above each register's table
2. the **Bit/Name table rows**
3. the `bit[hi:lo]` written inside the **Function description text**

`VDS_HB_ST` is the canonical case — diagram `[7:0]`/`[11:8]` (right), table rows
`[9:8]`/`[3:0]` (wrong) — and reading the table makes it 10 bits at `s3_05`
instead of 12 at `s3_04`. It holds 1342 on this bench, which does not fit in 10
bits, so the header is right and the table is simply an error.

- **Cross-check all three; do not trust one.** `c922c80` (2026-08-04) cut the open
  disagreements from 12 to 2 that way. **The diagram is not automatically the
  authority either** — `SP_H_CST_SP` has the wrong slice in its diagram and the
  right one in its table.
- **One name per field — there are no intended aliases.** Settled 2026-08-13:
  prefer the datasheet's name unless the firmware's has a tangible benefit, and
  the benefit that counts is *granularity* — `SDRAM_RESET_SIGNAL` names one bit
  the datasheet only has inside `MEM_INI_REG[7:0]`, so it stays. A better
  wording counts too (`INT_CONTROL_RST_SOGBAD` over the datasheet's bare
  `INT_RST_0`), and a name commented "fake name" does not. **An alias is not
  untidiness, it is a second owner**: the bring-up block wrote `INT_RST_0` while
  the sketch wrote `INT_CONTROL_RST_SOGBAD`, four bits had two owners, and every
  check passed because checks compare names. **Ask in bits, not in names** —
  nothing guards this automatically since the ownership tooling was deleted.
- **The whole-byte convenience names are a live campaign, not settled.**
  `PLL648_CONTROL_01::write(0x75)` sets five documented fields under a name the
  datasheet lacks. A literal byte write is the target; a save/restore pair
  through one is legitimate and stays. **Where a byte has bits no datasheet name
  covers, decomposing it is not equivalent** — the byte write zeroes those bits
  and field writes leave them. The inventory, the counts and the order are
  `docs/whole-byte-convenience-names.md`; do not restate a count here, it rots.
- **Where the header and the datasheet disagree, the header wins.** Its values
  are that audit's output plus bench proof, and eleven such fields remain.
  **Nothing checks this any more** — the audit was deleted on 2026-08-17 once it
  had finished finding things, so a new disagreement now arrives silently.
  `tools/gbsc-pro-hwtest/datasheet_fields.json` is the extraction it was checked
  against, and git has the tooling.
- **Assert known widths before believing any extraction.** `VDS_HSCALE` 10,
  `PLLAD_MD` 12, `IF_HB_ST1` 11, `MEM_MODE_REG` 16. Two attempts at re-deriving
  the field set on 2026-08-13 both produced complete-looking tables that failed
  exactly there — one keyed the parse by name so later slices of a wide field
  overwrote earlier ones, one read `(hi, lo)` as `(lo, hi)`. Both looked fine.
- **The danger is one-directional.** A field declared *narrower* than it is
  truncates every value written through it and says nothing.
- **A READ-BACK IS NOT PROOF THE HARDWARE ACTED ON THE WRITE.** Reserved bits
  beside a field are often writable storage with nothing behind them, so
  widening a field on a successful round trip proves only that the bit exists.
  `HD_HSYNC_RST` is the case: `s1_38` bit 3 stores a 1 and reads it back, and
  the channel's counter ignores it — proven by the pair either side of 2048,
  where a line of 2040 counts displays and one of 2056 gives no signal.
  **Ask the behaviour, not the register.**
  `docs/investigations/the-bypass-divider-is-capped-by-the-channel-counter.md`
- **The PDF's line wrapping is what breaks any re-derivation, in two
  directions.** A long field name wraps across two lines, and a parser that
  keeps only the second gets a fragment — `ALUE`, `EG0`, `R_B` — which reaches
  the header looking like a real register. Every genuine field name contains an
  underscore, so a name without one is a fragment. The worse direction is the
  quiet one: the wrap can put the whole name on the line above and leave the bit
  row holding only `[7:0]`, and the field then arrives as nothing at all. Seven
  went that way, every wide multi-byte address field
  (`WFF_SAFE_GUARD_A`/`_B`, `CAP_SAFE_GAURD_A`, `RFF_WFF_STA_ADDR_A`/`_B`,
  `VDS_NS_SQUARE_RAD`). **A completeness check that compares a header against an
  extraction cannot see this**, because both have the same hole — count against
  the PDF's own table count instead.
- **The datasheet has typos in field NAMES, not just in bit slices.**
  `CAP_SAFE_GAURD_A` ("GAURD") and `OSD_YCBCR_RGB_FORMATE` ("FORMATE"). The
  header carries the corrected spelling at the same address — so searching
  RD-5725-1.1 for the header's name will fail on exactly those two.
- **A "gap" in the register map may be a documented register the tooling lost.**
  s4_47..49 was carried for a session as an unnamed hole with a proposed bench
  experiment. It is `WFF_SAFE_GUARD_B`, documented in full, with its own table.
  Search RD-5725-1.1 itself before designing an experiment.
- **A bit the preset writes is not automatically a field.** Of the four
  addresses whose unnamed bits some preset table sets, three — `s3_14[3]`,
  `s3_71[3]`, `s4_5b[6:0]` — are marked **RESERVED** in the datasheet's own
  tables. The preset writes 1s into reserved bits. Only `s5_5d` is genuinely
  absent from RD-5725-1.1, appearing zero times in it.

## What a preset load actually writes

**The scaling preset tables are gone, and `writeProgramArrayNew()` with them,
and so is the last blob — `rgbhv.h` was unreferenced and is deleted.** Bypass is
applied by `bypassModeSwitch_RGBHV()` writing registers. So a preset load is now
`Tv5725::BringUp::init()` — the static registers, one call per subsystem — plus
whatever the geometry engine solves, and nothing else.

Two consequences that have each cost time. **A small register diff across a
preset load is expected, not evidence that no preset loaded**: `/sc?~` moves
about thirty registers on a settled source, and reading that as "the preset path
did not run" is a wrong inference drawn from the old 432-register figure.
And **a preset can no longer clobber a field**, because there is no bulk byte
range to overrun one — the mechanism behind `docs/preset-load-clobber.md` is
closed by construction, though its individual findings still describe registers
nothing owns.

`docs/chip-initialisation.md` is the design and why the tables went.
`docs/investigations/preset-abandonment-audit.md` has what was measured from the
twelve tables while they existed, which is what `BringUp` was built from.

## Register facts that are not obvious

- **THE ENGINE WORKS IN THE SOURCE'S FRAME, NOT IN WHAT `STATUS_SYNC_PROC_VTOTAL`
  READS.** That register counts from zero, so the bench RISC PC reads 311 and
  the Wii at 480p reads 524 where the frames are 312 and 525.
  `SyncProcessor::frameLines()` undoes it at the boundary and nothing
  downstream corrects again -- so the console says `measured: 312 lines`, a
  source key is `312@50.08/695??`, and `/framing.txt` stores the figure a mode
  file states. **A `+ 1` added to a count anywhere else is a second copy of a
  correction already made**, and there were eight of those before it moved.
  `docs/video-source-acquisition.md` has which other registers are zero based
  and where each is undone; `SamplingLog` and the sketch's diagnostics
  deliberately still print the register.
- **`STATUS_SYNC_PROC_HTOTAL` echoes `PLLAD_MD`.** The sync processor counts in
  ADC clocks and the ADC PLL is locked to HSync with `PLLAD_MD` as divider, so it
  reports your own setting back. Never key anything on it — **except as the one
  witness that the divider was LATCHED**, which is the next entry.
- **Reading `PLLAD_MD` back does not tell you what the ADC is doing.** It is
  loaded into the PLL by a *rising edge* on `PLLAD_LAT`, so between the write and
  that edge the register reports the new value while the chip still clocks at the
  old one. Anything that reads it back to derive something else is asking a
  question the register cannot answer. That is not theoretical: on 2026-08-15 a
  divider written **after** `latchPLLAD()` left `PLLAD_MD` reading 2210, the PLL
  running 2553 and `IF_HSYNC_RST` sized for 2210 — solid green screen, every
  register self-consistent, nothing to diagnose from. **Whatever writes
  `PLLAD_MD` must run before the latch, not after it.**

  **A solid green screen has a SECOND cause, and it is not the divider.**
  `PLLAD_CKOS` selects which tap of the ADC clock feeds the pipeline, and the
  decimators (`ADC_CLK_ICLK1X`, `ADC_CLK_ICLK2X`, `DEC1_BYPS`, `DEC2_BYPS`) undo
  in the digital domain what that tap added — so the two have to describe the
  same oversampling ratio. `Adc::applySampleRate()` writes all five together for
  that reason. Set `PLLAD_CKOS` to 2 against the decimators the firmware runs at
  oversample 4 and the screen is green and stays green: measured at +2 s and
  still green at +20 s, cleared by putting it back. Throughout it,
  `PLLAD_MD`, `IF_HSYNC_RST`, `SP_RT_HS_SP`, `HPERIOD_IF`, `SP_VTOTAL`,
  `STATUS_MISC_PLLAD_LOCK` and `STATUS_IF_HT_BAD` all read exactly what a healthy
  unit reads. **There is nothing in a register dump that distinguishes it**, and a
  detection pass is what repairs it, because that is what rewrites the group.

  Two traps come with it. The picture is the only instrument, so a still that
  misses it and a still that catches it look like the write not working — record
  a clip. And `HPERIOD_IF` stays perfectly healthy while the screen is green, so
  this is not the railing and the two must not be merged.

  `PLLAD_MD`, `IF_HSYNC_RST` (= `MD`/2) and `SP_RT_HS_SP`, the sync processor's
  retime window, are **ONE quantity in THREE registers**, and
  `Tv5725::SourceMeasurement` owns all three off one held value. It is *state*,
  handed to `Tv5725::CaptureWindow` rather than read back — the same rule
  `CaptureWindow::ProgressiveStart` already carried.

  **`SP_RT_HS_SP` IS THE CAPTURE COUNTER'S ORIGIN, NOT A FRACTION OF THE
  DIVIDER.** It lays the retimed pulse on the incoming one --
  `PLLAD_MD - STATUS_SYNC_PROC_HLOW_LEN + 63` -- so it follows the source's own
  sync width. `0.93 x MD` is `1 - duty` for a source whose sync takes 7% of the
  line and is wrong for every other duty, placing the whole capture window with
  nothing in a register dump to show it. **A narrow pulse is where this bites**:
  `retimeStopFor()` returns 0 when the measured sync is under the 63-sample
  origin, and `writeRetimeStop()` then writes nothing at all, so the counter
  keeps the origin it had.
  `docs/investigations/the-retime-stop-is-the-counters-origin.md`.

  **AND THE ORIGIN CARRIES THE SOURCE'S VIDEO LAG IN THE SOURCE'S OWN PIXELS**,
  which is why one constant framed some modes and not others. The lag is about
  4.4 source pixels, so it costs 1 ADC sample on a mode sampling 1.36 samples a
  pixel and 14 on the bench's 320x256@50 at 4.30 -- `SourceTiming::pixelSamples()`
  is what makes it computable, the raster stating the line's pixel count, and a
  source matching no raster gets the constant as measured. **Two bench modes at
  ONE divider is what names the quantity**: 800x600@60 samples 1436 across 1056
  source pixels and 320x480@60 samples 1444 across 400, and the two want 2
  samples of origin and 9.
  `docs/investigations/the-capture-origin-varies-by-mode-at-one-line-rate.md`.

  **`IF_HSYNC_RST` is `PLLAD_MD` HALVED ONLY WHERE THE LINE IS DOUBLED**, which is
  what `SourceMeasurement::ifLineFor()` does — an IF unit is two ADC samples on a
  doubled line and one on an undoubled one. All twelve shipped tables were
  line-doubled SD, so `== PLLAD_MD/2` held across every one of them and reads as a
  universal law; measured on an 800x600@60 RGBHV source it is `== PLLAD_MD`, 1124
  against 1124, and that is correct. `SP_RT_HS_SP` is wrong in five of the tables
  (`ntsc_1280x720` ships **68** against 2180) and is only saved by the runtime
  write.

  **`STATUS_SYNC_PROC_HTOTAL` is the only thing on the board that can see a
  missing latch**, because it counts real ADC clocks per line and so reports the
  *latched* divider. Locked, it equals `PLLAD_MD` — 2553 against 2553, with an
  occasional ±1. **Unlocked it is noise that looks like a small latch error**: it
  held a steady 2558 against 2553 across 22 samples while `SP_VTOTAL` sat at
  97/98. Steady is not valid; ask whether `SP_VTOTAL` is counting first. And it
  reads wrong for a third reason too — an *unconfigured sync processor*. With
  `SP_PRE_COAST`/`SP_POST_COAST`/`SP_DLT_REG` zeroed it read 2400 and did not
  move when 2553 was written and latched by hand.
- **`STATUS_MISC_PLLAD_LOCK` IS A DUTY CYCLE, NOT A STATE, AND ONE READ OF IT
  SAYS NOTHING.** It flickers several times a second on a picture judged
  perfect: measured with `Tv5725::SamplingLog` at 25 ms, a scaling state reads 1
  in **86.3% of 1131 samples with 261 transitions**, 8.7 a second. So a single
  HTTP read has better than a one in eight chance of returning 0 while
  everything is right, and **"the ADC PLL is unlocked" is not a conclusion a
  register dump can reach**. Ask the percentage and the transition rate, which
  need the sampling log -- an HTTP read answers at tens of hertz at best and
  cannot see a bit moving at 8.7 Hz.

  **The honest health metric is `STATUS_SYNC_PROC_HTOTAL` against the divider**,
  because the sync processor counts in ADC clocks and so reports a locked PLL's
  divider back. That is a comparison, it is stable where the bit is not -- 0 or
  ±1 on every healthy state measured -- and a genuinely sick state is hundreds
  out rather than one. `docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md`.

- **`STATUS_IF_VT_OK` READING 0 IS THE NORMAL STATE ON SEPARATE SYNC, NOT A
  FAULT.** The block does not complete a vertical measurement there and
  `VPERIOD_IF` holds debris rather than a period, so the bench RISC PC on `vga`
  reads 0 whenever it is healthy. Reading it as "the input formatter is dead" is
  a diagnosis of the sync arrangement, not of the unit. `STATUS_IF_HT_OK` is no
  better in the other direction: it reads 1 while `HPERIOD_IF` rails.

  **IT GATES ACQUISITION TOO, AND WHAT COVERS THAT IS THE LINE PERIOD RATHER
  THAN THE BIT.** `InputFormatter::verticalPeriod()` returns 0 while the bit is
  0, and that reading is the SECOND INSTRUMENT the contradicted-count guard
  uses to tell a sync processor that stopped counting from a source standing
  still -- so where the bit reads 0 that guard is blind, and the engine held a
  dead count at `state: acquired` indefinitely. `rateMoved()` is what notices
  now: a line period that MOVED arms on its own where no field rate can be
  measured at all, because a reading that is missing cannot refuse one that
  moved. **So the bit still explains nothing and is still not a verdict** -- it
  no longer decides whether the engine can act.

  **THE QUIET PIN IS THE DISCRIMINATOR, AND IT IS NOT THE SAME THING AS THE
  BIT.** `TestBus::selectInputVsync()` drives the formatter's vertical SIGNAL,
  where `STATUS_IF_VT_OK` gates only its MEASUREMENT REGISTER -- so a healthy
  separate-sync source reads the bit 0 and still times a field rate, which is
  why the arm does not fire there. `vsyncEdges(): no INPUT vsync` on the console
  is the state that reaches it. `docs/known-issues.md`,
  `docs/investigations/the-formatters-frame-is-gated-off-when-the-count-stops.md`.

  **The pattern is worth naming, because three of these have now cost time.**
  `STATUS_MISC_PLLAD_LOCK`, `STATUS_SYNC_PROC_VSACT` and `STATUS_IF_VT_OK` all
  read like verdicts and are none of them a verdict: one is a duty cycle, one
  reports the sync path already chosen, and one reports a measurement this sync
  type never completes. **A status bit is a measurement of a block, not a
  judgement of the unit** -- ask what the bit is counting before quoting it as
  evidence, and prefer a comparison that has a right answer, such as
  `STATUS_SYNC_PROC_HTOTAL` against the divider.

- **A WIDE STATUS FIELD TEARS ACROSS ITS TWO BYTES, AND THE TORN VALUE LOOKS
  LIKE A FAULT.** `rawRead()` takes both bytes in one I2C transaction, which is
  not atomic at the chip: the second byte is clocked out tens of microseconds
  after the first, and a per-line register updates inside that gap. **The
  signature is a 2x2 crossing of two genuine values** --
  `STATUS_SYNC_PROC_HLOW_LEN` on composite reads 153 (`0x099`) and 2661
  (`0xA65`), and the crossings 101 (`0x065`) and 2713 (`0xA99`) appear 57 times
  in 27207 samples with nothing else between them. **Decompose an outlier
  before believing it**, and treat it as real only where its high and low bytes
  are not both another observed value's -- `STATUS_SYNC_PROC_VTOTAL`'s 337
  (`0x151`) is genuine on exactly that test. `sampling_stats.py` flags the
  crossings itself.

  **AND A FIELD THAT NEVER MOVES CANNOT SHOW THE ARTEFACT**, so a comparison of
  two states is unsound in one direction: the same source on separate sync
  reads one high nibble and two adjacent low bytes, where tearing is invisible
  by construction, so its narrower spread is not evidence that the measurement
  is steadier. **A single-bit field is immune** and carries across such a
  comparison where a wide one does not.
  `docs/investigations/a-wide-status-field-tears-across-its-two-bytes.md`.

- **`STATUS_SYNC_PROC_VSACT` is not a lock indicator, and it is not dead
  either — it reports the sync path you are already on.** Both readings are
  measured, on the same source, a day apart:

  | | `VSACT` | `SP_VTOTAL` | picture |
  |---|---|---|---|
  | csync path (`SP_SOG_MODE` 1, coast 7/3) 08-14 | **0 in 2375/2375** | 308, 7.34% off-mode | glitching |
  | separate-sync path (`SP_SOG_MODE` 0, coast 0/0) 08-15 | **1 in 150/150** | 311, 0.00% off-mode | clean |

  So `V=no` is the normal locked state *in the csync configuration* — the
  earlier "dead on this board" reading was that configuration seen alone, and
  clearing `SP_EXT_SYNC_SEL` on its own does not revive the bit. **Never judge
  lock on it**; ask whether `SP_VTOTAL` is counting. That mistake cost an hour
  recovering a unit that was fine, and `dump_registers.py`'s lock indicator
  required it, so **every successful restore printed as failed** until `96efeec`.

  **Never steer the sync type off it.** Choosing `rto->syncTypeCsync` by reading
  `VSACT` is self-latching: the bit only reports correctly once the sync type is
  already right, so a unit that lands on csync stays there. `applyPresets()`
  decides with `sourceHasOwnVsync()` instead, which switches `SP_EXT_SYNC_SEL`
  and asks whether a V sync line actually arrives.

  **IT RUNS PER INPUT CHANGE, AND A MODE CHANGE REUSES WHAT IS HELD.**
  `establishSyncType()` keys on the input selection and says so, because a source
  changes its sync type far more rarely than it changes mode and a composite
  source spends the whole probe window every time. **So a source that changes its
  sync type on one cable is not followed** -- the RISC PC sets it from CMOS, and
  a `SYNC` command leaves the engine on the path it held. Only a line count the
  wrong path cannot produce arms a re-probe, and at 720x480@60 the wrong path
  counts 518 against 525, which is plausible enough that nothing fires.
  **`/sc?~` is what forgets it**, and `SP_SOG_MODE` read back is the only witness
  that the type in force is the one the source is sending -- check it before
  believing any measurement taken across a sync-type change, and note that
  FREEZING across one blocks the re-probe outright.
  `docs/sync-type-selection.md`,
  `docs/investigations/a-sync-type-change-arms-no-probe.md`.
- **The scaling path and bypass are told apart by `DAC_RGBS_BYPS2DAC` and
  `OUT_SYNC_SEL`**, both 1 in bypass and 0 on the scaling path.
  `DAC_RGBS_ADC2DAC` is NOT the tell, being 0 on both, and the scale registers
  are not either, since bypass leaves `VDS_HSCALE`/`VDS_VSCALE` on the last
  scaled load's values. **Establish the path before reading anything the input
  formatter owns as a fault** -- in bypass the IF is out of the circuit and its
  registers measure nothing. `docs/rgbhv-bypass-trap.md`.
- **Judge only settled samples.** Discard ~6 s after any mode change. Raw
  sampling across a sweep throws garbage at nearly every change that resolves on
  its own; scoring those produced 15 false positives in one run.
- **Preset loads leave things behind**, and the symptom lands somewhere
  unrelated. A yellow-tinted picture is `DAC_RGBS_B0ENZ` (s0 `0x45` bit 0)
  cleared by a bulk table load that never got patched back — the firmware never
  writes that bit to 0 anywhere. Fix: `curl '…/setreg?s=0&r=0x45&v=0x11'`.
  `docs/preset-load-clobber.md`.
- **"Something writes it" is not "something owns it", and there are TWO levels
  of that.** The first is settled: a field written only by `setResetParameters()`
  or one of the two bypass switches has no owner on the *scaling* path, because
  those functions leave a state a preset had to undo. The second cost step 4 a
  second time and no tool can see it: attribution is per FUNCTION, so a field
  `doPostPresetLoadSteps()` writes only inside `if (rto->outModeHdBypass)` or
  `if (rto->inputIsYpBpR)` reads as owned by a function that unquestionably
  runs. **22 fields were in that state on 2026-08-15 with `--gap` reporting
  zero.** A brace-depth heuristic flags an order of magnitude more than matter
  and was reverted. **The check that works is a bench diff against a known-good
  dump, minus the divider-derived differences.**
  `docs/investigations/preset-abandonment-audit.md`.
- **`/uc?h` does not clear Mode Detect.** It sets `presetPreference =
  Output480P` — a persistent user preference — and force-calls `applyPresets()`.
  It appears to "fix" railing by reloading a preset. The actual Mode Detect reset is
  `resetModeDetect()` (`SFTRST_MODE_RSTZ`, s0 `0x47` bit 1), reachable via
  `/setreg` — but it does **not** recover a bypassed IF, and nothing in the
  firmware resets Mode Detect while sync is present.
- **THE 535-LINE GATE IS GONE, and it was never a property of the part.** An
  RGBHV source used to enter bypass above 535 lines with no branch to leave by,
  so it stayed there for the life of the boot. Removed once the bench measured
  what it was costing: 800x600 at VTOTAL 627 scales sharp and full screen, with
  `PLLAD_MD` 1124 latched against the bypass switch's hardcoded 1856.
  **`preferScalingRgbhv` decides now**,
  both ways, and bypass is still reachable by turning it off (`/uc?x`).
  `docs/rgbhv-bypass-trap.md`.
- **`produced` IS `capture x 1024 / scale`** — a simple multiply, both axes, no
  loss term at either end. It did not look like one because the deficit against
  it *changes sign* with magnification, so four models were proposed in one
  evening and three refuted. The deficit was never in the span:
  `produced` was being measured from a corner assumed constant, and the corner
  moves. **The write start is `VDS_?B_SP + START_CONST + START_PER_MAG x
  magnification`** — 55 + 25m horizontally, 0.2 + 0.8m vertically — which is
  pipeline latency before the first write, seen from the far end where it could
  only look like loss. Measuring a fixed span from a moving origin gives a length
  that appears to vary. `docs/scaler-geometry-model.md`, `measure_origin.py`.
- **A good fit is not evidence the quantities are what you think.** The refuted
  two-term model fitted all eleven readings to 0.43 px and was still wrong;
  residuals could never have caught it. Only measuring the near edge did.
- **A measurement through an edge you cannot see is not a measurement**, and the
  specific failure is creeping until the *picture* stops and recording it as
  where the *screen* stops. That produced `CORNER_H` 129, `ORIGIN_OFFSET_H` 78
  and 94, `PANEL_VISIBLE_LEFT` 127 and a vertical bezel of 63 — every one of them
  a write start wearing another name, and 127 and 78 are the same reading filed
  twice. **Put content beyond the edge first**: set the picture to overrun the
  raster on all four sides, then creep, and every boundary has live video either
  side of it. Measured that way the bench panel shows `90..1351 x 41..1121`, not
  the 127/63 carried here for months.
- **Two points cannot disconfirm a line.** Three magnifications is the minimum
  that can fail, and `measure_produced.py` and `measure_origin.py` print
  residuals so they can.
- **Don't pin a CONSTANT to a panel edge**, because where a display stops
  showing is a property of that display: `geometry_math` centres on the raster
  instead and the user finds their own edges with pan and scale. The vertical
  visible region is derivable — `1121 - 41 = 1080` exactly, the encoder's active
  window, same on every display.
  **This is not licence to explain a discrepancy as overscan on THIS bench**,
  where the set is on FULL for every mode and paints the whole active area. A
  measurement taken here is portable in the direction that matters: what the
  board emitted.
- **The engine CALCULATES every register from held state. Registers are an
  output and are never an input.** Everything the engine needs, it already
  knows, because it computed it: the raster, both scales, both windows, the
  divider. Reading one back to derive another is what most of the geometry bugs
  here were.

  **The rule is two rules, and conflating them blocks correct work.** One is
  that **every field has a single owner**, and all writes to it go through that
  owner. The other is that **nothing is read back to derive something else**, so
  a value is derived once and a previous solve's state cannot reach the next
  one.

  **The exception to the read rule is a TRUE MEASUREMENT — something only the
  chip can tell you about the SOURCE.** `STATUS_SYNC_PROC_*` is the obvious
  family, and it is not a closed list: the source's hsync polarity is as much a
  measurement as its line count. What disqualifies a read is asking the chip for
  a value the engine itself put there.

  **BOTH RULES ARE SINGLE RESPONSIBILITY, AND THE EXCEPTION LICENSES ONE OWNER
  RATHER THAN EVERY CONSUMER.** Every measured fact about the source —
  field rate, line rate, line count, sync polarity — is owned by
  `Tv5725::SourceMeasurement`, and a class that needs one asks for it. Taking a
  second reading of a fact something else already holds is the same defect as a
  second writer on a field: `FrameSync` timed the source's vsync period on
  `DEBUG_IN_PIN` inside its own correction, the two instruments disagreed, and
  the unfiltered one won — a missed edge ISR reads a WHOLE MULTIPLE of the
  frame, which scales the phase target with it and slams the correction to its
  clamp. **An agreement test does not rescue a second owner**: agreement rejects
  noise and cannot reject bias, and two readings of a doubled frame agree with
  each other perfectly. Only a check against something independently known
  rejects that, which is what `rateIsPlausible()` is and what the period had
  none of. `docs/investigations/the-frame-time-lock-saturates.md`.

  **Writing a register FROM a measurement is ordinary, not an exception.**
  `SP_HS_INV_REG` set from the measured polarity is the same kind of act as any
  other solved register — one owner, derived once. Prefer it over carrying the
  polarity into the solve: **normalising at the hardware boundary removes a
  variable from the solver, and one fewer input retires a whole class of bugs
  rather than one instance of it.** Measure it where the sync arrangement is
  already being determined, write it once, and let everything downstream see one
  shape.

  Two consequences. **A register written by anything outside the engine corrupts
  the calculation**, which is why the OSD, the IR handler and the web pads must
  call the engine rather than write registers — and why a value the engine did
  not choose is *adopted* explicitly (`SourceMeasurement::adopt()`) rather than
  silently read. And **state living outside the registers is not a problem to
  solve**: it is the design. Hold it, derive from it, expose it over
  `/geometry` so tests and presets can set it.

  **Inheriting means reading it off the chip**, and every geometry fault of
  2026-08-06 was one: inheriting the corner put 41 px of the previous frame down
  the left of the screen, and inheriting the picture size froze a picture at 620
  lines that no zoom step could grow. `scale_step` deliberately takes no `scale`
  argument — there is a test asserting the parameter does not exist, because its
  existence was the bug. Every pad press recomputes every window, pan included.
- **The output raster is computed too, since 2026-08-13.**
  `Geometry::solveRaster()` derives both totals, both sync pulses and the display
  clock seed from the frame height and the measured field rate, so a preset
  table's raster bytes are overwritten on every mode change. Measured
  1436 x 1126 @ 80.85 MHz before, **1915 x 1126 @ 107.81 MHz after** — a third
  more horizontal resolution. Two rules come with it. The order is raster →
  clock → windows → rate steer last — expressed inside `Geometry::poll()`, which
  drives the whole sequence, because the display clock is steered from the seed
  the raster chose. Running the steer early gave a 31 Hz frame and a black
  screen.
  And **a wider raster costs zoom range, because the zoom floor is
  `raster / maxMagnification` while the default capture is a property of the
  INPUT line alone** — `1126 x 0.864 = 973` here — so the two do not
  track and widening the output silently eats the travel. That is the mechanism;
  the numbers below are what it cost, twice.

  `AxisHorizontal` used to magnify at most 2.048x (`scaleMin` 500, swept 2026-08-09).
  At the 1436 raster that left 307 units of travel; when `Geometry::solveRaster()`
  made the raster 1916 on 2026-08-13 it left **73**, at which point the
  horizontal scale clamps before the picture reaches full screen.
  **THE FLOOR IS PER AXIS, `Axis::magnificationFloor()`: 342 horizontally,
  which is 2.994x, and 205 vertically, which is 5.0x** — so the floor follows
  the raster (`room x floor / Unity`, about 640 at 1916) and no longer
  collapses each time the output widens. Both are PICTURE-QUALITY bounds and
  not derivations, and they differ because the evidence does: past 3.0x the
  horizontal scaler starts picking wrong samples, which is what `d882dff75`
  put the horizontal one there for and `docs/known-issues.md` carries the
  sweep, while nothing measured bounds `VDS_VSCALE`, a different register in a
  different stage. A shared floor put the horizontal finding on both and
  letterboxed every source too short to reach 3.0x.

  **RD-5725-1.1 states no minimum for `VDS_HSCALE`**: it gives
  only `HSCALE = 1024 x in / out` and the field is 10 bits, so there is no
  hardware bound to calculate and any floor is a picture-quality choice. Where
  interpolation starts to look bad depends on the resolution ratio, so it is the
  user's to find by zooming: only the picture can say when it is clean.
- **The output hsync position affects left-hand corruption, and nothing models
  it.** `VDS_HB_SP` below 8 corrupts, measured with `VDS_HS_ST` at 10; left-hand
  corruption that survived everything else then cleared by moving the pulse to
  62..77 (later, and a third as wide). The tempting reading — that the floor is
  `VDS_HS_ST - 2` — is **refuted by that same state**, which is clean with
  `VDS_HB_SP` at 9, fifty units before the pulse. Position or width, one at a
  time, is the experiment. Treat 8 as measured at one hsync setting only.
  `snapshots/hsync-tuned-no-left-corruption-2026-08-06.json`.
- **THERE IS NO CAPTURE BOUND, AND THE TAIL BAND IS NO LONGER REACHABLE.** The
  band at the end of the line is the VDS's one-line delay, `VDS_D_RAM_BYPS` —
  the OLED's Menu->Color->Line filter, `uopt->wantVdsLineFilter`, `/uc?m` —
  which sits DOWNSTREAM of the capture and destroys the tail of every line past
  `VideoProcessor::LineBufferSamples` captured samples. **The solve refuses that
  state both ways**: `dividerCeilingForLineFilter()` holds the divider down
  until the captured line fits the delay, and where the source has more pixels
  than the delay holds `applyLineFilter()` leaves the delay out and keeps the
  samples. So the band is a property of the buffer rather than an artefact a
  session will see, and the constants that bounded the divider against it are
  deleted. **The buffer's length looks like a hardware limit and a real fix is
  pending**, so what is left is the trade: the delay costs sampling density, or
  it is out of circuit. A framing widened past the buffer by hand still pays the
  ceiling without the delay — `docs/known-issues.md`.
  `docs/investigations/the-tail-green-is-the-vds-line-filter.md`.
- **THE LINE COUNTER IS ELEVEN BITS AND THE REGISTER IS TWELVE**, so 2047 kept
  samples per line is the wall on every source and both scan modes — and a
  read-back cannot test it, because `s1_0F` bit 3 stores a 1 and returns it
  while the counter ignores it. No status field reacts either: a counter of
  1500 on a 1103-unit line tears the picture in half with `STATUS_IF_HT_OK`
  still 1. Pass-through hits the same wall in `HD_HSYNC_RST`. **Only the
  picture can measure this**, and the discriminator is `correct + 2048`, which
  an eleven-bit counter must render indistinguishable from `correct`.
  `docs/investigations/the-line-counters-are-eleven-bits-measured.md`.
- **THE KEPT COUNT IS THE RULE AND NYQUIST IS BEST EFFORT.** What carries the
  source's pixels is the count the decimator KEEPS — `PLLAD_MD` undoubled, and
  **half of it where the line is doubled**. Oversampling acts on the CONVERSION
  rate (`converted = kept x ratio x samplesPerUnit`, the doubler's own
  decimation being a second factor of two), so it buys freedom from aliasing and
  **buys no resolution at all**. `SamplingClock::recommendedDivider()` therefore
  maximises the kept count and takes a ratio only where it costs none of it.
  **The asymmetry is the reason**: a source that could have been oversampled and
  was not aliases the top of its band, while one starved of samples to pay for
  oversampling loses resolution no filtering brings back.
  **The divider therefore moves with the measured line rate** wherever the row
  binds rather than the counter, so a wobble re-latches the ADC PLL.
  `docs/sampling-table.md` is what every input mode lands on, generated from the
  firmware — regenerate it, never edit it.
- **The horizontal axis has no native resolution.** The chip sees sync edges, not
  pixels, so the source's pixel clock is unknowable and 320x256 and 640x256 are
  indistinguishable. Capture is in ADC sample units, and how many there are per
  line is your choice (`PLLAD_MD`).
- **Blanking cannot be auto-detected.** A border is black *active* video,
  electrically identical to back porch. Sync-domain measurement finds the raster,
  never the picture inside it.
- **The headroom rule is RETRACTED — do not reinstate it.** The old rule
  (`memory window - produced >= ~13 px`) rested on `SOLVED-mode13-fullscreen-clean`,
  whose `VDS_DIS_HB_ST` of 1372 blanked 74 px of a picture ending at 1446 — and
  tearing shows worst at the *right* of the line, so its evidence was hidden.
  Points taken the same evening at HSCALE 993 and 850 failed identically, with
  24.9 px and 163 px of picture hidden. **Any headroom measurement is worthless
  unless the display window contains the whole picture.**

  Only two points survive scrutiny, and they do not fit a rule:

  ```
  HSCALE 1023 (x1.001) -> produced  798.78, edge at VDS_HB_ST  881    33.2 px
  HSCALE  850 (x1.205) -> produced  961.36, edge at VDS_HB_ST 1095   ~84.6 px
  ```

  The requirement is **not monotonic in HSCALE**, and the corruption comes in
  **multiple stable bands**, so an edge found by creeping down is only the true
  edge if you creep past all of them. More measurement does not fix that.

  So `geometry_math.py` no longer computes a margin: the memory window IS the
  display window, `VDS_?B_ST == VDS_DIS_?B_ST`, allocating nothing spare, and
  `HEADROOM_WARN_PX = 100` is **a floor to warn below, not a budget to reserve**.
  Banded non-monotonic thresholds look like marginal signal integrity, in which
  case the numbers are facts about *this board*. A torn picture is still not
  automatically a fault to chase.
- **The zigzag is NOT HSCALE-banded, and that is measured.** `VDS_HSCALE` was
  swept by hand across the corrupted state on 2026-08-09 and **no value cleared
  it**. The previous session's reading — clean at 823 and 762, torn at
  795, therefore banded like the left-edge corruption above — does not survive
  a full sweep. Do not reach for "move the scale" as the explanation or the fix.
  The two banding observations are **separate**: the headroom/left-corruption
  bands above stand, the zigzag one does not, and merging them is what produced
  the wrong call. An earlier "restoring a snapshot fixed it" was the snapshot
  being a different *mode*, not a different scale.
- **The sync processor counts in ADC samples, not IF units.** Settled
  2026-08-09 and worth stating because the opposite was assumed. Three of its
  registers hold values above the 1277-unit IF line — `SP_RT_HS_SP` 2374,
  `SP_H_CST_SP` 1667, `STATUS_SYNC_PROC_HTOTAL` 2553 — and `HLOW_LEN` only
  matches the source's mode file in ADC: `181/2553` = 7.09% against AKF50's
  `36/512` = 7.03%, where reading it as IF units gives 14.17%, twice the mode's
  sync width. **`SP_CS_CLP_ST`/`SP` ARE THE EXCEPTION AND THEY COUNT THE 27 MHz
  REFERENCE**, which `SP_CLP_SRC_SEL` selects and every path here asks for.
  Measured by walking the register against the emitted frame on the Wii at 480p
  into 1080p, divider 1212: **2.667 emitted columns per count** against 1.885
  per ADC sample, so reading the pair in ADC samples or IF units is out by a
  factor of 1.4. A reference count and a source pixel coincide on that source --
  858 x 31469 is 27 MHz -- so the walk rules the other two out and the clock
  selection names the survivor.

  **So the small values are the back porch, and the live bound is where the
  picture starts** -- 0..66 usable on that source, picture from 67. Past it the
  black level comes off picture and **the whole frame darkens with it**, a
  picture mean of 152.4 at a start of 108 against 205.7 at 19. So a dark or
  washed-out frame is as much a clamp symptom as the visible bar, and **neither
  shows in a register dump** -- every register reads correct under both.
  `docs/investigations/the-clamp-had-two-owners-and-two-units.md`.

  **`SP_H_CST_SP` IS A SECOND SUCH EXCEPTION, AND THE 1667 DOES NOT SETTLE
  IT.** Above the IF line rules out IF units and nothing more: the third
  candidate is **27 MHz counts**, and `SyncProcessor::coastLineFor()` writes the
  register in exactly those — `27e6 / lineRate`, which is 1728 on that source,
  so the 1667 read back is the firmware's own `0.968 x 1728` and is evidence
  about the formula rather than about the hardware. **The two readings only
  diverge away from the divider the formula was fitted at**, 1728 against 2553
  there and 858 against 1446 on the Wii at 1080p, so a coast window that scores
  well at one divider says nothing about another. Ask the COUNT at two
  dividers with the window placed, not the register.
- **`SP_H_PULSE_IGNOR` IS IN ADC SAMPLES TOO, AND A BYTE THAT READS ONE DIVIDER
  IGNORES THE SOURCE AT ANOTHER.** 107 was measured right through a 2200-sample
  line and ignored the Wii's 106-sample pulse through the 1446 a 1080p raster
  affords, which is the whole of why the Wii never acquired at 1920x1080. It
  follows the divider now, 49 per thousand of the line, and the search writes
  16 because an unlocked block counts at the 27 MHz reference, where the same
  pulse is 63 or 31 samples -- so a threshold sized for a divider cannot be
  written before the clock is latched.
  `docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md`.
- **EDID is unreachable.** The MS9288A HDMI encoder is on no MCU's I2C bus (see
  the schematic), so output rasters are chosen blind.
- **Sync stability does not mean the divider is right.** `getStatus16SpHsStable()`
  passed happily with `PLLAD_MD` halved from 2553 to 1276, and the display went
  solid green — the sync processor locks onto edges regardless of whether the
  ADC is sampling the line the way the rest of the pipeline assumes. Anything
  choosing `PLLAD_MD` automatically needs a real validity test; "did sync
  survive" is not one.
- **Don't read the scaler's raster as what the TV sees.** The MS9288A samples
  the analog output and re-encodes it, so `VDS_HSYNC_RST` and friends describe
  the scaler's own timing, not the HDMI mode the display locks to.
- **Read `s0_49` before chasing any output register — half the output pads are
  off.** The MS9288A takes the analog RGB output plus HSOUT/VSOUT, so the digital
  video output port is never driven, and the preset tables switch it off:

  | `s0_49` bit | | every scaling preset |
  |---|---|---|
  | 0 | `PAD_CKIN_ENZ` | 0 — external clock in, **enabled** |
  | 1 | `PAD_CKOUT_ENZ` | 1 — CLKOUT pin, **disabled** |
  | 2 | `PAD_SYNC_OUT_ENZ` | 0 — HSOUT/VSOUT, **enabled** |
  | 3 | `PAD_BLK_OUT_ENZ` | 1 — HBOUT/VBOUT, **disabled** |

  Two consequences that have each cost time. **`VDS_EXT_HB_*` and `VDS_EXT_VB_*`
  do nothing.** They program the HBOUT/VBOUT blanking ("this blanking is for
  external used") and those pins are off; `VDS_SYNC_IN_SEL` is 0 as well, so the
  VDS takes sync from the IF module and there is no internal consumer either.
  `applyPresets()` copies `VDS_DIS_?B_*` into them and
  nothing refreshes them afterwards, so they are *always* stale after the first
  pad press — measured at `EXT_HB` 348..1356 against `DIS_HB` 99..1501, with a
  perfect picture. Writing `VDS_EXT_HB_*` by hand changes nothing either. Do not
  add them to a solver and do not suspect them for an edge artefact.

  And **the datasheet's 108 MHz `CLKOUT` rating is a pad spec for a pin nobody
  loads**, so it is not the proven limit on the display clock. What bounds the
  internal VCLK is stated nowhere; the divider register offers 129.6 MHz and
  162 MHz above it, and `ntsc_1280x1024`/`ntsc_240p` already ship `0xA5`.
  The 2026-08-11 sweep settled the range: 108 MHz and **129.6 MHz both work and
  are sharp**, 162 MHz flickers then goes black. So 129.6 is the measured
  ceiling — `OutputMode::WorkingCeilingHz`. `src/tv5725/OutputMode.h`.

  **The engine nevertheless asks for 108, and that is not a contradiction.**
  `OutputMode::EngineCeilingHz` is a *usability* limit, not an electrical one.
  Three constants, three different questions — do not collapse them.

  **RAISING IT HAS BEEN TRIED AND IT IS A SETTLED NO — do not propose it as an
  untried experiment.** The reason is not the zoom travel an earlier reading
  gave, which is spent: a clock buys raster width and **raster width costs
  MAGNIFICATION**, because the capture is the source's and the solve magnifies
  it to fill whatever raster it was handed. Past a magnification the part does
  not state, the playback pipeline has no data for the head of each line and
  shows zeros as green then stale memory as a grey comb. Measured on the RiscPC
  at 320x256@50 with a capture of 973 units, 108 MHz gives 1.84x and is clean
  where 129.6 MHz gives 2.22x and a run-up on the picture. **It cannot be
  blanked or moved** — blanking opens the display window later and clips panned
  content, and moving the output hsync slides the whole picture, the encoder
  holding its sampling window fixed to that pulse — and framing cannot escape it
  either, a 2304 px raster magnifying past the onset even at the whole
  capturable region. `docs/investigations/display-window-opens-early.md` carries
  the sweep and the measurements; `src/tv5725/OutputMode.h` carries the three
  ceilings and what each one answers.

  **AND THE RASTER IS NARROWER FOR A 60 Hz SOURCE THAN A 50 Hz ONE, WHICH MOVES
  THE CAPTURE BOUND WITH IT.** The horizontal total is the display clock over
  the output's own lines and **the SOURCE's field rate**, so one output
  resolution is several rasters: 1080p solves 1920 at 50 Hz and 1602 at 59.94,
  and `VideoPath::dividerCeilingForOutput()` follows it — 1724 against 1446 for
  the same source line. So **1080p is the mode with the LEAST horizontal room,
  and a 60 Hz source is its worst case**, which is why a fault keyed on the
  divider shows on the Wii and never on the 50 Hz bench RiscPC. Judge a divider
  against the raster the source's rate solves, never against the output's name.

  The live trap is the inverse: `ofw_RGBS` and `ofw_ypbpr` are the only presets
  that **clear** bit 3, so loading either brings HBOUT/VBOUT alive carrying
  whatever stale window was left behind.

## Working with the unit

Snapshot before changing anything, diff after:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> --out snapshots/before.json
python3 tools/gbsc-pro-hwtest/snapdiff.py --diff snapshots/before.json snapshots/after.json
```

**The archive is `tools/gbsc-pro-hwtest/snapshots/`** — 100 tracked states plus
`LOG.md` saying what each one was. The `snapshots/` directory at the repo root
holds a handful of recent scratch dumps and is *not* it; reading this section as
pointing there found three files, concluded the archive did not exist, and cost
a session on 2026-08-14. To recover a working picture:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore tools/gbsc-pro-hwtest/snapshots/dis-hb-st-tweak-2026-08-03.dump.json \
  --segments 0,1,2,3,4,5 --repeat 2
```

**A restore latches** since `96efeec` — `PLLAD_LAT` is what loads `PLLAD_MD`,
`ND`, `KS`, `CKOS` and `ICP`, and a snapshot cannot carry a rising edge. Before
that fix a restore across a change of divider left the ADC PLL on the old one
with **every register reading back correct** and the screen black. If you write
registers back by any other route, latch them yourself.

**Not every "known-good" is a full dump.** Some are 11-field summaries
(`known-good-riscpc-320x256-50-2026-08-11.json`), which `--restore` cannot use
and `snapdiff.py` crashes on rather than reporting the mismatch. Check the size
before planning an experiment around one.

**Restore every segment, and note that this line used to say `1,3,4,5`.** Low
power calls `setResetParameters()`, which zeroes segment 0 — DAC power, the pad
enables, the display clock select — and segment 2, so a restore that skips them
cannot recover from the state you are most likely recovering from. Segment 2 is
the expensive one to miss: the registers that matter there are *bypass* bits
(`MADPT_PD_RAM_BYPS`, `DIAG_BOB_PLDY_RAM_BYPS`, `MADPT_Y_WOUT_BYPS`,
`MADPT_VIIR_BYPS`), so zeroed they route video *through* the deinterlacer RAM
with every coefficient at zero, and the screen fills with random colour pixels
while all 608 config registers still read correct.

**A restore is not a recovery on its own.** The firmware has to agree that the
source is present, or `sourceDisconnected` stays true and `loop()` keeps
ratcheting `ADC_SOGCTRL` down every 500 ms underneath the picture you just
restored — see the freeze note below, because `/freeze` does not stop it.

The two snapshot formats are not interchangeable: `dump_registers.py` writes 496
config registers, `snapdiff.py` writes all 1536. Diff like against like.

**AND A DIFF OVER THE SMALLER SET IS NOT A DIFF OVER THE MACHINE.** Comparing a
608-register dump against a full one leaves 928 addresses outside the comparison
entirely, and 114 of those hold values that are neither 0x00 nor 0xff on a
settled unit — s1 0x60..0x77 is a live table, not padding. So *"a full dump
either side differs in only five fields"* is a statement about whichever subset
was taken, and every such conclusion here was drawn over the config range. When
the question is **what state survived an excursion**, take `snapdiff.py --save`
at both ends; the config dump answers a narrower question than it appears to.

`geometry.py --host <ip>` prints the input side, output side, and where the three
horizontal extents disagree — the fastest read on why a picture is wrong.

**NEVER hand-write a field's segment/register/offset/width. Read it by NAME,
and the by-name call is the SHORTER one:**

```python
from gbs_unit import read_named, read_fields
read_named(host, "PLLAD_MD")                                  # one field
read_fields(host, ["PLLAD_MD", "STATUS_SYNC_PROC_VTOTAL"])    # many, one request
```

`read_field(host, 5, 0x12, 0, 12)` is the same read with four numbers nobody can
check, and it is reserved for a slice that genuinely has no name.

**This rule is broken every session, and the reason is ambient rather than
ignorance.** `tools/gbsc-pro-hwtest/` is full of literal `read_field(host, 3,
0x02, 4, 11)` calls, so the wrong shape is what surrounding code models — and
this section used to demonstrate the rule with a three-line recipe that ended in
`read_field()` with the numbers pre-extracted, which is *more* typing than
inventing them. **Do not copy the slice style out of the tests.**

A wrong NAME raises before a request goes out. **A wrong ADDRESS does not error,
it returns a plausible number**, which is what makes this expensive rather than
merely tedious. Two ways it has cost real time:

- four invented addresses read as `SP_VTOTAL` 71 steady, `HTOTAL` 2723 against a
  2250 divider and an 88% sync duty — a textbook no-lock signature, diagnosed as
  a dead source, while the picture on the screen was perfect. By name the same
  registers give 311, 2250 and 7.1%.
- checking RGBHV bypass by hand-written slice gave `DAC_RGBS_BYPS2DAC` 0 and
  `OUT_SYNC_SEL` 0 on a unit correctly *in* bypass with a full-screen picture,
  which reads exactly like the "no raster at all" failure. By name: 1 and 1.

**If a status read disagrees with what the screen is doing, check how you
addressed it before believing the registers.**

`tools/gbsc-pro-hwtest/tv5725_registers.json` is the map behind all of these, and
the same one `snapdiff.py` and `setfield.py` decode with. `setfield.py --set
NAME=VALUE` writes by name, read-modify-write, so a field sharing a byte with its
neighbour does not destroy it. `dump_registers.py` reads everything in one pass.

### THE BENCH TELEVISION DOES NOT OVERSCAN. IT IS SET TO FULL, ON EVERY MODE.

**So a photograph of the panel shows the WHOLE HDMI active area, and content
missing from a photograph was not sent.** Every mode, not just the ones someone
checked.

**THE GUARANTEE IS THE SET BEING ON FULL, AND IT HAS ANOTHER MODE.** Put it in
its own 4:3 mode and it CROPS -- it is fitting a 16:9 signal, so it takes the
sides off rather than pillarboxing, and an hour went into diagnosing that as a
firmware fault. The board emits the shape itself now
(`docs/aspect-ratio.md`), so the set has no reason to leave FULL; if a
measurement disagrees with every other instrument, check which mode the set is
in before believing it. Nothing on this page may be read as the set hiding picture, and a
discrepancy may not be explained away as overscan -- that explanation is closed,
and reaching for it has cost several sessions and at least one wrong retraction.

**AND A SET OFF FULL CAN COST AS LITTLE AS ONE PIXEL, WHICH IS WHY IT READS AS
A BOARD FAULT.** It is not always the obvious crop above. `PATTERN CARD`'s green
frame is one SOURCE pixel wide, so a set off FULL can show no border on any edge
while the emitted frame carries it on all four -- measured at 320x256@50 into
1080p with 16:9 declared, the USB capture reads 2 px of black at the near edge
and 0 at the far, and the panel beside it showed none at all until the set went
back to FULL. **THE TWO INSTRUMENTS DISAGREEING AT ONE PIXEL IS THE SET'S MODE**:
ask the capture, and a border that returns when the set goes to FULL is the
whole diagnosis. Weakening the rule above on the strength of the panel alone is
the wrong retraction it warns about.

Two consequences, both the opposite of what is easy to assume:

- **The panel is a valid instrument for where the OUTPUT ends**, vertically and
  horizontally. A row or column count against a known feature is a real
  measurement, not a measurement of the set.
- **A "clip" seen on screen is the board's.** If the picture is short at an
  edge, the board did not emit it.

What still is NOT a panel property, and is the thing that actually blocks a
naive edge measurement: **the engine always scales the capture to fill the
raster**, so there is never blanking above or beside the picture to measure
against. That is a property of the solve. Count a feature of the SOURCE.

**THE PATTERNS ANIMATE BY DEFAULT, AND `ANIM OFF` STOPS THEM.** `PM5544` flips
its four corner squares yellow/white twice a second (`PROCanimcorners`) and
`CARD` flashes its border yellow, so a feature caught in one phase and absent in
the other reads exactly like a crop and has been reported as one. **Turn the
animation off and a single frame is evidence again**; without it, measure
against something that does not animate -- the grid, the colour blocks, the
frequency wedge -- or record a clip and compare frames in the same phase.
`PatLib.bas` in the `RiscPc` repo says which is which.

**`CARD` CARRIES A ONE-PIXEL GREEN BORDER FLUSH TO ALL FOUR EDGES**, so "is
anything clipped" is a yes/no on one frame rather than a margin to compute.
Green on all four sides means nothing was lost.

**THAT IS THE ACCEPTANCE FOR A FRAMING: the border on all four edges with NO
BLACK between it and the picture's edge.** Read at 16:9 -- a 4:3 declaration
pillarboxes, the bars are OUR blanking, and a strip taken at the frame's own
edge then measures the bars instead of the picture. **And a stored framing
answers before the computed default**, so `/sc?B` forgets this source's entry
first or what is judged is the last session's tuning. `four_edges.py` reports
the border's green dominance and the black count at each edge together.

**`PATTERN` WITH NO ARGUMENT REDRAWS THE DEFAULT rather than reporting what is
up**, so asking what is on screen changes it -- and `MODE` repaints too. Re-issue
`PATTERN CARD` after either, or the measurement is taken against a card nobody
selected.

### The shape of an eye-in-the-loop measurement

Where the picture is the only instrument, the tool is a **jog the user drives**,
not a sweep the session drives. `creep_window.py` is the worked example — coarse
and fine keys, one keypress per step, marks recorded as it goes. Build the next
one the same way; the rules below are each a wasted session.

- **A scripted sweep with a prompt per step does not work.** Numbered steps
  cannot be reported back reliably, and the answer arrives after the state has
  moved on. Give the keys to whoever is watching the screen.
- **Never jump to a value.** A transition that happens between two frames nobody
  saw is not a measurement. Creep *to* the interesting value, do not land on it.
- **Never bisect.** Corruption comes in bands, so a boundary found by halving is
  only the real one if nothing clean lies beyond it — keep creeping past the
  first edge to find out.
- **Enter means "clean, continue".** One keypress per step is what makes a
  one-unit creep bearable over eighty of them.
- **Marks belong in the tool.** Values printed to scrollback are lost; a mark
  with a note, printed as a table on exit, is the measurement.
- **Read every register the arithmetic uses in ONE pass.** The engine re-solves
  as the measured field rate wobbles, so a capture from one solve paired with a
  window from another invents discrepancies. A 46 px overshoot was reported that
  way and did not exist.
- **Freeze automation first**, or the solver rewrites the windows underneath the
  experiment.
- **BUT A FROZEN POKE CANNOT JUDGE WHERE THE PICTURE STARTS ON THE EMITTED
  FRAME**, because the transmitted window's start is latched at link-up and no
  register write re-locks it. Measured at 640x480@60: the pair the engine solves
  puts the source's outermost pixel off the left of the frame when written by
  hand into a frozen acquisition, and four columns inside it once the link has
  re-acquired — same two registers, opposite verdicts, and the frozen one is
  stable and repeatable.
- **A SOURCE MODE ROUND TRIP RE-ACQUIRES THE CAPTURE, AND ONLY A SYNC PAD
  TOGGLE RE-PLACES THE SINK'S WINDOW.** Re-issuing the source's `MODE` while
  automation is FROZEN makes the source leave and return with the hand-set
  register in force, so the engine's own re-acquisition judges it -- which is
  what makes a one-register A/B on the CAPTURE valid, and it agrees with
  `card_edges.py` on both in-scope VESA modes. It never toggles
  `PAD_SYNC_OUT_ENZ`, and the sink's window stays where the last mode change
  put it: measured on 320x256@50 into 1080p, three round trips at 172, 171 and
  172 with the framing zoomed for the middle one. **The sink places its window
  each time the pad is driven again** and remembers nothing between returns --
  158.9 with the picture at the aperture's edge, 171 with captured black there,
  on one state. **WHAT IT READS TO CHOOSE THAT IS OPEN, AND THE IMAGE CONTENT
  IS REFUTED**: see the shown-window entry above, and do not reason from "it
  hunts the first non-black column" in either direction -- a measurement taken
  with picture hard against the aperture's edge reports the APERTURE back, and
  the discriminator is the black at the LEFT rather than where the picture
  starts. So a measurement of where the SINK cuts the line needs a pad toggle
  in the state under test, and a measurement of the capture needs the round
  trip. The dongle needs ten seconds or so to deliver a
  usable frame after either, so a clip taken too early is all black and reads
  as a dead output. Read the register back afterwards -- a frozen unit is not an
  inert one.
  `docs/investigations/the-encoder-places-its-window-when-the-sync-pad-returns.md`,
  `docs/investigations/full-screen-framing-on-the-vesa-modes.md`.
- **THE SINK'S OWN POSITION IS PER RASTER AND NO TERM PLACES IT.** Measured
  across the stock AKF50 set into 1080p and 720p with `framing_sweep.py --tier
  S` and read with `framing_report.py --sink`: within an output it steps with
  the field rate in bands, two sources sit off their band, and a transition
  with PICTURE at the aperture's edge lands 5 to 9 units before it where a
  pad toggle on the settled state does not. So a frame judged after a mode
  change carries a placement a settled toggle would not give, and a model in
  T, the clock or the rate cannot close the 50 Hz 1080p rasters.
  `docs/investigations/the-sinks-own-window-position-is-per-raster.md`.
- **An open window past the end of the picture is not corruption.** It shows
  whatever the playback stage fetches, which looks like a fault and is only
  absence of data. Establish that there IS picture under the window before
  reading anything as a floor — force the magnification if need be.
- **A MAPPING FROM PHOTO COLUMNS TO OUTPUT PIXELS DOES NOT SURVIVE AN OUTPUT MODE
  CHANGE.** Measured twice: 57 columns adrift after a 1080p/960p/1080p round
  trip, and ~150 columns after a pass-through round trip, each with the raster
  registers read identical either side. Calibrate by differencing a frame at one
  `VDS_DIS_?B_ST` against frames at others, so the difference IS the strip the
  register blanked, and **re-calibrate after any output excursion**. Read against
  a stale mapping, a correct far edge reads as 110 px of overshoot and the
  line's repeat reads as the picture.

  **THE SCOPE IS AN EXCURSION, AND IT IS NARROWER THAN IT READS.** Both figures
  are ROUND TRIPS, and both were taken on the scaling path before
  `Tv5725::OutputMode` stated a standard's timings. Every mode it offers now
  carries one — CEA-861 for 1080p, 720p, 480p and 576p, VESA DMT for the rest —
  and a sink locking to a mode it recognises places it the same way every time.
  **Nothing records the picture moving between two acquisitions while one path is
  held**, and nothing records it on pass-through at all.

  **AND IT IS NOW MEASURED QUIET ON THE SCALING PATH.** `full_margins.py` carries
  the anchor and `counter_origin.py` does not, and on three modes spanning a
  19-sample error range at 1080p they agree within a sample -- so a margin taken
  at 100% framing reads the counter's own placement and needs no re-lock. The
  comparison crosses no output-resolution change and no pass-through excursion,
  which is where the 57 and ~150 column figures came from.

  So a blanking edge found in pass-through is a real edge and does not need
  re-checking against a second acquisition. **Read as a general licence to
  explain away a discrepancy, this entry invents constants**: a pass-through
  blanking offset justified by it rests on one frame photographed with
  automation UNFROZEN, where the registers in force at the exposure are not
  vouched for. Freeze, and read the registers either side of the shutter, before
  reaching for this.

  **THE DISPLACEMENT IS DOWNSTREAM OF THE BOARD, AND IT IS NOW MEASURED.** Both
  checks this entry used to demand have been run. A full `snapdiff.py --save`
  either side covers all 1536 addresses and the byte-identity has been
  *constructed* — writing every differing byte so the dump reports 0 differing
  leaves the picture where it was, as do resets of the video blocks, the SDRAM,
  the phase adjusters and the sync processor.

  **The instrument that localises it is the STV9426 overlay, and it needs no
  probe.** The television's menu is drawn from `HS_OUT`/`VS_OUT` and keyed into
  the video at U13, downstream of the VDS, so it rides the SYNC timebase while
  the picture rides the VDS's counter — one photograph carries both. At two
  landings they move together, overlay -100.78 px / +26.58 rows against picture
  -101.01 px / +26.43 rows, agreeing to 0.23 px and 0.15 rows with the camera
  controlled at lag 0, r = 0.998. Video cannot have moved relative to sync
  inside the scaler, so the analog frame is identical and the position is chosen
  after it.

  **What is still NOT established is which downstream part chooses it** — the
  MS9288A and the television are both there and a second display is what splits
  them — and *why* it differs between acquisitions. So the attribution that
  remains unlicensed is a specific mechanism inside the encoder, not the
  locality. Repeat the overlay measurement before believing any new claim about
  where the position is set; it costs one photograph per landing.
  `docs/investigations/the-picture-position-is-latched-not-re-rolled.md`.
- **Separate what the board must emit from what one display happens to show.**
  The MS9288A consumes the scaler's analog blanking and generates HDMI blanking
  of its own, so the minimum the scaler must emit is a board property, measured
  once, portable. Where a set stops painting is that set's overscan. Sizing a
  constant from the second reads as a fix and ships one panel's number.
- **BYPASS IS A SECOND VIEW, NOT A PANEL REFERENCE, AND IT IS NOT TRUSTWORTHY
  VERTICALLY.** The set does not overscan, so the panel needs no reference: what
  is on it is what was sent. And bypass has a vertical fault of its own --
  `known-issues.md` carries 800x600 in bypass clipping the top and leaving a bar
  at the bottom, **varying between entries** -- so it cannot anchor a vertical
  measurement. It remains useful as a second path to compare against
  horizontally. An 800x600 source in bypass passes straight through filling the
  screen:
  `printf 'MODE X800 Y600 C256 F60\n' | nc 192.168.88.10 6502`, then `/uc?x` to
  turn `preferScalingRgbhv` off — **the line count no longer puts it there, so
  the reference has to be asked for**. Take it at the same camera position as
  the frame being judged, compare the two, and `/uc?x` back afterwards.
- **PUT COLOUR AGAINST THE EDGE BEFORE MEASURING ONE.** At a default framing the
  last thing on screen is captured INPUT blanking, so "where the picture ends"
  measures the source's border and not the output's edge — a comparison against
  bypass is out by the whole border. Zoom in and pan until content is hard against
  the edge under test. Panning `-` moves the capture left and puts the image
  against the RIGHT edge; `+` exposes the source's blanking there instead.
- **USE PLAIN `tv-snap`, NOT `--full`, FOR ANY POSITION MEASUREMENT.** The
  rectification is a saved fixed homography in `~/.config/tv-snap/rectify` —
  lens correction, perspective and a scale, applied identically to every shot —
  so the plain rectified capture is what makes two frames comparable. `--full` is
  the keystoned raw sensor frame, and positions measured on it are not
  comparable between shots. No crop is saved (`tv-snap --show-crop` says so), so
  the panel is not cropped to a fixed frame either; setting one would improve it
  further, and the calibration frame has to be pass-through.
- **THE ROOM IS IN THE PHOTOGRAPH, SO ABSOLUTE BRIGHTNESS IS NOT A MEASUREMENT.**
  Held still, `tv-snap` is repeatable to a tenth of a grey level -- four shots over a
  minute on an untouched unit gave mean 88.9, 88.9, 89.0, 89.0 -- and that
  repeatability is exactly what makes the trap convincing. Opening a curtain
  moves it further than anything the scaler does, and the auto-exposure that
  follows lifts the mid-tones while leaving the blacks where they were, which
  reads as a gamma change rather than as light. A picture "looking washed out"
  across minutes is therefore not evidence about the unit, and a register diff
  taken beside it will offer plenty of innocent differences to blame.
  **What survives is a DIFFERENCE between states photographed close together,
  with the same state shot twice as the control** -- the shape
  `docs/investigations/framing-is-anchored-to-a-measured-pulse.md` uses.
- **A FIXED BAND OF A PHOTOGRAPH IS COMPARABLE ONLY WHILE THE PICTURE STAYS PUT.**
  The sync pulse, the playback fetch and the scale all move the picture, and a
  count inside a fixed band then reports the movement rather than the artefact —
  in both directions, so a starved fetch scores *cleaner* than a clean edge. It
  is the same error as measuring a fixed span from a moving origin. Anchor the
  band to a feature of the picture, and print that feature's position with every
  frame so a shift is visible in the results rather than hidden in them.
- **A FRAME DIFFERENCE IS NOT A DISPLACEMENT, AND IT IS BLIND OVER FLAT COLOUR.**
  Scoring a clip on `tblend=all_mode=difference` measures displacement
  MULTIPLIED BY LOCAL CONTRAST, so a whole line can shift across a solid block
  and read zero while a fraction of a pixel on a grating reads large. Per-row it
  then puts the entire score on the test card's frequency wedge, which reads as
  aliasing when the artefact is uniform jitter. **Measure an edge's POSITION
  instead** — average a flat strip to one row, take the centroid of the
  strongest luma gradient, and the spread across frames is the jitter in pixels.
  The bench floor established that way is 0.05 px.
  `docs/investigations/a-hand-set-divider-cannot-be-judged-against-a-solved-window.md`.
- **NO SUBSET OF AN INTERDEPENDENT SOLVE CAN BE SET BY HAND AND JUDGED.** The
  divider is one input to a solve that also sets the capture window, both
  scales, the playback fetch and the stride, so freezing automation and walking
  `PLLAD_MD` measures the MISMATCH against the last solve. It produced a clean
  cliff at 2005 across two line rates with the PLL group identical either side —
  which was `IF_HB_ST2` frozen at 2005, and every "clean" point had the window's
  stop past the end of a shorter line. **There is no count limit below 2047 and
  that figure must not be reinstated.** Compare engine-solved states, or change
  the rule and reflash.
- **An artefact that leaves the screen is hidden, not cured**, and which one it
  is decides whether it travels. Move the thing that generates it back onto the
  panel and re-measure before concluding anything: shift the picture back by the
  same amount the change moved it, then compare like with like.

## The diagnostic toolkit

Which instrument to reach for is above -- the console first, then `/geometry`,
`/samplinglog`, `/testbus`, fields read by NAME, the HDMI capture. This is what
to DO with them, and every entry here has had to be arrived at more than once.

### Whose black is it: lift the picture

A border is black ACTIVE video and our blanking is black, so counting black at
an edge cannot tell them apart and "is the picture flush" is unanswerable until
they are separated. **`VDS_Y_OFST` lifts the PICTURE's luma, the source's own
border with it, while the VDS blanking interval keeps the blanking value** -- so
after a lift every black row or column still at an edge is OURS and everything
grey is the source's. It needs `VDS_DYN_BYPS` 0, or the offset reaches nothing.

Measured on `vga` at 320x256@50 into 1080p: 7 black columns at the left and 2
rows at the top before the lift and **0 and 0 after**, so both belonged to the
source; 14 columns at the right under both, so **all fourteen were ours**.
Without it the two edges read alike and a deficit gets attributed to the wrong
end of the line.

**IN PASS-THROUGH THE LIFT REACHES NOTHING AND `HD_MATRIX_BYPS` 1 IS THE
ANSWER.** Video routes around the VDS there, so `VDS_Y_OFST` is out of circuit.
Bypassing the colour matrix leaves the picture green and the source's border
dark magenta at G exactly 0, which separates the same two blacks in one
register. Measured on the Wii at 480p into 1080p: border at the right ~50
emitted columns, bottom ~40 rows, top ~3, and **none at all at the left** --
so the bars a pass-through frame shows are the source's, and the left one is
not arriving. **It also refutes the black-hunt model**, because the border is
not black in that state and the window still does not reach it.
`docs/investigations/the-encoder-tunes-the-left-edge-in-pass-through.md`.

### Move the picture without disturbing the parity bias

The memory window's WIDTH parity is the zoom shear, so a probe that moves one
end of it changes the thing under test. Three shapes, each asking a different
question:

| move | what it asks | parity |
|---|---|---|
| `VDS_DIS_?B_SP`/`ST` alone | where the SINK cuts, and where the write reaches | no rule on the display window, so safe |
| all four by the same delta | where the picture COULD go | both widths intact |
| `VDS_?B_SP` alone | whether an artefact belongs to the WRITE or the aperture -- the write's own features follow it 1:1, the aperture's do not | **flips it**: move `VDS_?B_ST` with it |

### Creep in whichever direction the state calls for

A boundary found by bisection is only the real one if nothing lies beyond it,
and a creep that assumes which way the error runs finds nothing when it runs the
other way. Read the edge at the solved value FIRST, take the direction from
that, and creep a unit at a time until the state flips; the reading is the last
value before the flip, whichever side it started from. A walk that returns
nothing after its whole range is a walk whose premise was wrong, not an absent
boundary.

### Poll the registers across the change, not just the console

The console says what the engine DECIDED; it cannot say what moved first, and
its timestamps are delivery times that compress. A cascade -- a register we
write, then the count, then something keyed on the count -- is only visible as a
timeline of registers sampled a few milliseconds apart across the event. Where
the device's own figure for an interval exists, that is the one to believe.

### Compare against something that was working

Two baselines, and they answer different questions:

- **A known-good snapshot** from `tools/gbsc-pro-hwtest/snapshots/`, diffed with
  `snapdiff.py`. This says what OUR change moved. Take `--save` at both ends
  when the question is what survived an excursion, because the config dump
  answers a narrower question than it appears to.
- **The vendor image.** The repo's own 1.x tags are the stock board firmware, so
  a fault that might predate the rewrite can be scored against what the board
  shipped doing -- often by reading those sources rather than by flashing them.

  **DO NOT FLASH A STOCK TAG TO TAKE A REGISTER COMPARISON. IT CANNOT ANSWER
  ONE.** Upstream `1.3` registers no register route at all and its
  `dumpRegisters()` has every print commented out, and `be58a24f4` has
  `/getreg` but no `/getregs`, no `/input`, no `/samplinglog`, and a console
  heap gate of 20000 that this fork never reaches -- so the websocket takes a
  client and delivers nothing. Flashing one and then discovering there is
  nothing to read costs the flash, the comparison and the trip back.

  **`tools/reference-build/` is the answer and it is already built.**
  `be58a24f4-bench-instruments.patch` adds `/input`, `/samplinglog` and a
  usable console gate to the reference commit and nothing else; its `README.md`
  has the build line. Both sides are kept as images in
  `~/Projects/gbsc-pro-backups` -- `esp_ref-be58a24f4-instrumented_*.bin` and
  `esp_current-<rev>-samplinglog_*.bin` -- so a switch is one `flash-bin.sh`
  rather than a rebuild.

  Three things that bite on the way:
  **a full snapshot is 1536 single reads there**, about 90 s, `/getregs` being
  absent; **the switch reformats the filesystem each way**, SPIFFS against
  LittleFS, so `/framing.txt`, `/slots.bin` and the preferences go unless they
  are pulled over `/fs/download` first, and `SeleInputSource` comes back 0 with
  a `default:` case that transmits nothing, so the input needs reselecting;
  and **its detection sweeps for a live signal**, so the source with sync on
  another ADC input wins whatever is asked for -- with both bench sources
  powered, that build cannot be held on the one under test.

### Keep the shape and the pan still between readings

`reset_framing()` resets the declared ASPECT as well as the framing, so a run
that calls it to get back to a known state drops a 16:9 source to the raster's
default and narrows the room -- two readings either side of one are taken on
different pictures. Read `aspect` and `shaped` out of `/geometry` with every
measurement and put the shape back with `/uc?G`, which cycles it.

The pan matters the same way, and `framing_to()` lands on the granule rather
than the value asked for, so read `oh` back rather than assuming it.

### A black capture after a session of poking

Every register reading correct with nothing on the capture is the encoder
holding a stale timing, and the recovery is the `PAD_SYNC_OUT_ENZ` toggle rather
than the connector or a power cycle. Frozen poking across a source mode change
is what provokes it, because nothing re-presents the output while automation is
held. Measured: resets all 1, pad enabled, `STATUS_SYNC_PROC_HTOTAL` equal to
`PLLAD_MD`, `state: acquired` -- and a black frame until the pad was dropped and
driven again.

## Conventions

**EVERY DIFFERENCE MUST BE DELIBERATE, AND THE GOAL IS THE BEST PICTURE WITH THE
LEAST MECHANISM.** This is the point of the whole refactor and it is easy to
mistake for tidying, so it is stated first.

Where two paths do the same job differently, the question is *why*, and **"nobody
knows" is the answer that means collapse them**. An unexplained divergence is not
a risk to preserve carefully -- it IS the complexity, and preserving it because
removing it might change something is how it has survived this long. That
instinct reads as caution and is the opposite: it guarantees the next reader
inherits the same unanswerable question.

The worked example is the bypass route. Two existed, the second had no reason to,
and it went -- one route, less code, and the picture measured indistinguishable.
`docs/investigations/one-bypass-route-carries-rgbhv.md`.

So: keep a difference that has a reason, and write the reason down. Delete one
that does not. A faithful refactor of an accident preserves the accident.


**`CODING_STYLE.md` is the C++ style, and it is not optional.** Classes rather
than namespaces over file-scope globals, one class per file named after it,
declare-in-header/define-in-.cpp, minimal OO strongly preferring composition over
inheritance and using no virtuals, `NULL` for a null pointer rather than `0` or
`nullptr`, dependency injection over reaching for globals, a default of no
comment at all,
and a behaviour-preserving refactor proven by diffing the `--dump` oracles.
Every rule in it is there because it cost a session. Read it before writing
firmware C++.

- **The default is NO comment, and a densely commented file is a defect rather
  than a matter of taste.** Commentary reads as machine-written, and a heavily
  commented file says its author thought the code was unreadable. Nothing checks
  it, so removing it is a pass of its own —
  `grep -c '^\s*//' <file>` against `wc -l` is the measure. Write one or two
  lines of *why* only where a constraint is genuinely hidden, put anything
  longer in `docs/` with a pointer to it, and remember that extracting a
  well-named function beats explaining an unnamed one. `CODING_STYLE.md`,
  "Comments are pointers, not essays".
- **A detailed comment is a COPY of a fact, and copies diverge silently.** This
  is the concrete harm, and it is not hypothetical. **The more detail a comment
  lists, the more likely it is to go stale, and the same misinformation then
  spreads through the code base.** A single document can be reviewed and updated
  as understanding changes; *n* copies cannot. Two examples found in one pass:

  | the fact | the copies |
  |---|---|
  | what causes the tail green at IF 1126 | `VideoSourceLine.h` and the host tests both asserted *"it is the source's blanking"* — which `docs/scaler-geometry-model.md` had already **refuted** by measurement, and carries as an open question |
  | the horizontal zoom ceiling | `test_geometry_pads.py` said `1024/500 = 2.048x` against what `test_axis.cpp` asserts, `Scale::Min` having moved |

  One fact, three copies, two wrong, and every test passed — because tests check
  code, and nothing checks prose. A single doc can be reviewed and corrected in
  one place; *n* copies in source cannot, and each one reads as authoritative to
  whoever finds it first. **So the length of a comment is a good predictor of
  how wrong it will be**: put the detail in `docs/`, leave a pointer, and let
  the doc be the thing that gets updated.
- **NEVER quote the user. State the requirement instead.** No name, no date, no
  speech marks — in source, tests, tooling, `docs/`, **and in this file and
  `CODING_STYLE.md` too**. These are the rules for the project, not one person's
  opinions recorded with a byline. Three costs:

  - **An attribution reads as scare quotes.** A rule with a name on it says *"I
    don't agree with this, but I was told to"* — the writer standing apart from
    the rule instead of stating it. Whoever reads it next inherits that doubt
    about a decision that was never in doubt.
  - **It freezes a live requirement** at the moment it was said, so it reads as
    history rather than as the spec it still is.
  - **It gets repeated verbatim rather than updated**, so it outlives the
    requirement it describes — which is how a scale floor of 500 reached four
    files and stayed there after it became 256.

  *"One unit of zoom is one pixel of the output screen"* is the comment. A
  decision worth keeping is recorded as a decision.
- **"Narrative" means anything that only makes sense to someone who was in the
  room.** That is the test — not whether it is long, and not whether it is true.
  Text that means nothing to someone who was not part of the investigation
  fails it: the reader does not want the mis-steps, only the current shape of the
  code and why it is like that. It also ages worse than anything else in the file, because it describes a state
  no reader can check any more. The recurring forms, every one of them found in
  this tree:

  | form | example |
  |---|---|
  | defending a signature | *"injected so a test can hand it a different one"* |
  | defending the default style | *"a class rather than statics in this header"* |
  | counterfactual history | *"used to be maintained from one"*, *"was reverted"* |
  | an unsourced duration | *"it did not look like one for two years"* |
  | a quote from a session | *"can you create a top level folder…"* |
  | the test build, from production source | *"doctest's CHECK binds each operand"* |
  | a path that rots | `test/test_hold_ramp.cpp`, `snapshots/CLEAN-*.json` |
  | a count that rots | *"all 107 decode sites"* |

  **What survives is what the reader cannot derive**: a datasheet contradiction,
  an ordering constraint, a trap with a live consequence, and a "do not
  reinstate this" guard. A failure mode stated in the present tense is not
  narrative — *"writing the divider after the latch leaves the PLL on the old
  value"* is a live warning; *"we wrote it after the latch once and it took an
  evening"* is a diary.
- **A MEASUREMENT BELONGS WITH THE TEST THAT PINS THE BEHAVIOUR, NOT IN THE
  PRODUCTION COMMENT.** Nothing ever forces a production comment to be re-read,
  so it diverges in silence — which is the whole complaint above, and the rule
  that merely asks for shorter comments does not prevent it. A test comment has
  a forcing function: the behaviour it describes stops holding and the test
  fails, so the comment is read exactly when it is wrong, and is then revised or
  deleted with the test. **The suite is what keeps the evidence honest.**

  The codebase already proves it. `scale_step` takes no `scale` argument and a
  test asserts the parameter does not exist, because its existence was the bug —
  a rationale that cannot rot, because deleting it means deleting the guard.

  | the context is | it belongs |
  |---|---|
  | the contract — what it does, its units, its absent case | the declaration |
  | why the behaviour is required, and the measurement that decided it | **the test that pins it** |
  | what was tried, what was refuted, the figures in full | a `docs/` page, cited by name |

  So a constant carries one line of what it is and a pointer; the run counts,
  the shares of samples and the per-mode tables go to the test, and anything
  spanning several behaviours goes to `docs/`.

  **WHERE NO TEST CAN PIN IT, THE EVIDENCE STAYS IN THE COMMENT** — a register
  width, a datasheet contradiction, a hardware-only trap, an ordering constraint
  the host suite cannot observe. Keep it to the line that IS the constraint, and
  say in the commit why there is no test, exactly as an untestable fix does.
  Moving such a fact to a test that does not exercise it is worse than leaving
  it: it reads as covered and is not.
- **A `docs/` page is current state; `docs/investigations/` carries the models
  that were refuted.** Even there, write for someone who was not present: what
  the code does now, why not the obvious alternative, and what going back would
  look like from the outside. A refuted model earns its page only when the wrong
  answer was convincing — `docs/investigations/moving-write-origin.md` is the
  shape, and its point is that a two-term fit matched eleven readings to 0.43 px
  and was still wrong.
- **A HANDOVER IS NEVER COMMITTED.** Not to `docs/investigations/`, not
  anywhere in the tree. A handover is addressed to the next session — where the
  unit is, what is uncommitted, what to do first — and every one of those facts
  is false within a day, so it arrives in review as a commit to delete. Write it
  outside the repo, in `~/Projects/gbsc-pro-handovers/`.

  **An investigation record is the opposite and is welcome.** It is addressed to
  anyone, and it says what was tried, what was measured, and what the outcome
  was — models refuted with the evidence that killed them, a measurement with
  the framing it was taken at, a trap with a live consequence. It carries no
  session state: no working-tree status, no "read this first", no "where the
  unit is", no instruction to the reader about what to do next.

  The test is the tense. A finding is stated in the present and stays true;
  a handover is stated in the imperative and stops being true when the session
  ends. When a session produces both, the findings go in `docs/investigations/`
  and the rest leaves the repo.
- **WORK GOES ON `dev`. `main` IS WHAT HAS BEEN REVIEWED**, and it moves only
  when a review advances it — never as a side effect of doing the work.
  **Check the branch before the first commit of a session**, because a session
  that starts on `main` will happily put a day's work there and destroy the one
  signal the branch carries. Nothing is lost when that happens and the repair is
  pointer movement, as long as `main` is still a pure fast-forward ahead of
  `origin/main`:

  ```sh
  git rev-list --count main..origin/main    # 0, or stop and think
  git merge-base --is-ancestor dev main     # dev must be behind main
  git branch -f dev main && git checkout dev && git branch -f main origin/main
  git merge-base --is-ancestor main dev     # main is now an ancestor of dev
  ```

  No `feature/` or `fix/` branches, and no self-review PRs. The discipline is in
  how commits are split, not in branch topology.
- **`dev` IS PUSHED SO IT CAN BE READ ON GITHUB, and the review is of the
  RESULTING CODE rather than of each commit.** Hundreds of commits are not read
  one at a time; what is reviewed is the engine as it now stands. So a push is
  routine and carries no promise that the history is final — **once the code is
  agreed, the commits are squashed aggressively to remove churn** and the series
  rationalised.
- Commit messages: lowercase area prefix (`tv5725:`, `tools/hwtest:`,
  `build:`), then what changed and *why*, with the evidence. Look at
  `git log` before writing one.
- **Group by FEATURE first, then split that group by kind.** A theme is one
  feature — every change that implemented it — not one file, one layer, or one
  working session. A reviewer reads a feature once; grouping by file makes the
  churn the unit and leaves them rebuilding the feature out of five commits
  because it touched five files. `docs/patch-workflow.md` has the mechanics,
  including which regroupings conflict and why.
- **A commit holds ONE of these four, never a mixture.** It is a readability
  rule: a reviewer reading the engine should not have to step over a prose edit,
  and a tooling change should not hide inside a firmware diff.

  **UPSTREAM CHERRY-PICKABILITY IS NOT A CONSTRAINT.** This fork has diverged
  too far for a commit to be lifted back, so nothing is shaped to keep it
  liftable -- upstream-derived files, the frame time lock among them, are
  refactored like any other, and a fix does not wait on being separable.

  | kind | paths | prefix |
  |---|---|---|
  | firmware | `GBSC-Pro-Source code/**`, `test/**` | `tv5725:`, `build:`, … |
  | Python tooling | `tools/**` | `tools:`, `tools/hwtest:` |
  | project conventions | `CLAUDE.md`, `CODING_STYLE.md` | `project:` |
  | design notes | `docs/**` | `docs:` |

  `test/` travels with the firmware — it is the host-compiled C++ that proves it.
  So one feature is at most four commits, landing as an **adjacent run**:
  firmware first, then its tooling and its docs. The run is still one push and
  one conceptual commit, split only so each kind reads on its own.
  Check before committing:

  ```sh
  git show --name-only --format= HEAD | \
    sed 's#^GBSC-Pro.*#firmware#;s#^test/.*#firmware#;s#^tools/.*#tools#;
         s#^\(CLAUDE\|CODING_STYLE\).md#project#;s#^docs/.*#docs#' | sort -u
  ```

  One line out means one kind, which is the rule. More than one means split it.
- **Load the `tdd` skill before writing code.** Not a formality here: this
  codebase punishes the alternative. An evening went into a preferences bug that
  three sessions "fixed" without a failing test to say what fixed meant, and each
  fix guarded the wrong thing — the open instead of the read, then the read
  instead of the writer. A test that fails for the stated reason first is what
  stops that. Write the test at the cheapest layer that really exercises the
  behaviour, watch it fail, then make it pass.
- **Load the `troubleshooting` skill before diagnosing a fault**, and before
  reaching for a fix that "should" work. The expensive mistake here is a
  hypothesis formed before the instruments are read, and this file is a list of
  them: which vsync sample failed, inferred twice and wrong both times; a dead
  source diagnosed from four invented register addresses while the picture on
  the screen was perfect; four geometry models proposed in one evening and three
  refuted. The instruments are cheap and answer first — the serial console,
  `/geometry`, fields read by NAME, a `tv-snap` photograph, a register diff
  against a known-good. Read them, carry two or three candidates, and pick the
  one experiment that separates them.
- Firmware changes ship as a bounded commit plus a commit adding a runtime
  acceptance test. No source-parsing tests — test behaviour, not implementation.
  No tests for removals.
- **When a fix cannot be tested, say so in the commit and say why.** Several
  reliability fixes here have no acceptance test because their trigger is not
  reachable over HTTP — the preference wipe fires only from the OSD and IR
  handlers, so it has three manual confirmations and no automation. That is an
  acceptable answer; silently shipping untested and letting the next session
  assume coverage is not.
- Tests that disturb the picture or write flash are opt-in flags in
  `conftest.py` (`--source`, `--preset-save`, `--no-sync`, `--pllad-hostile`),
  so a bare `pytest --host=…` stays safe to run on a working unit.
  **`--source` leaves the picture wrong, and a cold boot is the fix** — say so
  before running it if someone is watching the screen. Measured 2026-08-15: the
  pad tests left the framing cropped and panned well off the default, and an
  oversampling test left `PLLAD_MD` 4012 against the 2548 the source wants, so
  the picture is zoomed and softer. Neither is persisted, so an ESP reset
  re-detects and re-solves both — `nix develop -c esptool --port /dev/ttyUSB0
  --after hard_reset --no-stub flash_id`. **There is no framing constant to
  check it against**: `/geometry` reports where the capture window starts and
  how far it runs, and the default is the placement the solve computes for the
  source, not four zeroes. Reset once, note what it lands on, and compare with
  that. The route is behind `GBS_DEBUG`, so a 404 is a build without it.
- The Makefile never shells out to nix; entering the dev shell is the caller's
  job, so the rules work outside NixOS.
