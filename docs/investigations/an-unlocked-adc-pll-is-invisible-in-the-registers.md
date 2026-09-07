# The ADC PLL comes out of lock with every config register unchanged

The state the no-sync gate lands in when the sketch's escalation stops running
against a source the engine can measure. It is not a wrong register value, and
no register dump distinguishes it from a healthy unit.

## The measurement

Bench RiscPC on `vga`, 320x256@50, after a sync-type round trip, with both
branches of `runSyncWatcher()` keyed on `Geometry::sourceIsPresent()`. Every
field read by name, bad state and good state, the good one reached with `/sc?~`:

| | bad | good |
|---|---|---|
| `PLLAD_MD` / `KS` / `FS` / `ICP` / `CKOS` | 2250 / 2 / 1 / 5 / 0 | **identical** |
| `PLLAD_VCORST` / `PDZ` / `LEN` | 0 / 1 / 1 | **identical** |
| `ADC_CLK_ICLK1X` / `ICLK2X` / `DEC1_BYPS` / `DEC2_BYPS` | 1 / 1 / 0 / 0 | **identical** |
| `ADC_POWDZ` | 1 | **identical** |
| `IF_HSYNC_RST` / `SP_RT_HS_SP` | 1125 / 2092 | **identical** |
| `STATUS_SYNC_PROC_VTOTAL` | 311 | 311 |
| **`STATUS_SYNC_PROC_HTOTAL`** | **3234** | **2250** |
| **`STATUS_MISC_PLLAD_LOCK`** | **0** | **1** |

The picture is scrambled -- torn horizontal noise across the whole raster.

**Only the two status readings differ, and they are the two the chip measures
rather than the ones the firmware writes.** So the fault is a sequence, not a
state, and a snapshot diff against a known-good dump reports nothing.

## What it is not

- **Not a missing latch.** `PLLAD_LAT` pulsed low then high by hand against the
  live bad state: `HTOTAL` 3195 -> 3235 -> 3224 over ten seconds, lock still 0.
- **Not the phase sweep.** `optimizePhaseSP()` withheld from the maintenance
  branch entirely: three of three round trips still settled at ~3250.
- **Not `HPERIOD_IF` railing**, which is its own fault and reads 511 in the
  *good* state here.

## What reaches it

`/sc?~` -- `goLowPowerWithInputDetection()` -- clears it every time.

## How it is provoked

It appears only when the sketch's escalation stops running against a source the
engine can measure. On a build where `runSyncWatcher()`'s no-sync branch is
keyed on `getVideoMode()`, that branch runs continuously on a scaling RGBHV
source -- the classification returns 0 there for ever -- and `HTOTAL` settles at
2250 in five of five round trips. Keyed on the engine, the branch stops and
`HTOTAL` settles near 3250 in three of three.

**So something the escalation does on a cadence keeps the ADC PLL in lock**, and
which rung is unknown. The latch is ruled out above, which removes the obvious
candidate: putting a sync separator level in force latches the ADC PLL, and the
`% 150` rung does that through `optimizeSogLevel()`.

**The next thing to try is `PLLAD_VCORST`**, pulsed against a live bad state.
A PLL that will not re-lock on a latch may want its VCO control voltage reset,
and that is the one clock reset not yet tried against this fault -- it has been
tried against `HPERIOD_IF` railing, which is a different state.

## Why it blocks the no-sync gate

`Geometry::sourceState()` reports this correctly -- `unlocked`, with `present`
false -- so the gate does let the escalation run once the state is reached. The
escalation then cannot recover it. Withholding the escalation causes a fault the
escalation cannot repair, which is why the gate cannot land on this alone.
`docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md`
