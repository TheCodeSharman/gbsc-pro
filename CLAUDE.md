# GBSC-Pro / RetroScaler

Fork of gbs-control for the GBSC-Pro board: a TV5725 video scaler driven by an
ESP8266, taking analog RGB/YPbPr/composite in and HDMI out. The bench source is a
RISC OS RiscPC at 320x256@50 (VTOTAL 311).

## Layout

| Path | What |
|---|---|
| `GBSC-Pro-Source code/gbs-control/` | the firmware. `gbs-control.ino` is ~19k lines; `framesync.h` is frame time lock; the register map is declared by the subsystem that owns each block, under `src/tv5725/`, with whatever has no owner yet left in `Tv5725::Tv5725` |
| `build/` | `make`-driven arduino-cli build. `data/`, `output/`, `user/` are gitignored and large |
| `tools/gbsc-pro-hwtest/` | Python: pytest suite against a live unit, plus register/geometry/soak tooling |
| `docs/` | **`chip-initialisation.md` is the design — code first, one class per subsystem in `Tv5725::`, and why the preset blobs are being deleted rather than tidied. Read it before adding a register write. `testing.md` is which test layer to use, the fake-Wire seam that makes firmware register code host-testable, and the poison/mutation disciplines.** TV5725 datasheet and register definitions; `scaler-geometry-model.md` is the measured arithmetic from capture window to output blanking registers — **read it before touching geometry**; `firmware-geometry-engine.md` is how `src/tv5725/` uses it and the rules that keep it correct; `vesa-gtf.md` settles the capture-window default — select PAL or NTSC on field rate, no curve — and records why GTF was rejected. Read before proposing a blanking formula. `capture-limits.md` is the two bounds on what arrives intact — the horizontal write limit and the vertical bypass threshold — and the trade `PLLAD_MD` makes between sampling density and reaching the end of the line; `rgbhv-bypass-trap.md` explains why a >535-line RGBHV source is never scaled, and which measurements bypass invalidates; `osd-menu.md` is the menu the remote drives — read it before touching anything called OSD, because three subsystems answer to that name and only the STV9426 reaches the television; `sync-type-selection.md` is why the csync/separate-sync choice is circular and latches — read it before touching `syncTypeCsync` or believing `VSACT`; `preset-load-clobber.md` is what to read before rewriting preset loading; `whole-byte-convenience-names.md` is the inventory and order for removing the 25 non-datasheet byte-wide names; `preset-gap-datasheet-map.md` is every field the preset still owns, resolved against RD-5725-1.1. `MS9288A-Datasheet-Rev-B0.pdf` is the HDMI encoder's datasheet, in Chinese — pinout, application circuit and electrical characteristics, and **no register map**, which Macrosilicon release under NDA only; `EM638325-Industrial_Rev-3.2.pdf` is the **SDRAM part's** datasheet — the frame buffer is one EM638325TS-6, a 166 MHz bin, and it is what bounds the memory clock; `webui-build-chain.md` is the four-file UI chain, three of them checked-in artefacts — read it before editing anything under `public/`; `ota-flashing.md` is the espota handshake, why it needs an inbound port on the host, and how to tell an unarmed unit from a blocked one |
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

pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 -v
pytest tools/gbsc-pro-hwtest/ -q  # no --host: hardware tests skip, unit tests run

