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

## Things that will cost you an hour if you don't know them

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
