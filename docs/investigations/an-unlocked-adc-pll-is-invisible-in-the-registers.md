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

## The ADC clock is running 1.44x too fast, and that is the whole of it

Read against the live bad state, four samples:

```
ht=3248  hlow=230  ratio=1.444  vt=311  md=2250  sog=0 ext=0 loop=1 rths=2092 hsact=1
ht=3239  hlow=204  ratio=1.440  vt=311  md=2250  ...
ht=3249  hlow=230  ratio=1.444  vt=311  md=2250  ...
ht=3241  hlow=210  ratio=1.440  vt=311  md=2250  ...
```

**Both sync-processor measurements scale by ONE factor.** The line is 1.444x the
divider, and `STATUS_SYNC_PROC_HLOW_LEN` reads 230 where a healthy unit reads
159 -- and 159 x 1.444 = 230. The sync pulse has not changed duration; it is
being counted in faster clocks.

So this is a clock-rate fault and not a sync fault. `STATUS_SYNC_PROC_HSACT` is
1, the vertical count is a correct 311, and every register in the sync path
reads what a healthy unit reads.

**The line rate does not change across the round trip**: the csync leg is 308
lines at 50.56 Hz and the return leg 311 at 50.08, both 15625 Hz. So the samples
per line should be unchanged, and the divider the engine solves is the same 2250
either side.

## What it is not

- **Not a missing latch.** `PLLAD_LAT` pulsed low then high by hand against the
  live bad state: `HTOTAL` 3195 -> 3235 -> 3224 over ten seconds, lock still 0.
- **Not the phase sweep.** `optimizePhaseSP()` withheld from the maintenance
  branch entirely: three of three round trips still settled at ~3250.
- **Not the VCO reset.** `resetPLLAD()`'s own sequence by hand against the live
  bad state -- `PLLAD_VCORST` 1, `PLLAD_PDZ` 1, latch, `PLLAD_VCORST` 0, latch --
  left the ratio at 1.416, 1.439, 1.433 over the following sixteen seconds.
- **Not `HPERIOD_IF` railing**, which is its own fault and reads 511 in the
  *good* state here, with a clean full-screen picture.
- **Not the sync-type probe.** Probe durations are drawn from the same
  distribution in passing and failing runs -- 2 ms, 3 ms, and a 1000 ms timeout
  on the csync leg, in both.
- **Not how busy the return leg is.** A passing run and a failing run both show
  ~25 probes, ~10 solves and ~100 sampling lines over the same window.
- **Not the engine's rate measurement.** One failing run rejected 11 readings
  and accepted 20, all at 15625; a passing run rejected 70 and accepted 39
  spread over 15580..15685.

## What reaches it

`/sc?~` -- `goLowPowerWithInputDetection()` -- clears it every time.

## How it is provoked

It appears only when the sketch's escalation stops running against a source the
engine can measure. On a build where `runSyncWatcher()`'s no-sync branch is
keyed on `getVideoMode()`, that branch runs continuously on a scaling RGBHV
source -- the classification returns 0 there for ever -- and `HTOTAL` settles at
2250 in five of five round trips. Keyed on the engine, the branch stops and
`HTOTAL` settles near 3250 in three of three.

**So something the escalation does on a cadence keeps the ADC PLL at the right
ratio**, and which rung is unknown. The obvious candidate is removed: putting a
sync separator level in force latches the ADC PLL, and the `% 150` rung does
that through `optimizeSogLevel()` -- but a latch by hand does not recover the
state.

**The `% 413` ADC input toggle is NOT the candidate, and cannot be**, on this
bench or any unit with an input selected: it is gated on
`detectionMayChangeInput()`, which is `!InputSource::chosen(Info)`. Measured over
a 125 s hunt with `ADC_INPUT_SEL` read 35 times, it never moved.

**What survives is that the PLL may be LOCKED, to a divider of ~3250 that the
register does not report.** `PLLAD_MD` reads 2250 while the chip produces 3250
samples per line, which is the shape of the documented write-after-latch trap:
a divider written after `latchPLLAD()` leaves the register reading the new value
while the PLL runs the old one. What argues against it is that a latch pulsed by
hand does not load 2250 either. What would settle it is watching `PLLAD_MD` and
`STATUS_SYNC_PROC_HTOTAL` across one solve, from the console rather than from a
poll.

## The instrument, because the obvious one does not work

**A register poll fast enough to catch this changes the outcome.** Polling two
fields at ~50 Hz through a round trip turned three failures in three runs into a
pass, and `STATUS_MISC_PLLAD_LOCK` was seen flickering 0/1 within 50 ms in the
HEALTHY state -- so it is a weak witness read as single samples, and the
sustained `HTOTAL` divergence is the reliable one.

The engine's idle pass already reads both halves of the presence answer, so it
logs the transition and the numbers behind it and costs no extra bus traffic:

```
source UNLOCKED: 311 lines, 3251 samples against divider 2250
```

That line lands in the console beside `source moved` and `own V sync`, which is
what made the sequence legible at all.

## Why it blocks the no-sync gate

`Geometry::sourceState()` reports this correctly -- `unlocked`, with `present`
false -- so the gate does let the escalation run once the state is reached. The
escalation then cannot recover it. Withholding the escalation causes a fault the
escalation cannot repair, which is why the gate cannot land on this alone.
`docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md`
