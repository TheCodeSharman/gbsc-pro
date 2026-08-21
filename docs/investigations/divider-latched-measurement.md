# A wrong sampling divider makes the measurement that would fix it

`Tv5725::SourceMeasurement` chooses `PLLAD_MD` from the source's line rate, and
takes the line count from `STATUS_SYNC_PROC_VTOTAL` because nothing else on the
board can supply it. **That count is not independent of `PLLAD_MD`**, and when
the divider is far enough wrong the count is wrong in a way that stops the engine
replacing it.

Measured 2026-08-20, RiscPC changed live from 640x480@75 to 320x256@50:

| | `PLLAD_MD` | `STATUS_SYNC_PROC_VTOTAL` | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|---|
| divider carried from 640x480 | 1124 | **155**, steady over 56 s | **2249** |
| divider written by hand and latched | 2250 | **311** | **2250** |

Both readings are out by exactly a factor of two, in opposite directions: the
sync processor counts one line per two the source sends. 1124 on a 15.6 kHz line
asks the ADC PLL for 17.6 MHz, below what it will lock at, so it runs at roughly
double and every count taken through it is wrong by that ratio.

## Why it does not recover

155 is below `CaptureWindow::SourceVerticalTotalMin`, so `lineRateFrom()` returns
0, `CaptureWindow::readRasters()` returns false and the engine declines to solve.
Declining leaves `PLLAD_MD` exactly as it was. **The divider that caused the bad
reading is the one thing a refusal cannot change**, so the state is stable rather
than transient — it held for as long as it was watched, and the picture stays
black with every configuration register self-consistent.

## It is asymmetric

Only a change DOWN in line rate traps. Going the other way, 2250 on the 37.4 kHz
line is 84 MHz, inside the PLL's range, so the count is right and the engine
re-solves normally. A source change from 320x256@50 to 640x480@75 therefore works
and hides the fault.

## What the refusal is protecting against

The bound is not wrong: a count of 97 or 98 mid-preset-load is a measurement in
progress and sizing a window from it is worse than waiting. What is missing is
that a *persistent* refusal has no way out. Two shapes fit:

- Re-derive the divider from a measurement the ADC does not colour. The field
  rate is counted at the ESP off `DEBUG_IN_PIN` through FrameSync, so it is
  independent; the line count is not.
- Treat a refusal that repeats as evidence about the divider rather than about
  the source, and step it toward what the held field rate implies before
  measuring again.

Neither is implemented. `SourceMeasurement`'s own header still states that the
line count is reliable where the period measurement is not, which is true only
while the divider is already roughly right.

## VPERIOD_IF cannot supply the independent count

`IF_VPERIOD` is Mode Detect's own line count -- "input source V total lines",
11 bits across s0_07[7:1] and s0_08[3:0] -- and it comes from a different
measurement path from `STATUS_SYNC_PROC_VTOTAL`. It is the obvious candidate for
a witness the divider does not colour. It does not work, and the reason is not
that separate sync bypasses it.

**It is latched, not live.** On the bench source, locked and settled, sixty
consecutive reads returned 51 with no variation at all. Across 60 archived
snapshots whose `SP_VTOTAL` is the same 311, it reads 13, 20, 25, 26, 29, 30,
37, 44, 45, 49, 50, 52, 59, 65, 66, 72, 75, 76, 80, 84, 94, 104, 121, 194, 249,
347, 361, 545, 619, 623 and 910. Perfectly steady within a state, uncorrelated
with the line count across them.

That is the failure `HPERIOD_IF` is already documented for -- a single stable
value that is simply wrong, which every health check scores as healthy because
it is stable and nowhere near a rail.

**A Mode Detect reset clears it and nothing re-latches it.** Pulsing
`SFTRST_MODE_RSTZ` low and high takes it from 51 to 0, where it stays, with
sync present, the picture perfect and every geometry register unchanged.
`HPERIOD_IF` is untouched by the same pulse, holding 431 throughout, so the two
halves of that status register behave differently.

So **a stable `VPERIOD_IF` of 0 does not mean the input is bypassed.** It means
the measurement has not latched since the last thing that cleared it.

What has not been tested is whether a genuine mode change latches it. Nothing in
the firmware resets Mode Detect while sync is present, and `MD_SW_DET_EN` is 1,
so the latching event is plausibly the mode switch itself -- which is exactly the
moment the trap above occurs. The experiment is to read `VPERIOD_IF` immediately
after the source changes, before the engine settles, and see whether it holds the
new mode's line count. Until that is done, `VPERIOD_IF` cannot be polled on
demand and is no use as the independent measurement.

Mode Detect is configured in auto mode throughout: `MD_DET_BYPS_H`/`_V` 0,
`MD_TIMER_DET_EN_H`/`_V` 0 (which selects the stable indicator rather than the
timer), `MD_SW_DET_EN` 1, `SFTRST_MODE_RSTZ` 1. Nothing is switched off.

## The mode-switch interrupt status is the better candidate

`INT_STATUS` at s0_0F is a latched interrupt status byte, and **bit 3 is
"input source switch the mode"**. The eight sources are:

| bit | meaning | firmware reads it |
|---|---|---|
| 0 | SOG unstable | yes, 10 sites |
| 1 | SOG switch | yes, 1 site |
| 2 | SOG stable | no |
| 3 | **mode switch** | **no** |
| 4 | no sync | yes, 1 site |
| 5 | H-sync changed between stable and unstable | no |
| 6 | V-sync changed between stable and unstable | no |
| 7 | H-sync changed between stable and unstable | no |

All eight generators are enabled (`INT_ENABLE0..7` = 1) and `SFTRST_INT_RSTZ` is
1, so the block is running. `STATUS_INT_INP_SW` is declared and read by nothing.

**The interrupt output is not connected, and it does not need to be.** DS-5725-3.2
gives it as QFP160 pin 76, shared with GPIO bit 0, low active; the schematic marks
that pin **not connected**, and the only signals the ESP8266 and TV5725 sheets
share are `MSCLK`/`MSDA`, `DEBUG_PIN` and power. So no ISR is possible.

That costs almost nothing. An ISR here could only set a flag for `loop()` to
notice, and a LATCHED status bit already is that flag -- held in the chip until
acknowledged, so a slow poll cannot miss the edge. One I2C read per pass replaces
`getVideoMode()`'s 6000 ms sweeps, which block `loop()` hard enough that
`/getreg` times out while they run. The pin would have bought lower latency on
something polled at loop rate anyway.

That is the property the engine lacks. It currently infers "the source changed"
from measurements the divider colours, which is what makes the trap
self-latching; a latched flag from a different block does not depend on the
divider at all.

Two things to establish before building on it. Whether bit 3 actually fires for a
source mode change on this board -- it reads 0 now, but a `SFTRST_MODE_RSTZ`
pulse preceded that reading and would have cleared it. And that anything using it
must acknowledge it: `INT_RST_3` exists and nothing writes it, so once latched it
stays latched and reads as a permanent "a mode change happened".

`mode_detect_watch.py` samples s0_0F in the same burst as the period registers,
so one source change answers this and the `VPERIOD_IF` question together.

## See also

- [../scaler-geometry-model.md](../scaler-geometry-model.md), "What the IF counter counts"
- [../capture-limits.md](../capture-limits.md) for the other bound on the divider
