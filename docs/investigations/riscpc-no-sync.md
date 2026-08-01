# RISC PC → GBSC Pro: `RGBHV limit no sync`

**Fixed 2026-08-01.** The input signal was fine all along. The ESP firmware
classified a separate-sync source as composite sync and programmed the scaler's
sync processor into a state that cannot see V sync. See § Diagnosis for how that
was established and § The fix for what changed. The earlier monitor-ID
hypothesis is refuted; it is kept below because the evidence that pointed at it
is still worth having, and because it should not get re-derived.

This records what is established, what is ruled out, and what remains open — so
the next session starts from evidence rather than from the top.

**No workaround needed now.** Before the fix, RISC PC VGA straight to the TV was
the way to get desktop modes; the scaler now locks on its own from a cold boot.

## The signal path

- **GBSC Pro** = Tvia Trueview5725 upscaler driven by gbs-control on an ESP8266,
  plus an **HC32F460 "AV module"** — the analog front end (input switching,
  LM1881, ADV7280, sync routing into the scaler).
- **Source:** a RISC PC has **no native mode** in any useful sense. Resolution,
  refresh rate *and* sync format all come from the mode file (MDF), so "the RISC
  PC outputs X" is only ever a statement about how it is currently configured.
  Any claim here that reads as inherent is a mistake — check the MDF.

  **As configured for this work:** **1280×1024, 256 colours, 60 Hz**, over
  VGA/RGBHV with **separate H and V sync**. Measured `HTOTAL` 1856, `VTOTAL`
  1061 — so 63.66 kHz HSync at 59.94 Hz, from a **110 MHz** pixel clock. (The
  scaler's `HTOTAL` is in its own sample clocks, not source pixels — see
  § A working lock.)
- The two halves flash separately: AV module over USB-C with
  [`tools/gbsc-pro-flasher/`](../../tools/gbsc-pro-flasher/), ESP over micro-USB
  with `esptool`.

## Symptom

The ESP's hardware serial reports **`RGBHV limit no sync`**: the input is
detected as RGBHV, but the scaler's sync processor never achieves a stable H
*and* V lock (`STATUS_16 & 0x0a`). It retries 300 times, gives up, and outputs
nothing. (`gbs-control.ino`, the sync watcher around line 6648.)

## How it started

1. Flashed the AV module to v1.3 → picture became **corrupted**.
2. Flashed the ESP to v1.3 to match, then `/uc?1` factory reset → **no signal**.
3. Reverted the ESP (full 4 MB backup) and the AV module to v1.2.3 → **still no
   signal**.

## Ruled out

- **The flasher.** An SWD readback proved the AV module's flash matches the
  official image byte for byte — see [PROTOCOL.md §7](../../tools/gbsc-pro-flasher/PROTOCOL.md).
  "Update success" means the image really was programmed. This was the leading
  suspicion and it is dead.
- ~~**The ESP firmware.** Both the old and the v1.3 ESP builds report the
  identical message, so the failure is upstream of the ESP.~~ **Wrong — this was
  the fault.** Both builds share the classification bug, so agreeing with each
  other told us nothing. Two suspects behaving identically is only evidence when
  they are actually independent.

One detail that looked unexplained: old-ESP + v1.3-AV gave a *corrupted picture*
at one point and *no sync* later, read at the time as the AV module's sync
output being marginal. It fits the diagnosis better — the firmware only converges
on a working configuration when it watches a signal arrive, so what changed
between those two observations was when the source came up, not the hardware.

## Measurements (2026-08-01)

First direct measurements of the sync processor, taken with `printInfo()`
repaired (it had been printing nothing — see the debug-interface doc) and with
`/getreg` polling `STATUS_16` over HTTP.

`stable` in the sync watcher is `(STATUS_16 & 0x0a) == 0x0a`: bit 1 HS active
**and** bit 3 VS active, at the same moment.

Steady state, ~574 samples over 25s after a clean reset:

| `STATUS_16` | meaning | share |
|---|---|---|
| `0x01` | HS polarity set, nothing active | 93% |
| `0x03` | HS polarity + **HS active** | 7% |
| `0x00` | nothing | <1% |

- **VS active was never once observed** in steady state.
- HS active appears in ~7% of samples — present but marginal.
- `STATUS_SYNC_PROC_HTOTAL` reads 0 throughout; `VTOTAL` reads noise (97–108,
  against the 1061 this mode actually runs at).
