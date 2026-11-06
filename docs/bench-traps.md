# Things that will cost you an hour if you don't know them

- **THE TELEVISION IS SET TO FULL AND DOES NOT OVERSCAN, ON EVERY MODE.** A
  photograph shows the whole HDMI active area, so picture missing from one was
  never sent and "the panel is hiding it" is not available as an explanation.
  `working-with-the-unit.md` has what follows from that.
- **THE ADC PLL GROUP CANNOT BE BISECTED BY HAND, AND TRYING COSTS THE LOCK.**
  `PLLAD_MD`, `PLLAD_KS`, `PLLAD_CKOS`, `PLLAD_ICP`, `PLLAD_FS` and the two
  decimators are ONE setting: `PLLAD_LAT` loads several of them on a rising edge
  and the loop filter has to suit the tap. Writing two or three of them over HTTP
  to compare configurations leaves the PLL unlocked -- measured twice in one
  session, `STATUS_SYNC_PROC_HTOTAL` wandering a few counts under the divider in
  states that reported it back exactly before the poking started. The wandering
  count is the evidence; the lock bit beside it is not, for the reason in
  `register-facts.md` -- it is a duty cycle and not a state.
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
  **ONLY ONE PROCESS MAY DRIVE THE UNIT AT A TIME**, and this is about WRITERS:
  two runs that both call `/input` time each other's source changes, which reads
  as the unit being unreliable rather than as the bench being driven twice. It
  invalidated a twenty-leg acceptance run -- the first run had not died as
  assumed, because `gbs_unit.wait_for()` RETURNS rather than raising, so a
  precheck that never saw an acquired source carried on into its legs and ran
  concurrently with its own relaunch, both writing the same log. **A background
  task reported "completed" is not proof its process exited.**
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
  binaries go in a separate repo; see the photo repo in `repo-layout.md`.
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
