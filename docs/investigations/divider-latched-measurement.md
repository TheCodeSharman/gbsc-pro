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

155 is below `CaptureWindow::SourceVerticalTotalMin`, so
`SourceMeasurement::sampleSteady()` refuses the count and `poll()` returns
before it measures anything. **The gate is the steadiness one, not
`lineRateFrom()`** — `measureLineRate()` is never called at all, so the line
rate and the field rate behind it are not read either. Refusing leaves
`PLLAD_MD` exactly as it was, and **the divider that caused the bad reading is
the one thing a refusal cannot change**, so the state is stable rather than
transient: it held for as long as it was watched, and the picture stays black
with every configuration register self-consistent.

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

## What is implemented

The first shape, keyed off a third register. `STATUS_SYNC_PROC_HTOTAL` counts
real ADC clocks per line, so with the PLL locked at the ratio the divider asked
for it EQUALS the divider — and a near-integer multiple between them is direct
evidence that it is not, taken from a `STATUS_SYNC_PROC_*` read the engine is
already allowed. It is not read to derive a register; it decides whether the
other two measurements can be believed at all.

`SourceMeasurement::recoverDivider()` acts on that, and `Geometry::poll()`
reaches it only where `sampleSteady()` has already refused. Four things keep the
readings that look like this and are not from moving anything:

| reading | divider | lines per count | why |
|---|---|---|---|
| 2249 trapped | 1124 | **2** | 2249 is one sample off 2248 |
| 2250 locked | 2250 | 0 | two counts would be 4500 |
| 2558 unlocked | 2553 | 0 | five out, and the nearest multiple is 5106 |
| 2400 unconfigured | 2553 | 0 | unrelated to the divider, so to every multiple of it |

The corrected count must itself be a source count, which is what the 97 a preset
load leaves behind cannot reach: two lines per count makes 194. The field rate
is then measured independently, at the ESP off `DEBUG_IN_PIN` rather than
through the ADC, and only after a multiple has been found — it costs up to
250 ms a vsync pulse. Attempts are capped, and every ambiguous outcome leaves
the divider alone.

The line rate moves with the divider, because `Adc::applySampleRate()` picks the
post divider from `divider x lineRate`: one left on the old mode's crossover row
makes every later measurement garbage.

**The engine only polls while a mode change is outstanding.** On the measured
trap the sketch reloads a preset, so it is; a source change that never reaches
`modeChanged()` still has nothing watching it.

## Measured working

2026-08-20, source changed 640x480@75 down to 320x256@50 with the divider held
at 1124. The trap appeared and cleared unaided in 1.7 s, the firmware naming
its own reasoning on the console:

```
divider: 2251 samples / 1124 = 2 lines per count -> 310 lines, 1124 -> 2250
```

Two things the same run settles about the signal, both of which had made the
correction unreachable in its first form:

- **Neither register holds still.** `STATUS_SYNC_PROC_VTOTAL` alternates
  155/156 across a longer window, and `STATUS_SYNC_PROC_HTOTAL` spans
  2246..2251 against a target of 2248 — offsets of -2 to +3. A gate wanting
  either value to repeat exactly never opens, and a fixed two-sample window
  rejects the 2251 that in fact triggered this correction.
- **The unmeasurable run is short in wall-clock terms.** 200 samples elapsed
  inside 1.7 s, so `poll()` runs fast enough that the limit costs no visible
  delay.

`snapshots/divider-recovered-from-trap-2026-08-20.json` is the state afterwards.

The encoder does not follow the scaler through this: both mode changes in the
run left the TV holding the previous timing, cleared each time by the sync-pad
toggle in [encoder-stale-timing.md](encoder-stale-timing.md). The two faults
look identical on the screen and are unrelated.

The second shape — stepping the divider toward what the held field rate implies
— is not implemented and is not needed while the multiple is available.

**There is no runtime acceptance test, because the state cannot be reached over
HTTP.** The trap needs the engine to be *holding* a divider that is wrong for
the source in front of it, and every route reachable remotely re-asserts the
held divider rather than adopting the chip's: a forced preset load writes the
engine's own value back and latches it, so hand-writing a hostile `PLLAD_MD`
produces a chip that disagrees with the engine rather than an engine that is
wrong. Only a real source change puts the held value out of date. The host
suites carry the arithmetic and the four guards; the acceptance is the bench
reproduction — change the source down and confirm the picture returns unaided.

`SourceMeasurement`'s own header still states that the line count is reliable
where the period measurement is not, which is true only while the divider is
already roughly right.

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

**The bits do not persist, so the argument above needs correcting.** Measured
with the source driven over ModeServ and NOTHING polled during the change --
`INT_RST_*` all pulsed to clear, the mode changed, then a single read 25 s
later:

```
before  s0_0F = 0x00      -> 640x480@60, +25 s   s0_0F = 0x00   VTOT 524, MD 1124
before  s0_0F = 0x00      -> 320x256@50, +25 s   s0_0F = 0x00   VTOT 311, MD 2250
```

Both changes solved correctly, so this is not a broken transition. Nothing
acknowledges bit 3, and it still reads 0 afterwards. So it is not latched in the
sense assumed, or it self-clears when the condition ends -- and **a slow poll
CAN miss the edge**, which is the property the whole argument rested on.

The bits are visible only while the change is happening. Polling s0_0F four
times a second from the host does catch them -- bit 4 at +0.92 s on one
transition, bits 1, 3 and 7 at +0.96 s on another -- but that poll rate is not
usable: register reads are deferred to `loop()`, and it starved the loop badly
enough to wedge the mode change it was watching, leaving `PLLAD_MD` on the
previous mode's value with `IF_HSYNC_RST` 202.

**So the host cannot settle this, and firmware can.** `loop()` reads s0_0F far
faster than any HTTP poll and pays one I2C read for it. The sampling log carries
the byte as its last column for exactly that reason; `mode_detect_watch.py`
samples it too, but from outside, which is the side that cannot see it.

What that costs the design: the interrupt is still the cheapest possible
trigger, but it can only be consumed in `loop()`, and it cannot be used as a
flag that survives until someone gets round to it.

## See also

- [../scaler-geometry-model.md](../scaler-geometry-model.md), "What the IF counter counts"
- [../capture-limits.md](../capture-limits.md) for the other bound on the divider