ruff check tools                  # BEFORE any hardware run
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
printf 'PATTERN PM5544\n'          | nc 192.168.88.10 6502   # redraw, or pick the plainer CARD
printf 'MODES\n'                   | nc 192.168.88.10 6502   # what this monitor definition allows
printf 'PING\n'                    | nc 192.168.88.10 6502   # OK ModeServ 1
```

`MODE` replies with the mode read back from the hardware, never with the
request, so a monitor definition that cannot do what was asked does not look
like a fault in the scaler. **It repaints the card too**, because the default
signal after a mode change is black with a flashing cursor, which reads from
here as a scaler with no output and has been diagnosed as one. **A session can therefore change the source without
anyone at the bench**, which is what makes the mode-change recovery below usable
unattended.

`--source` opts into tests needing a locked signal; `--preset-save` opts into
tests that write flash. Without `--host` everything hardware skips, so a bare
`pytest` stays useful.

### Flashing

**OTA is the everyday path and works.**

```sh
make -C build flash-ota HOST=192.168.88.108     # arms the unit itself
python3 tools/gbsc-pro-hwtest/ota_probe.py --host <ip>   # when an upload hangs
```

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
- **The ESP commands the HC32 over its UART**, ESP TX → HC32 `USART4` RX (PB7),
  115200 8N1. 7-byte frame: `41 44 <cmd> <arg> <val|nonce> FE <sum of bytes 0-5>`.
  `'S'` selects input: `0x4n` RGBs, `0x5n` RGsB, `0x6n` VGA, `0x70` YPbPr,
  `0x1n` S-Video, `0x2n` composite; `0xA0`/`0xA1` toggle `asw_02`. There is **no
  readback** — the `'I'` INFO handler is commented out.
- **The OLED menu is on the ESP**, not the HC32. Picking an input there works
  because that handler transmits the frame above.
- **`ADC_INPUT_SEL` is only half the input path.** It selects which TV5725 ADC
  input is used. Whether the HC32F460 has actually *connected* anything to it is
  `ASW_01`-`ASW_04`, which appear in no register dump. Two muxes in series.
  **VGA is the only input that raises `asw_01`** (the schematic's
  `ASW_01 = 0, HS_IN = SOGIN` — it selects the dedicated HSync pin over
  sync-on-green), and the only one raising `asw_01` and `asw_04` together.
- **Both MCUs persist "which input", separately, and neither can read the other
  back.** The ESP keeps `SeleInputSource` in `/preferencesv2.txt`; the HC32 keeps
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
- **AV module v1.3 changes only the ADV7280/ADV7391 composite path** (525p vs
  625p encoder config). `uart_dma.c` and `flash.c` are byte-identical to v1.2.3.
  It cannot affect RGB/VGA routing — don't reach for it to fix a VGA fault.
- **A register dump is not the state of the machine.** `/getregs` reads the
  TV5725 and nothing else.
- **The MS9288A cannot be reset, queried or configured** by anything on the
  board. Only removing power clears it. Its I²C slave pins are wired to
  nothing — SDA (29) and SCL (28) carry no-connect flags on schematic sheet
  4/13 — so an NDA register map would still not reach it, and neither MCU's
  firmware addresses it: the HC32 drives one I²C device (0x42, the ADV7280)
  and the ESP drives the TV5725, Si5351, STV9426, PT2257 and the EEPROM. The
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
   re-acquire, and measured 2026-08-20 the picture comes back at once with no
   power cycle. Entering RGBHV bypass does the same the long way round. The
   firmware has this as `useHdmiSyncFix`, armed only where the input
   classification swaps inside the SD 50/60 families — so a change between an SD
   source and a VGA-class one never gets it. `docs/investigations/encoder-stale-timing.md`.

   A power cycle — mains *and* USB, since USB backfeeds the rails — remains the
   fallback for a state the toggle does not clear.
3. **The TV timed out** and dropped the input.

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
- **A preset load turns a 15 kHz RGBHV source PROGRESSIVE, and the picture
  survives it.** Measured: one `/sc?)` on the bench RiscPC moves
  `IF_PRGRSV_CNTRL` 0 -> 1, `IF_LD_RAM_BYPS` 0 -> 1, `IF_HS_DEC_FACTOR` 1 -> 0,
  `PLLAD_MD` 2250 -> 1124, and `SP_VTOTAL` collapses from 311 to noise. The
  chain: `preferScalingRgbhv` defaults to 1, `runSyncWatcher()` sets
  `isValidForScalingRGBHV` for an RGBHV source of 535 lines or fewer, and
  `PresetLoad::videoStandardInputAfterLoad()` then forces `videoStandardInput`
  to `ScalingRgbhvStandard`, which is **3** -- and `doPostPresetLoadSteps()`
  branches `3 || 4 || 8 || 9` straight into `applyScanMode(Progressive)`. So a
  source that qualifies for scaling RGBHV and also needs the line doubler gets
  the wrong one, because one number carries both facts. The divider that follows
  is arithmetically right for the wrong premise, so every register reads
  self-consistent. `pytest test_geometry_pads.py --source` reaches it in about
  four and a half minutes and is the reproduction.
- **THE TWO RECOVERIES ARE NOT INTERCHANGEABLE, and each fails at the other's
  fault.** Measured, both directions:

  | fault | cleared by | does NOT clear it |
  |---|---|---|
  | railed `HPERIOD_IF`, sync processor fine | a source mode change | `/sc?~`, every `SFTRST_*_RSTZ`, the analog bias resets, one cold boot |
  | divider stuck on another mode's value | `/sc?~` | a mode-change round trip |

  So reach for the one that matches. A stuck `PLLAD_MD` survived 311 -> 524 ->
  311 unchanged at 1822 and the picture rolled; `/sc?~` restored 2250 at once.
  **Judge the divider against the source**, not against whether it moved.

  **The first row is contradicted on both counts by a 2026-08-25 measurement, so
  treat neither recovery as reliable for it.** After an OTA reflash `HPERIOD_IF`
  read 14/255/271/274/508/510/511 against the 431 the mode was due, with
  `SP_VTOTAL` a rock-steady 311 beside it. A source mode change -- 320x256@50 to
  800x600@60 and back, the recovery that row names -- did **not** clear it. A
  cold boot with mains and USB both pulled, which that row lists as not clearing
  it, restored a steady 431 at once.

  **And it does not necessarily reach the picture.** Throughout the noisy state
  the panel showed a correct full-screen picture, and all 1536 config registers
  were byte-identical to a known-good reference. Nothing in a register dump
  distinguishes the two, so `HPERIOD_IF` disagreeing with the mode is a reason to
  look, not a fault to chase on its own -- the geometry engine does not read it.
- **Check `HPERIOD_IF` against the value the MODE should give**, which is
  `27e6 / (4 x lineRateHz) - 1` -- 431 at 311 lines/50 Hz, 213 at 524/60, 214 at
  448/70. Steady is not valid: a steady **50** was measured at 640x480@60 where
  213 was due, and every check that tests stability alone passes it.
  `docs/investigations/hperiod-if-railing.md` has the table.
- **Check the preferences before diagnosing anything.** A short read of
  `/preferencesv2.txt` silently yields a full set of defaults, and one evening
  produced three separate investigations with this single cause: the custom
  preset "not loading" (`presetPreference` 5 means it was never looked for),
  FrameSync "broken" (`enableFrameTimeLock` 0 means it never ran), and the input
  not applying. Read byte 0 first — 2 is a saved setting, 5 is defaults:
  `fs_read(host, "/preferencesv2.txt")`.
- **USB backfeeds power.** A "power cycle" with the USB cable attached does not
  drop the rails, so the MS9288A and HC32F460 never reset. Pull mains *and* USB,
  and wait. Several apparent power-cycle results were nothing of the kind.
- **Cold boot and warm reset are different tests.** The preferences bug is a
  power-up race on the SPI flash — `LittleFS.begin()` returning true does not mean
  reads work yet. Reflashing tests nothing; only a true cold start does.
- **A connected-but-silent console means the heap gate is shut, not that the
  firmware is quiet.** `SerialMirror` only calls `broadcastTXT` when free heap
  clears `CONSOLE_BROADCAST_MIN_HEAP`. That was **20000**, which this fork can
  never reach: globals take 47584 bytes of 81920, and once WiFi, the async server
  and the websocket server have taken theirs, `/bootlog` measures ~18 KB free
  **at boot**. The socket then accepts clients and delivers nothing for the whole
  session — the web UI loads its shell and sits on the splash with the red
  disconnected indicator. Lowered to 8000 on 2026-08-05; measured 18120 free at
  boot afterwards, console delivering. **Read `/bootlog`'s `free heap:` line
  before believing a silent console.** Guarded by
  `test_the_console_delivers_anything_at_all`.
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
  one of detection's long searches — the 6000 ms `getVideoMode()` sweeps in
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
  `fsDebugPrintf` (`framesync.h`), so it silences `no INPUT vsync`,
  `no OUTPUT vsync` and every `runFrequency()` reason — precisely the messages
  the "no HDMI with every register perfect" section tells you to read. It buys
  1068 bytes of globals (46516 vs 47584) and costs the diagnosis. Use
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
- **The `framesync.h` hang is fixed — do not diagnose with it.**
  `sampleVsyncPeriod()` used to spin 3,000,000 passes with `ESP.wdtDisable()`,
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

  `PLLAD_MD`, `IF_HSYNC_RST` (= `MD`/2) and `SP_RT_HS_SP` (= 93% of `MD`, the
  sync processor's retime window) are **ONE quantity in THREE registers**, and
  `Tv5725::SourceMeasurement` owns all three off one held value. It is *state*,
  handed to `Tv5725::CaptureWindow` rather than read back — the same rule
  `CaptureWindow::ProgressiveStart` already carried. Verified across all twelve shipped
  tables: `IF_HSYNC_RST == PLLAD_MD/2` without exception, while `SP_RT_HS_SP` is
  wrong in five of them (`ntsc_1280x720` ships **68** against 2180) and is only
  saved by the runtime write.

  **`STATUS_SYNC_PROC_HTOTAL` is the only thing on the board that can see a
  missing latch**, because it counts real ADC clocks per line and so reports the
  *latched* divider. Locked, it equals `PLLAD_MD` — 2553 against 2553, with an
  occasional ±1. **Unlocked it is noise that looks like a small latch error**: it
  held a steady 2558 against 2553 across 22 samples while `SP_VTOTAL` sat at
  97/98. Steady is not valid; ask whether `SP_VTOTAL` is counting first. And it
  reads wrong for a third reason too — an *unconfigured sync processor*. With
  `SP_PRE_COAST`/`SP_POST_COAST`/`SP_DLT_REG` zeroed it read 2400 and did not
  move when 2553 was written and latched by hand.
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
  and asks whether a V sync line actually arrives. It costs ~500 ms, so
  `rto->syncTypeIsSet` runs it once per SOURCE rather than once per mode change.
  `docs/sync-type-selection.md`.
- **`HPERIOD_IF` going bad is three different faults, and BYPASS IS NOT ONE OF
  THEM — establish the path first.** With the IF out of the path the register
  measures nothing, and it does *not* only sit at a stable `0`: measured in
  bypass it ran 255, 511, 511, 275, 258, 511 while the sync processor stayed
  perfect beside it, which is indistinguishable from the railing fault and has
  twice been diagnosed as one. `DAC_RGBS_ADC2DAC` and `OUT_SYNC_SEL` are 1 in
  bypass and 0 on the scaling path; the scale registers are NOT the tell, since
  bypass leaves `VDS_HSCALE`/`VDS_VSCALE` on the last scaled load's values.
  `docs/rgbhv-bypass-trap.md`. On the scaling path, noisy multi-valued garbage
  is the second fault. The third is
  the dangerous one: **a single stable value that is simply wrong** (192 where
  212 was due), which every health check ever written here scores as healthy
  because it is stable and nowhere near a rail. **Validate against the expected
  value for the mode, not against `0`/`511`.** `STATUS_IF_HT_OK` reads 1 even
  when railed, so it is not a validity signal either. `docs/tv5725-chip.md`.
- **What predicts the fault is the mode you land in, not what happened before.**
  Over 195 transitions the failure rate by destination runs 44% (VTOTAL 524), 28%
  (363), 24% (533) and 0-4% for everything else — and the wrong values repeat:
  524 latches `50`, while 311 and 261 both latch `350`. A preceding deep sync
  loss raises the odds (28% vs 3% after a clean change) but **is not a
  discriminator** — an earlier "0 of 32 without a deep sync loss", drawn from 42
  transitions, does not survive the full sample and sent two sessions after a
  test that does not exist. To reproduce, park the source in VTOTAL 524.
  A one-sample `97`/`98` blip mid-change is normal; `SP_VTOTAL` *steady* at a
  non-mode value is the fault.
- **Judge only settled samples.** Discard ~6 s after any mode change. Raw
  sampling across a sweep throws garbage at nearly every change that resolves on
  its own; scoring those produced 15 false positives in one run.
- **Preset loads leave things behind**, and the symptom lands somewhere
  unrelated. A yellow-tinted picture is `DAC_RGBS_B0ENZ` (s0 `0x45` bit 0)
  cleared by a bulk table load that never got patched back — the firmware never
  writes that bit to 0 anywhere. Fix: `curl '…/setreg?s=0&r=0x45&v=0x11'`.
  Same mechanism is the leading explanation for the `HPERIOD_IF` failures.
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
  Output480P` — a persistent user preference — and force-calls `applyPresets()`,
  falling back to the remembered standard when `getVideoMode()` returns 0. It
  appears to "fix" railing by reloading a preset. The actual Mode Detect reset is
  `resetModeDetect()` (`SFTRST_MODE_RSTZ`, s0 `0x47` bit 1), reachable via
  `/setreg` — but it does **not** recover a bypassed IF, and nothing in the
  firmware resets Mode Detect while sync is present.
