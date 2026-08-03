# GBSC-Pro / RetroScaler

Fork of gbs-control for the GBSC-Pro board: a TV5725 video scaler driven by an
ESP8266, taking analog RGB/YPbPr/composite in and HDMI out. The bench source is a
RISC OS RiscPC at 320x256@50 (VTOTAL 311).

## Layout

| Path | What |
|---|---|
| `GBSC-Pro-Source code/gbs-control/` | the firmware. `gbs-control.ino` is ~19k lines; `framesync.h` is frame time lock; `tv5725.h` has the register map |
| `build/` | `make`-driven arduino-cli build. `data/`, `output/`, `user/` are gitignored and large |
| `tools/gbsc-pro-hwtest/` | Python: pytest suite against a live unit, plus register/geometry/soak tooling |
| `docs/` | TV5725 datasheet and register definitions |
| `GBSC-AV-IR-v1.1-20240923.pdf` | the board schematic (KiCad, 14 sheets) |

## Commands

```sh
nix develop                       # tools: arduino-cli, make, esptool, python3+pytest
make -C build setup               # once: esp8266 core + libs (~440M into build/)
make -C build                     # compile -> build/output/gbs-control.ino.bin
make -C build flash               # upload over USB serial (PORT=/dev/ttyUSB0)
make -C build flash-ota HOST=…    # upload over WiFi, no cable

pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 -v
pytest tools/gbsc-pro-hwtest/ -q  # no --host: hardware tests skip, unit tests run
```

`--source` opts into tests needing a locked signal; `--preset-save` opts into
tests that write flash. Without `--host` everything hardware skips, so a bare
`pytest` stays useful.

## The system has three control domains, and you can only see one

This is the single most expensive thing to not know. An evening was spent
diagnosing "the unit" while able to observe roughly a third of it.

| Domain | Reaches | Visible to you? |
|---|---|---|
| ESP8266 | TV5725 registers, Si5351, STV9426 (0x5D), audio | `/getregs` — **this is all you can see** |
| HC32F460 | `ASW_01`-`ASW_04` analog input routing (pins PB12-PB15), the OLED, the ADV7280 | no — separate MCU, own GPIOs |
| MS9288A | HDMI encoding, EDID, output link | no — on nobody's I²C bus |

- **`ADC_INPUT_SEL` is only half the input path.** It selects which TV5725 ADC
  input is used. Whether the HC32F460 has actually *connected* anything to it is
  `ASW_01`-`ASW_04`, which appear in no register dump. Two muxes in series.
  Picking the input on the OLED is what sets the far one, which is why that
  fixes things a register write cannot.
- **A register dump is not the state of the machine.** `/getregs` reads the
  TV5725 and nothing else.
- **The MS9288A cannot be reset, queried or configured** by anything on the
  board. Only removing power clears it.

## "No HDMI" with every register perfect

Seen four times in one evening. The scaler can be locked, preset loaded, DACs
powered, sync output enabled — and the TV still says no signal. Registers cannot
distinguish these:

1. **The output clock is not running.** `PLL648_CONTROL_01 == 0x75` is a
   *sentinel the firmware wrote* meaning "the Si5351 drives the display", not a
   measurement that it does. Diagnostic: watch the console. If
   `vsyncPeriodAndPhase()` prints its header repeatedly with no `fpsOutput=`
   line following, one of the two vsync samples is failing — and if the input is
   locked, it is the output one. That means no output vsync exists.
2. **The encoder has stopped.** Nothing can see or reset it; power cycle.
3. **The TV timed out** and dropped the input.

## Things that will cost you an hour if you don't know them

- **Check the preferences before diagnosing anything.** A short read of
  `/preferencesv2.txt` silently yields a full set of defaults, and one evening
  produced three separate investigations with this single cause: the custom
  preset "not loading" (`presetPreference` 5 means it was never looked for),
  FrameSync "broken" (`enableFrameTimeLock` 0 means it never ran), and the input
  not applying. Read byte 0 first — 2 is a saved setting, 5 is defaults:
  `spiffs_read(host, "/preferencesv2.txt")`.
- **USB backfeeds power.** A "power cycle" with the USB cable attached does not
  drop the rails, so the MS9288A and HC32F460 never reset. Pull mains *and* USB,
  and wait. Several apparent power-cycle results were nothing of the kind.
- **Cold boot and warm reset are different tests.** The preferences bug is a
  power-up race on the SPI flash — `SPIFFS.begin()` returning true does not mean
  reads work yet. Reflashing tests nothing; only a true cold start does.
- **One WebSocket client at a time.** A second connection crashes the ESP. Close
  the web UI before running anything that opens the console (`mode_watch.py`,
  `soak_watch.py`, the `console` fixture, OTA).
- **SPIFFS access blocks the firmware loop.** `/spiffs/dir` calls `delay(1)` in a
  loop. Hammering it can make the sync watcher see instability. Read-only over
  HTTP is not the same as zero-impact.
- **Opening the serial port resets the board** (DTR/RTS). `stty -F /dev/ttyUSB0
  115200 -hupcl raw -echo` first. The boot ROM prints at 74880 baud.
- **`make -p | grep VAR`** to check what Make really assigned. A bare `#` starts a
  comment mid-assignment, which silently dropped a library commit pin and cost a
  build failure that looked like a library incompatibility.
- **Flashing preserves SPIFFS** (`wipe=none` in the FQBN), so stored timings and
  preferences survive.

## Register facts that are not obvious

- **`STATUS_SYNC_PROC_HTOTAL` echoes `PLLAD_MD`.** The sync processor counts in
  ADC clocks and the ADC PLL is locked to HSync with `PLLAD_MD` as divider, so it
  reports your own setting back. Never key anything on it.
- **`HPERIOD_IF` rails to 0 or 511** when Mode Detect is disturbed and does not
  self-recover; `/uc?h` clears it. `STATUS_IF_HT_OK` reads 1 *even when railed*,
  so it is not a validity signal. Derive line rate from `field_rate x VTOTAL`
  instead.
- **The horizontal axis has no native resolution.** The chip sees sync edges, not
  pixels, so the source's pixel clock is unknowable and 320x256 and 640x256 are
  indistinguishable. Capture is in ADC sample units, and how many there are per
  line is your choice (`PLLAD_MD`).
- **Blanking cannot be auto-detected.** A border is black *active* video,
  electrically identical to back porch. Sync-domain measurement finds the raster,
  never the picture inside it.
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

## Working with the unit

Snapshot before changing anything, diff after:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> --out snapshots/before.json
python3 tools/gbsc-pro-hwtest/snapdiff.py --diff snapshots/before.json snapshots/after.json
```

`snapshots/` holds known-good states. To recover a working picture:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore snapshots/dis-hb-st-tweak-2026-08-03.dump.json --segments 1,3,4,5 --repeat 2
```

The two snapshot formats are not interchangeable: `dump_registers.py` writes 496
config registers, `snapdiff.py` writes all 1536. Diff like against like.

`geometry.py --host <ip>` prints the input side, output side, and where the three
horizontal extents disagree — the fastest read on why a picture is wrong.

## Conventions

- Commit messages: lowercase area prefix (`tools/hwtest:`, `build:`,
  `framesync:`), then what changed and *why*, with the evidence. Look at
  `git log` before writing one.
- Firmware changes ship as a bounded commit plus a commit adding a runtime
  acceptance test. No source-parsing tests — test behaviour, not implementation.
  No tests for removals.
- The Makefile never shells out to nix; entering the dev shell is the caller's
  job, so the rules work outside NixOS.
