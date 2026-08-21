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

## See also

- [../scaler-geometry-model.md](../scaler-geometry-model.md), "What the IF counter counts"
- [../capture-limits.md](../capture-limits.md) for the other bound on the divider