- **An RGBHV source over 535 lines is trapped in bypass** and is never scaled —
  deterministic, re-armed on every boot. The bench 800x600 (VTOTAL 627) hits it.
  It still gives a picture; the cost is scaling. `docs/rgbhv-bypass-trap.md`.
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
- **Don't pin the picture to a panel edge.** Where a display stops showing is a
  property of the display, so `geometry_math` centres on the raster instead and
  the user finds their own edges with pan and scale. The vertical visible region
  is derivable — `1121 - 41 = 1080` exactly, the encoder's active window, same on
  every display — while the horizontal is real overscan and is not.
- **The engine CALCULATES every register from held state. Registers are an
  output and are never an input.** The only reads permitted are measurements of
  the source — the `STATUS_SYNC_PROC_*` registers, which nothing else can
  supply. Everything else the engine needs, it already knows, because it
  computed it: the raster, both scales, both windows, the divider. Reading one
  back to derive another is what most of the geometry bugs here were.

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
  **`scaleMin` is now DERIVED — `Scale::Unity / maxMagnification`,
  both axes at 4.0x** — so the floor is `raster / 4` (479 at 1916, 530 units of
  travel) and it no longer collapses each time the output widens.

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
- **Nothing is captured past IF 1125.** The capture path stops writing video
  there and writes `Y=U=V=0`, which decodes to green and **destroys** active
  picture that reaches it — it is a bound on usable width, not an artefact to
  hide. The position is absolute in the line and unmoved by the capture start,
  the source's timings or the memory clock; what counts to 2250 ADC samples is
  unknown. `InputLine::WriteLimitUnits` is 1125 IF units — 2250 ADC samples —
  and `SourceMeasurement` caps `PLLAD_MD` there so the whole line arrives, which
  is why the bench now runs a 1126-unit IF line rather than 1277, and
  `InputLine::lastCapture()` clamps the lines the divider did not choose. Do not
  read the divider cap as the tearing ceiling that was removed — that one stays
  refuted. `docs/capture-limits.md`.
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
  sync width. The exception nobody can settle from the values alone is
  `SP_CS_CLP_ST`/`SP` (26 and 150), small enough to be either — and misplaced
  under both readings, landing inside the hsync pulse rather than the back
  porch. `docs/scaler-geometry-model.md`.
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

  **But its original justification is GONE, and nobody has re-tested the
  alternative.** It read: at 129.6 MHz the raster is 2298, the zoom floor lands
  exactly on the default framing, and horizontal zoom-in has no travel. That was
  true at `scaleMin` 500. With the floor now `raster / 4` it is `575` against an
  890 default — **315 units of travel, not zero** — so the usability argument
  for 108 over 129.6 no longer holds on its own terms. 129.6 MHz would buy a
  third more horizontal resolution and is already measured as working and sharp.
  Raising `EngineCeilingHz` is now a live bench experiment
  rather than a settled no; it has not been tried, so do not assume it works.

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
- checking RGBHV bypass by hand-written slice gave `DAC_RGBS_ADC2DAC` 0 and
  `OUT_SYNC_SEL` 0 on a unit correctly *in* bypass with a full-screen picture,
  which reads exactly like the "no raster at all" failure. By name: 1 and 1.

