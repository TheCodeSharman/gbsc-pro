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
| `docs/` | TV5725 datasheet and register definitions; `scaler-geometry-model.md` is the measured arithmetic from capture window to output blanking registers — **read it before touching geometry**; `firmware-geometry-engine.md` is how `src/tv5725/` uses it and the rules that keep it correct; `vesa-gtf.md` settles the capture-window default — select PAL or NTSC on field rate, no curve — and records why GTF was rejected. Read before proposing a blanking formula. `rgbhv-bypass-trap.md` explains why a >535-line RGBHV source is never scaled; `preset-load-clobber.md` is what to read before rewriting preset loading; `webui-build-chain.md` is the four-file UI chain, three of them checked-in artefacts — read it before editing anything under `public/` |
| `GBSC-AV-IR-v1.1-20240923.pdf` | the board schematic (KiCad, 14 sheets) |

## Commands

```sh
nix develop                       # tools: arduino-cli, make, esptool, python3+pytest
make -C build setup               # once: esp8266 core + libs (~440M into build/)
make -C build                     # compile -> build/output/gbs-control.ino.bin
make -C build flash               # upload over USB serial (PORT=/dev/ttyUSB0)
make -C build flash-ota HOST=…    # upload over WiFi — only after arming, see below

pytest tools/gbsc-pro-hwtest/ --host=192.168.88.108 -v
pytest tools/gbsc-pro-hwtest/ -q  # no --host: hardware tests skip, unit tests run
```

`--source` opts into tests needing a locked signal; `--preset-save` opts into
tests that write flash. Without `--host` everything hardware skips, so a bare
`pytest` stays useful.

### Flashing

**USB is the reliable path, and usually the only one available.**

```sh
ls /dev/ttyUSB*                          # CH340 on this board; enumerate before flashing
make -C build flash                      # PORT=/dev/ttyUSB0 by default
make -C build flash PORT=/dev/ttyUSB1
```

**OTA has never worked on this unit. Do not try it.** Measured 2026-08-05:
`curl 'http://<ip>/sc?c'` returns **200**
and port **8266 never opens**, so `make -C build flash-ota` has nothing to talk
to. The mechanism is all there in the source — `rto->allowUpdatesOTA` defaults to
false (`gbs-control.ino:7630`), `/sc?c` sets it and calls `initUpdateOTA()`,
`ArduinoOTA.handle()` runs only when true — which makes it look like an arming
problem you can solve. It is not; it has never once produced a working upload.

USB is the only route. The `build/Makefile` comment claiming "no cable needed" is
wrong, and the `flash-ota` target is kept only because removing it would invite
the next session to reinvent it.

**A 200 from `/sc?c` is not evidence of anything**, which is the trap: see the
async-server note below for why HTTP answers even when the firmware loop is not
running.

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
- **Both MCUs persist "which input", separately, and never reconcile.** The ESP
  keeps `SeleInputSource` in `/preferencesv2.txt`; the HC32 keeps `asw_01..04` in
  its own flash and restores them via `Video_ReadNot2()`. **Nothing sends a frame
  at boot**, so a cold start can come up with the two disagreeing — which is what
  picking the input on the OLED repairs.
- **AV module v1.3 changes only the ADV7280/ADV7391 composite path** (525p vs
  625p encoder config). `uart_dma.c` and `flash.c` are byte-identical to v1.2.3.
  It cannot affect RGB/VGA routing — don't reach for it to fix a VGA fault.
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
   measurement that it does. Diagnostic: watch the console. **Do not infer which
   vsync sample failed — the firmware now says.** `vsyncPeriodAndPhase()` prints
   `no INPUT vsync` or `no OUTPUT vsync`, and `runFrequency()` names its failing
   check. A stream of bare headers with no outcome means a pre-`f5bb2b0` build.
   *"The input is locked, so it must be the output sample"* was inferred twice
   and is wrong: measured on the bench unit with the source locked and the TV
   dark, it is the **input** sample that times out — a measurement-path fault at
   `DEBUG_IN_PIN`, not a video-path one. And because `runFrequency()` returns
   early, the Si5351 never gets adjusted at all.
2. **The encoder has stopped.** Nothing can see or reset it; power cycle — mains
   *and* USB, since USB backfeeds the rails. This is the likeliest cause after a
   session with heavy mode switching: every bypass↔scaling transition retimes the
   output and forces the MS9288A to re-lock, so the wedge looks dose-dependent
   rather than random. A mode sweep is dozens of re-locks, and the bill arrives
   later as a blank screen.
3. **The TV timed out** and dropped the input.