- `noSyncCounter` stays at **0**, because line 5880 only increments it when
  `videoStandardInput != 15` and this unit is in mode 15. So the
  `noSyncCounter < 100` guard on the RGBHV re-init (line 6625) is *not* what
  stops recovery — a plausible-looking theory that the measurement killed.
- `RGBHV limit no sync` fires roughly once every 30s, so that recovery path is
  running.

One earlier 663-sample window did show VS active 23% of the time and eight
moments of genuine H+V lock. That has not been reproducible since; the unit was
reporting video mode 15 in `printInfo` at the time and a different mode after.
It was initially attributed to the sync watcher being enabled, but a
watcher-enabled sample after a reset looks identical to a watcher-disabled one,
so **that comparison was confounded and the attribution is withdrawn.** What it
does establish is that a brief H+V lock is achievable at all.

**Reading at the time — wrong, kept for the record:** that the scaler was not
being denied a lock by the firmware but was simply not seeing a usable V sync.
The § Diagnosis experiment refutes this. V sync is present and lockable
throughout; whether the chip *reports* it depends entirely on how the firmware
has configured the sync processor.

### A working lock, for reference

Later the same session, rebooting the RISC PC produced a stable lock — the first
one on record. Captured immediately, for comparison against the broken state:

| | broken | working |
|---|---|---|
| `STATUS_16` | `0x01` | `0x0f` (H and V active, both `+`) |
| H and V active together | 0–4% | **100%** (296/296 samples) |
| `STATUS_SYNC_PROC_HTOTAL` | 0 | 1856 (±1) |
| `STATUS_SYNC_PROC_VTOTAL` | 98, drifting | 1061, rock steady |
| `SP_SOG_MODE` | 1 | 0 |
| `SP_EXT_SYNC_SEL` | 1 | 0 |

`printVideoTimings()` reported `CsVT: 1061`, agreeing with the sync processor.

**Only the reboot changed** — same cable, same connector. At the time this was
read as proof that the fault lay in what the RISC PC selects at startup. It is
better read as: the reboot makes the signal disappear and reappear, and the
firmware only converges on a working configuration when it watches a signal
*arrive*.

The mode is fully characterised, and the mode file settles it exactly. The MDF
(`RetroScale`, from `!MakeModes`) defines 1280×1024 as **110 MHz** over 1728
pixels by 1062 lines — 63.66 kHz HSync, 59.94 Hz. The scaler reads `VTOTAL`
**1061** against that 1062, which is as close as a measurement gets.

**`RetroScale` is hand-authored, not an Acorn mode file.** That is fine for what
it is used for here — it is what this RISC PC runs, so it is the correct thing to
predict *this machine* against. It is not evidence about RISC OS timings in
general, and its numbers must not be generalised into rules or defaults. Use a
stock Acorn file (`AKF50`, `AKF52`, …) for that.

**`HTOTAL` is not the same quantity, and reading it as one is a mistake made
here before.** The scaler samples on its own PLL locked to HSync, so `HTOTAL`
1856 is in *scaler sample clocks*, not source pixels. It is not the RISC PC's
1728. What it says is that the scaler samples each line 1856 times — about
118 MHz — so it oversamples the 110 MHz source by roughly 7%. Compare `VTOTAL`,
line rate and field rate against a mode file; never `HTOTAL`.

`tools/gbsc-pro-hwtest/mdf_modes.py` derives all of these from an MDF, which is
what makes a mode file a *prediction* to check the scaler against.

A DSO Nano read 92.8 kHz on pin 13 while broken, but its frequency counter was
visibly unreliable at that rate and the reading is not needed by any surviving
conclusion.

## Diagnosis (2026-08-01)

**The input signal is fine. The firmware's sync watcher is what loses the lock.**

The decisive experiment, using the register snapshots in
[`tools/gbsc-pro-hwtest/snapshots/`](../../tools/gbsc-pro-hwtest/snapshots/):

1. With the unit not locking, dump the registers and diff against the working
   snapshot. **24 of ~37 differing registers are in segment 5, the sync
   processor** — including `SP_SOG_MODE` 0→1 and `SP_EXT_SYNC_SEL` 0→1.
2. Write the working segment-5 values back. It locks **immediately, on the first
   pass, every time**: `STATUS_16` `0x0f`, `HTOTAL` 1855, `VTOTAL` 1061.
3. Stop rewriting: within seconds the firmware restores its own values and the
   lock collapses. `HTOTAL` decays 1039 → 553 → 0.