**If a status read disagrees with what the screen is doing, check how you
addressed it before believing the registers.**

`tools/gbsc-pro-hwtest/tv5725_registers.json` is the map behind all of these, and
the same one `snapdiff.py` and `setfield.py` decode with. `setfield.py --set
NAME=VALUE` writes by name, read-modify-write, so a field sharing a byte with its
neighbour does not destroy it. `dump_registers.py` reads everything in one pass.

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
- **An open window past the end of the picture is not corruption.** It shows
  whatever the playback stage fetches, which looks like a fault and is only
  absence of data. Establish that there IS picture under the window before
  reading anything as a floor — force the magnification if need be.
- **A MAPPING FROM PHOTO COLUMNS TO OUTPUT PIXELS DOES NOT SURVIVE AN OUTPUT MODE
  CHANGE.** Measured 57 columns adrift — a fifth of the picture — after a
  1080p/960p/1080p round trip with the raster registers identical either side:
  the encoder re-acquires and where it puts the picture on the panel is its
  choice. Calibrate by differencing a frame at one `VDS_DIS_?B_ST` against
  frames at others, so the difference IS the strip the register blanked, and
  **re-calibrate after anything that re-locks the encoder**. Read against a stale
  mapping, a correct far edge reads as 110 px of overshoot and the line's repeat
  reads as the picture.
