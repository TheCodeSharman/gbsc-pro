# The sync type is probed, because reading it back is circular

`Tv5725::SyncType` chooses between composite-sync separation and separate H/V.
It holds two facts: what the type is (`isCsync()`) and whether that came from a
measurement (`isSet()`). `set()` deliberately does not mark the type as probed --
the YPbPr fallback and the temporary flips during detection assert a type without
measuring one, and recording those as probed would suppress the real probe.

**It must never be decided by reading `STATUS_SYNC_PROC_VSACT`.** That bit
reports the sync path already configured, not a property of the source, so the
reading only confirms whatever the unit is already doing — and a unit that lands
on csync stays there for the session. `applyPresets()` used to decide that way,
which is what this page exists to stop anyone reinstating.

The decision is made by `sourceHasOwnVsync()`, which breaks the circularity by
*changing* the path before reading the bit.

## The evidence

Same source, same cable, same `ADC_INPUT_SEL` (1), same `PLLAD_MD` (2553), a day
apart:

| sync path | `VSACT` | `SP_VTOTAL` | off-mode |
|---|---|---|---|
| csync — `SP_SOG_MODE` 1, coast 7/3, `IGNOR` 107 (08-14) | **0 in 2375/2375** | 308 | 7.34% |
| separate — `SP_SOG_MODE` 0, coast 0/0, `IGNOR` 255 (08-15) | **1 in 150/150** | 311 | 0.00% |

Both states show a picture. The source is a RISC OS RiscPC at 320x256@50, whose
AKF50 mode file gives `v_timings: 3, 16, 17, 256, 17, 3` — VTOTAL 312, and
separate sync. On the csync path the counter reads **308, three lines short, and
wobbles**; on the separate-sync path it reads 311 and does not move.

The paired dumps are `snapshots/glitching-2026-08-14.json` and
`snapshots/CLEAN-riscpc-320x256-50-2026-08-15.json` in
`tools/gbsc-pro-hwtest/`, with the field-by-field comparison in that directory's
`LOG.md`.

**The earlier note that `VSACT` is "dead on this board" came from seeing only the
top row.** It is not dead. It is also not a lock indicator — both of its values
occur with a perfect picture, which is exactly why nothing may gate on it.

## Where it is decided

| site | how |
|---|---|
| `inputAndSyncDetect()` | field-rate probes **and** `sourceHasOwnVsync()` |
| `applyPresets()`, the mode-14 arm | `SyncType::probeOnce()`, so it measures only if nothing has for this source |
| `applyPresets()`, the no-mode arm | `SyncType::probe()`, unconditional — this arm has just moved `ADC_INPUT_SEL`, so there is nothing to inherit |
| `applyPresets()`, the YPbPr fallback | unconditional `SyncType::set(true)` |

**Only the mode-14 arm reaches `probeOnce()`**, so on a source that classifies as
SD the gate below never runs at all and the type comes from
`inputAndSyncDetect()`'s `set()` calls. That is why the gate cannot be exercised
from a bench source at 320x256@50.

`sourceHasOwnVsync()` clears `SP_EXT_SYNC_SEL`, waits 240 ms for the sync
processor to reacquire V, polls for up to 250 ms, re-confirms after 10 ms, then
restores the register.

**It costs ~500 ms, so it runs once per SOURCE, not once per mode change.**
`SyncType::isSet()` is the gate and `SyncType::forget()` re-arms it.

**What re-arms it is a change of SOURCE, not a cleared clamp.** `forget()` sits
beside `coastPositionIsSet` and `clampPositionIsSet` at the five sites that mean a
different source may now be attached — the two resets, the low-power entry, the
RGBHV watchdog, and `LoadDefault()`, which every OLED input handler reaches
through `resetSyncProcessor()`. **Six other sites clear those two flags and
deliberately do not forget**, because they are mode changes on the source already
attached: twice inside `applyPresets()` itself, twice in `runSyncWatcher()`'s
scaling-RGBHV arm, and the serial clock-generator command. Reading the flags as
the rule re-probes on every preset load and spends 500 ms doing it.

**It cannot be left to `inputAndSyncDetect()` alone.** That probe sits behind
`SyncSearch::searchFor(...) == VsyncPresent`, so on a source with no separate
vsync — the csync case, the one that matters — the block is skipped and the type
keeps its initial false. Circular in the same way, one level up.

**And an input change must forget.** `LoadDefault()` writes `set(false)`, which
asserts separate sync without measuring it. Without a `forget()` beside it the
previous input's `isSet()` survives, `probeOnce()` finds the answer already held
and returns the false just written, and a csync source selected after a
separate-sync one is never measured.

**Two paths do not consult the sync type at all.**
`videoStandardInputIsPalNtscSd()` writes `SP_H_PULSE_IGNOR` 0x6b and
`updateSpDynamic()` writes coast 7/3, whatever the sync type resolved to.

## The flip is reproducible, and so is the recovery

Observed on 2026-08-15 while running the watermark acceptance tests, which load
three preset tables in succession. After the churn the unit was on the csync
path — `SP_SOG_MODE` 1, `SP_EXT_SYNC_SEL` 1, `VSACT` 0 — and had **lost lock**:
`SP_VTOTAL` sat steady at 97, a non-mode value, and stayed there across 40 s of
sampling. It did not recover on its own.

**`/sc?~` recovered it in under 10 s**, back to `SP_SOG_MODE` 0, `VSACT` 1 and
`SP_VTOTAL` 311 stable, and a full 1536-register diff against the pre-churn
state came back with one byte different — `SP_H_CST_SP`, which the sync
processor recomputes anyway. So the recovery is complete, not approximate.

Two things follow. **Preset churn is a trigger**, which makes the flip cheap to
reproduce whenever someone wants to work on it — no waiting for it to happen by
itself. And **`/sc?~` is the recovery**, because it runs
`goLowPowerWithInputDetection()` and forces a fresh detection pass, which lands
on the separate-sync path when the source is presenting separate sync. A plain
preset reload does *not* do it: the acceptance tests already reload
`pal_1920x1080` in their teardown and the unit stayed stuck.

Worth knowing before running the hardware suite on a source like this one: it
can leave the unit without a picture, and the fix is one HTTP call rather than a
power cycle.

## What is *not* being claimed

- **This is not the intermittent shear glitch.** That was closed as
  source-generated on 2026-08-15 — cold-booting the RiscPC as well as the unit
  cleared it and it has not returned. The table above is a sync-path comparison
  that happens to span that event; do not re-read it as a glitch diff.
- **Forcing the separate-sync registers by hand on 08-14 changed nothing
  visible.** That is consistent rather than contradictory: if the analog routing
  has `HS_IN = SOGIN` there is no separate HSync on the pin for the sync
  processor to use, so the registers had nothing to act on. It does not show the
  choice is harmless, and it does not show it matters.
- **Whether the three-line deficit costs anything is untested.** It is a wrong
  number in a counter. No artefact has been traced to it.

## What is still open

The YPbPr fallback sets `csync = 1` unconditionally rather than probing, and the
two SD paths above write sync-separation parameters without asking. Neither has
been shown to cost anything on this bench; both are places where the sync type
is decided by something other than a measurement.

## See also

- CLAUDE.md, *Register facts that are not obvious* — the `VSACT` entry.
- [`riscpc-no-sync.md`](investigations/riscpc-no-sync.md) — where `VSACT` was first read as
  proof of a separate V sync line.
- [`rgbhv-bypass-trap.md`](rgbhv-bypass-trap.md) — the other decision in
  `applyPresets()` that latches for the session.