4. Stop the sync watcher (`/sc?m`) *and* restore segment 5: **locked 100% of
   5,793 samples over 75 seconds, and the desktop appears on the TV.**

So the signal was lockable the whole time. The sync watcher, once it has decided
there is nothing there, hunts through sync-processor configurations and never
revisits the one that works — and it overwrites any attempt to put it right.

This also explains the confounded result recorded above. Disabling the watcher
*alone* changes nothing, because the sync processor is already in a broken
configuration and nothing puts it right. Both steps are needed, which is why the
first attempt looked like a null result.

### Root cause: one bit, from a sync-type test that cannot fail

Bisected by applying each broken value alone from the working state, with the
sync watcher stopped so nothing interfered. Of 26 differing segment-5 registers,
**24 have no effect whatsoever**:

| register | good → broken | lock |
|---|---|---|
| `5:0x20` `SP_EXT_SYNC_SEL` (bit 3) | 0 → 1 | **0/8 — kills it** |
| `5:0x56` `SP_SOG_MODE` (bit 0) | 0 → 1 | 5/8 — degrades |
| the other 24 | — | 8/8, no effect |

Isolated to the bit: `0x20` = `0x0a` (only `EXT_SYNC_SEL` set) gives 0/8, while
`0x00` (only `SOG_P_ATO` cleared) stays 8/8. **`SP_EXT_SYNC_SEL` = 1 alone
blinds the sync processor**, and clearing it restores the lock immediately.

Both registers are written together by `bypassModeSwitch_RGBHV()`:

```c
if (rto->syncTypeCsync == false) {   // separate H/V — correct for a RISC PC
    GBS::SP_EXT_SYNC_SEL::write(0);  GBS::SP_SOG_MODE::write(0);
} else {                             // composite sync
    GBS::SP_EXT_SYNC_SEL::write(1);  GBS::SP_SOG_MODE::write(1);
}
```

So the fault is that `rto->syncTypeCsync` is true for a source with separate H
and V sync. The test that sets it cannot do otherwise:

```c
for (int i = 0; i < 3; i++) {
    rto->syncTypeCsync = 1;              // assume composite
    float sfr = getSourceFieldRate(1);   // measure field rate under that assumption
    rto->syncTypeCsync = 0;
    if (sfr > 40.0f) decodeSuccess++;
}
if (decodeSuccess >= 2) rto->syncTypeCsync = true;
```

It asks whether assuming composite sync yields a plausible field rate. A
separate-sync source at 60 Hz answers yes, because **any** source above 40 Hz
answers yes. There is no discrimination in the test, so a working RGBHV source
is classified as composite and configured accordingly.

`SP_EXT_SYNC_SEL` is also written to 1 at `gbs-control.ino:4113`, again gated on
`rto->syncTypeCsync`.

## The fix

Make the classification able to say no. A source presenting an independent V
sync is by definition not composite: with `SP_EXT_SYNC_SEL` at 0,
`STATUS_SYNC_PROC_VSACT` going active is direct proof of a separate V sync line,
so `syncTypeCsync` must be false whatever the field-rate test says. That probe
is `sourceHasOwnVsync()`, and it is used in two places:

1. **At classification** (`detectAndSwitchToActiveInput()`) it can now veto the
   field-rate result, so the csync branch needs both a plausible field rate
   *and* no V sync of the source's own.
2. **At the RGBHV recovery path** in `runSyncWatcher()`, which already ran this
   exact probe every 150 no-sync polls and then threw the answer away. It now
   clears `syncTypeCsync` on the way past, so the retry that follows does not
   hand `bypassModeSwitch_RGBHV()` the same wrong answer again. Without this the
   hunt can never revisit the configuration that works.

This is not a new idea in the file: `applyPresets()` already reads `VSACT`
exactly this way to set `syncTypeCsync` for video modes 0 and 14.

### The last inferred step, now measured

Whether `syncTypeCsync` really was true at the moment it broke had only ever
been inferred from registers — it lives in ESP RAM, so neither `/getreg` nor
`printInfo()` carries it. A `debugPrintf` at the decision point settles it. On a
cold boot with the RISC PC attached:

```
sync type: 3/3 field rate probes plausible, own V sync yes -> separate H/V
```

All three field-rate probes passed, which is `decodeSuccess = 3` and would have
set `syncTypeCsync = true` on its own. The chain is now measured end to end with
nothing inferred.

### Result

Flashed and tested on the unit. After a cold boot, with the sync watcher running
normally and nothing restored by hand:

| | before | after |
|---|---|---|
| `SP_EXT_SYNC_SEL` | 1 (363 of 363 samples) | 0, never once set |
| `SP_SOG_MODE` | 1 | 0 |
| H+V lock | 40 of 426 samples, flickering | **322 of 322 over 5 minutes** |

Two regression tests cover it in
[`tools/gbsc-pro-hwtest/`](../../tools/gbsc-pro-hwtest/), both behind `--source`:
`a_source_with_its_own_vsync_is_not_configured_for_csync` and
`the_sync_processor_holds_a_lock`. Both fail on the pre-fix firmware.

A third route to `syncTypeCsync = true` survives, untriggered so far: four
consecutive `STATUS_INT_SOG_BAD` reads in `runSyncWatcher()` flip it, and SOG is
not in use on an RGBHV input, so "SOG bad" says little about one. It now logs
when it fires, so it will announce itself rather than having to be deduced.

### Recovering the picture on firmware without this fix

```sh
curl 'http://<unit>/sc?m'                       # stop the sync watcher
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <unit> \
    --restore tools/gbsc-pro-hwtest/snapshots/riscpc-working-2026-08-01.json \
    --segments 5
```

Neither step persists: an ESP reset re-enables the watcher
(`rto->syncWatcherEnabled` is runtime-only, defaulted true at `gbs-control.ino`
line 474) and the sync processor reverts. With the watcher stopped the unit also
will not react to a source mode change.

### Still open

**Check whether the same test misfires for other sources.** Nothing about the
broken classification is specific to a RISC PC — the field-rate test passes for
*any* source above 40 Hz, so every separate-sync RGBHV source should hit it.
Only this one source has been tried. If it does generalise, this is an upstream
gbs-control bug worth reporting rather than a fork-local patch.

## Superseded hypothesis: RISC PC monitor-ID pins

**Refuted by § Diagnosis** — the signal is lockable exactly as it arrives, so
nothing about the RISC PC's mode selection needs to change. Kept because the
reasoning was sound on the evidence available, and to stop it being re-derived.

The RISC PC auto-selects its `MonitorType`/mode — and therefore its sync format
(separate H+V vs composite vs sync-on-green) and pixel rate — from the VGA
connector's monitor-ID/DDC pins. The scaler's VGA input almost certainly presents
different ID pins than the TV does, so RISC OS may be picking a mode whose sync
the gbs-control RGBHV bypass cannot lock.

(This section originally leaned on the machine having a "native 1024×768@75" near
the RGBHV passthrough limit. That was wrong twice over: the mode was
1280×1024@60, and there is no native mode to appeal to in the first place — the
MDF decides. The hypothesis is refuted on other grounds anyway.)

## Recovery

A full 4 MB ESP backup taken before any of this is kept in a local backups
directory **outside the repo**, deliberately not committed: a full-flash image
includes the saved Wi-Fi credentials. Restore with
`esptool ... write_flash 0x0 <file>`. The AV module rolls back by flashing
`firmware/GBSC_PRO_AV_MODULE_v1.2.3.bin`, and SWD (PROTOCOL.md §7) can recover it
even if the bootloader is lost.

## Aside: 3 missing rasters at the bottom

The TV reports 1280×**1021**@60 from a 1024-line source, and the bottom 3
rasters really are missing — confirmed visually, not just a readout artefact.

Three candidates falsified, each with a deliberately large change rather than a
nudge, so that "no effect" is informative:

| register | manual | change | result |
|---|---|---|---|
| `HD_VB_SP` S1_45/46 | "Output vertical blank to DAC" | 20 → 60 | nothing |
| `HD_VB_ST` S1_43/44 | "Output vertical blank to DAC" | 0 → 900 | nothing |
| `HD_EXT_VB_ST` S1_4B/4C | "…for DVI mode" | 0 → 900 | nothing |

All restored. The reading is that the HD-bypass output blanking registers are
inert here and the output timing passes straight through from the input — so
the loss is on the capture side, not the display side.

`HD_EXT_VB_SP` (S1_4D/4E, reads 6) is untried. Worth knowing that gbs-control
does not define the `HD_EXT_*` family at all, so the firmware never writes them
and experimental values stick.

Register names and semantics from
[`docs/Tvia TrueView 5725 Registers Definition (RD-5725-1.1).pdf`](Tvia%20TrueView%205725%20Registers%20Definition%20%28RD-5725-1.1%29.pdf).
The product datasheet in `~/Projects/RiscPc/docs/` has no register map at all.