- **Separate what the board must emit from what one display happens to show.**
  The MS9288A consumes the scaler's analog blanking and generates HDMI blanking
  of its own, so the minimum the scaler must emit is a board property, measured
  once, portable. Where a set stops painting is that set's overscan. Sizing a
  constant from the second reads as a fix and ships one panel's number.
- **BYPASS IS THE ONLY REFERENCE FOR WHERE THE PANEL'S PICTURE ENDS.** Nothing on
  the scaling path can supply it, because whatever the scaler blanks reads as
  bezel from the far end. An 800x600 source is over the 535-line threshold, so it
  is forced into RGBHV bypass and passes straight through filling the screen, and
  its extent IS the panel's painted area:
  `printf 'MODE X800 Y600 C256 F60\n' | nc 192.168.88.10 6502`. Take it at the
  same camera position as the frame being judged and compare the two.
- **PUT COLOUR AGAINST THE EDGE BEFORE MEASURING ONE.** At a default framing the
  last thing on screen is captured INPUT blanking, so "where the picture ends"
  measures the source's border and not the output's edge — a comparison against
  bypass is out by the whole border. Zoom in and pan until content is hard against
  the edge under test. Panning `-` moves the capture left and puts the image
  against the RIGHT edge; `+` exposes the source's blanking there instead.
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
- **An artefact that leaves the screen is hidden, not cured**, and which one it
  is decides whether it travels. Move the thing that generates it back onto the
  panel and re-measure before concluding anything: shift the picture back by the
  same amount the change moved it, then compare like with like.