**`VDS_ENABLE == 0` is not evidence of no output.** In RGBHV bypass the video
path does not go through the VDS at all, so an empty segment 2/3 is expected and
the unit is still driving the encoder. Reading it as "nothing is being sent" is a
mistake that has been made and cost a wrong diagnosis — bypass produces a working
800x600 picture. See `docs/rgbhv-bypass-trap.md`.

## Things that will cost you an hour if you don't know them

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
  in `d3a4426`. Still worth closing the web UI before a long capture, because it
  costs heap, not because two clients are forbidden.
- **HTTP answering does not mean the firmware is running.** The web server is
  `ESPAsyncWebServer` (`gbs-control.ino:544`), which serves from network-stack
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
- **Check for stray tooling before diagnosing anything.** On 2026-08-05 a
  `soak_watch.py --interval 5` had been polling for **2 days 22 hours** and a
  `regpanel.py` for **1 day 23 hours**, both left from earlier sessions, leaving
  hundreds of sockets in `TIME-WAIT` and starving the websocket server (which
  caps at 5 clients). An evening went into "the websocket is wedged" with that
  running underneath. One command:
  `ps -eo pid,etime,cmd | grep -E 'soak_watch|regpanel|sweeplog'`, and
  `ss -tanp | grep <ip>` for what is actually connected.
- **Never flash `GBS_DEBUG=0` while diagnosing.** It is the flag gating
  `fsDebugPrintf` (`framesync.h:28`), so it silences `no INPUT vsync`,
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

## The datasheet contradicts itself, and `tv5725.h` is the survivor

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
- **Where `tv5725.h` and the datasheet disagree, the header wins.** Its values are
  that audit's output plus bench proof. Eleven such fields remain, listed by name
  in `tools/tv5725-header/test_fielddocs.py::HEADER_WINS`, and a *new*
  disagreement fails that test rather than joining a tolerated count.
- **Assert known widths before believing any extraction.** `VDS_HSCALE` 10,
  `PLLAD_MD` 12, `IF_HB_ST1` 11, `MEM_MODE_REG` 16. Two attempts at re-deriving
  the field set on 2026-08-13 both produced complete-looking tables that failed
  exactly there — one keyed the parse by name so later slices of a wide field
  overwrote earlier ones, one read `(hi, lo)` as `(lo, hi)`. Both looked fine.
- **The danger is one-directional.** A field declared *narrower* than it is
  truncates every value written through it and says nothing.
- **`merged.json` is the artefact, not `fielddocs.parse()` output.** Parsing must
  use `keep_slices=True`, or the `[9:8]` suffixes are stripped and every slice of
  a wide field collides on one dict key.
- **The extractor invents names if you let it.** On 2026-08-13 it produced 32 —
  `ALUE`, `EG0`, `FFSET`, `R_B` — from names the PDF wraps across two lines, and
  they reached `tv5725.h` looking like real registers. Every genuine field name
  contains an underscore; the six in the document that do not are all fragments.

## What a preset actually writes

`preset_common.py` measures it, `test_preset_common.py` pins it. Of the 432
registers one `writeProgramArrayNew()` call writes:

- **306 are identical in all twelve scaling tables** — static bring-up wearing a
  preset's clothes — and 126 differ somewhere.
- **No read-only register is written.** All 16 RO registers are `s0_00..s0_2e`
  and the preset ranges start at `s0_40`. There is no overlap.
- **73 registers are written `0x00` by every table** because the ranges overrun
  the documented register set — the datasheet stops at `s4_5b`, `s5_63`, `s0_98`
  and the preset writes past all three. Nobody chose those; they are padding.
- Counting only datasheet-attested names, 188 of the 432 are fully named, 147
  partly, 97 not at all. Counting gbs-control's own convenience typedefs too it
  is 229/136/67 — **that is the number not to quote**, because a whole-byte view
  like `STATUS_00` reads no better than a raw byte.

## Register facts that are not obvious

- **`STATUS_SYNC_PROC_HTOTAL` echoes `PLLAD_MD`.** The sync processor counts in
  ADC clocks and the ADC PLL is locked to HSync with `PLLAD_MD` as divider, so it
  reports your own setting back. Never key anything on it.
- **`HPERIOD_IF` going bad is three different faults**, and the old note here
  merged them. A stable `0` means the IF is out of the path (RGBHV bypass —
  expected, not a fault). Noisy multi-valued garbage is the second. The third is
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
- **Compute the geometry, never inherit it.** Read the capture and the raster;
  derive everything else. Every geometry fault of 2026-08-06 was a violation:
  inheriting the corner put 41 px of the previous frame down the left of the
  screen, and inheriting the picture size froze a picture at 620 lines that no
  zoom step could grow. `scale_step` deliberately takes no `scale` argument —
  there is a test asserting the parameter does not exist, because its existence
  was the bug. Every pad press recomputes every window, pan included.
- **The output raster is computed too, since 2026-08-13.**
  `Engine::solveRaster()` derives both totals, both sync pulses and the display
  clock seed from the frame height and the measured field rate, so a preset
  table's raster bytes are overwritten on every mode change. Measured
  1436 x 1126 @ 80.85 MHz before, **1915 x 1126 @ 107.81 MHz after** — a third
  more horizontal resolution. Two rules come with it. It runs from
  `applyPresets()` **before** `externalClockGenResetClock()`, because that reads
  back the seed it writes; the order is raster → clock → windows → rate steer
  last, and running the steer early gave a 31 Hz frame and a black screen.
  And **a wider raster costs zoom range**: `AxisH` magnifies at most 2.048x, so
  the capture cannot go below `ceil(raster x 500 / 1024)`. At the 2298 the
  129.6 MHz ceiling gives, that floor is 1123 and the *default* framing sits on
  it — horizontal zoom-in dead, measured. Hence
  `OutputRaster::EngineCeilingHz` is 108 MHz while `WorkingCeilingHz` stays at
  the 129.6 the part really does. Do not merge them.
- **The output hsync position affects left-hand corruption, and nothing models
  it.** `VDS_HB_SP` below 8 corrupts, measured with `VDS_HS_ST` at 10; left-hand
  corruption that survived everything else then cleared by moving the pulse to
  62..77 (later, and a third as wide). The tempting reading — that the floor is
  `VDS_HS_ST - 2` — is **refuted by that same state**, which is clean with
  `VDS_HB_SP` at 9, fifty units before the pulse. Position or width, one at a
  time, is the experiment. Treat 8 as measured at one hsync setting only.
  `snapshots/hsync-tuned-no-left-corruption-2026-08-06.json`.
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

  So `geometry_math.py` no longer computes a margin: `VDS_?B_ST` goes to the last
  value below the raster total, taking all of it, and `HEADROOM_WARN_PX = 100` is
  **a floor to warn below, not a budget to reserve**. Michael's reading, which
  fits: banded non-monotonic thresholds look like marginal signal integrity, in
  which case the numbers are facts about *this board*. A torn picture is still
  not automatically a fault to chase.
- **The zigzag is NOT HSCALE-banded, and that is measured.** Michael swept
  `VDS_HSCALE` by hand across the corrupted state on 2026-08-09 and **no value
  cleared it**. The previous session's reading — clean at 823 and 762, torn at
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
  `applyPresets()` copies `VDS_DIS_?B_*` into them (`gbs-control.ino:4097`) and
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
  ceiling — `OutputRaster::WorkingCeilingHz`. `src/tv5725/DisplayClock.h`.

  **The engine nevertheless asks for 108, and that is not a contradiction.**
  `OutputRaster::EngineCeilingHz` is a *usability* limit, not an electrical one:
  a wider raster needs a wider capture to fill it, and at 129.6 MHz the floor
  lands exactly on the default framing so horizontal zoom-in has no travel.
  Three constants, three different questions — do not collapse them.

  The live trap is the inverse: `ofw_RGBS` and `ofw_ypbpr` are the only presets
  that **clear** bit 3, so loading either brings HBOUT/VBOUT alive carrying
  whatever stale window was left behind.

## Working with the unit

Snapshot before changing anything, diff after:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> --out snapshots/before.json
python3 tools/gbsc-pro-hwtest/snapdiff.py --diff snapshots/before.json snapshots/after.json
```

`snapshots/` holds known-good states. To recover a working picture:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore snapshots/dis-hb-st-tweak-2026-08-03.dump.json --segments 0,1,2,3,4,5 --repeat 2
```

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

## Conventions

**`CODING_STYLE.md` is the C++ style, and it is not optional.** Classes rather
than namespaces over file-scope globals, one class per file named after it,
declare-in-header/define-in-.cpp, minimal OO with no inheritance or virtuals,
dependency injection over reaching for globals, and a behaviour-preserving
refactor proven by diffing `test_geometry --dump`. Every rule in it is there
because it cost a session. Read it before writing firmware C++.

- Commit messages: lowercase area prefix (`tools/hwtest:`, `build:`,
  `framesync:`), then what changed and *why*, with the evidence. Look at
  `git log` before writing one.
- **Load the `tdd` skill before writing code.** Not a formality here: this
  codebase punishes the alternative. An evening went into a preferences bug that
  three sessions "fixed" without a failing test to say what fixed meant, and each
  fix guarded the wrong thing — the open instead of the read, then the read
  instead of the writer. A test that fails for the stated reason first is what
  stops that. Write the test at the cheapest layer that really exercises the
  behaviour, watch it fail, then make it pass.
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
- The Makefile never shells out to nix; entering the dev shell is the caller's
  job, so the rules work outside NixOS.