## Conventions

**`CODING_STYLE.md` is the C++ style, and it is not optional.** Classes rather
than namespaces over file-scope globals, one class per file named after it,
declare-in-header/define-in-.cpp, minimal OO with no inheritance or virtuals,
dependency injection over reaching for globals, a default of no comment at all,
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
  | what causes the tail green at IF 1126 | `InputLine.h` and the host tests both asserted *"it is the source's blanking"* — which `docs/scaler-geometry-model.md` had already **refuted** by measurement, and carries as an open question |
  | the horizontal zoom ceiling | `test_geometry_pads.py` said `1024/500 = 2.048x` against the **4.0x** `test_axis.cpp` asserts, `Scale::Min` having become derived |

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

  **What survives is what the reader cannot derive**: a measurement, a datasheet
  contradiction, an ordering constraint, a trap with a live consequence, and a
  "do not reinstate this" guard. A failure mode stated in the present tense is
  not narrative — *"writing the divider after the latch leaves the PLL on the old
  value"* is a live warning; *"we wrote it after the latch once and it took an
  evening"* is a diary.
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
- Commit messages: lowercase area prefix (`tools/hwtest:`, `build:`,
  `framesync:`), then what changed and *why*, with the evidence. Look at
  `git log` before writing one.
- **Group by FEATURE first, then split that group by kind.** A theme is one
  feature — every change that implemented it — not one file, one layer, or one
  working session. A reviewer reads a feature once; grouping by file makes the
  churn the unit and leaves them rebuilding the feature out of five commits
  because it touched five files. `docs/patch-workflow.md` has the mechanics,
  including which regroupings conflict and why.
- **A commit holds ONE of these four, never a mixture.** The firmware is a fork
  and its commits have to be cherry-pickable upstream on their own; a commit that
  also edits our Python or our conventions cannot be taken without them.

  | kind | paths | prefix |
  |---|---|---|
  | firmware | `GBSC-Pro-Source code/**`, `test/**` | `tv5725:`, `framesync:`, … |
  | Python tooling | `tools/**` | `tools:`, `tools/hwtest:` |
  | project conventions | `CLAUDE.md`, `CODING_STYLE.md` | `project:` |
  | design notes | `docs/**` | `docs:` |

  `test/` travels with the firmware — it is the host-compiled C++ that proves it.
  So one feature is at most four commits, landing as an **adjacent run**:
  firmware first, then its tooling and its docs. The run is still one push, and
  it is one conceptual commit split only as far as cherry-pickability demands.
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
