# Why `HPERIOD_IF` goes bad

**Status:** open. The cause is not known. What to do about it — validate against
the expected value for the mode, read it once just after the preset apply — is in
[`tv5725-chip.md`](../tv5725-chip.md); this page is what has been ruled out, so
the same ground is not covered again.

## It follows the source, not the preset


Watched across a source mode change back from 800x600 to 320x256:

| t | `SP_VTOTAL` | `HPERIOD_IF` | `VDS_HSYNC_RST` | `PLLAD_MD` |
|---|---|---|---|---|
| 15 s | 627 | **511 railed** | 0 | 1856 |
| 17 s | 171 | **431 healthy** | 0 | 1856 |
| 20 s | 311 | 431 | 2704 | 2345 |

**It recovered three seconds before the preset loaded**, while the bypass preset
was still in place. So it tracks the incoming signal, not the register
configuration — which rules out the tempting explanation that HD bypass takes the
Input Formatter out of the path and leaves it nothing to measure.


## The destination mode predicts it, the journey does not

Measured over 195 judged mode transitions on a live unit, firmware running, all
eight sub-535-line AKF50 modes, no bypass anywhere. The first cut is by what
disturbed sync immediately before the dwell:

| disturbance immediately before the dwell | transitions | failed | |
|---|---|---|---|
| **deep** — `SP_VTOTAL` read 0, or a wild non-mode value | 57 | 16 | **28%** |
| the ordinary one-sample `97`/`98` blip | 49 | 9 | **18%** |
| clean — real mode straight to real mode | 89 | 3 | **3%** |

A deep sync loss does raise the odds, and that much survives. But an earlier
version of this section, drawn from the first 42 transitions, read it as a clean
split — *"0 failures in 32 transitions without a deep sync loss"* — and sent two
sessions after a discriminator that does not exist. The full sample fails 18% of
the time after a mere `97`/`98` blip and 3% after nothing at all. **It is a
correlation, not a test.** Do not use it to decide whether a sample is
trustworthy.

What the larger sample gives instead is sharper, because it is about the
destination rather than the journey:

| VTOTAL | expected | dwells | failed | | settled `HPERIOD_IF` when it failed |
|---|---|---|---|---|---|
| 524 | 212 | 25 | 11 | **44%** | `50` ×128, `19` ×45, `93` ×26 |
| 363 | 308 | 25 | 7 | **28%** | `511` ×20, `255` ×9, `269` ×8 |
| 533 | 250 | 25 | 6 | **24%** | `511` ×50, `510` ×4 |
| 311 | 431 | 25 | 1 | 4% | `350` ×11 |
| 261 | 429 | 24 | 1 | 4% | `350` ×17 |
| 499 | 179 | 24 | 1 | 4% | `93` ×26 |
| 519 | 176 | 24 | 1 | 4% | `93` ×23 |
| 448 | 214 | 25 | 0 | **0%** | — |

Three modes carry three quarters of the failures and one mode never fails at all,
over equal numbers of dwells. **The wrong values repeat too**: `524` lands on a
stable `50` again and again, and `311` and `261` — different modes, different
expected values — both land on `350`, so it is not a scaled mis-measurement of
the line. That splits the fault into at least two mechanisms, railing to `511`
and latching a specific wrong number, and it gives something reproducible to aim
at: park the source in VTOTAL 524 and roughly two dwells in five will fail.

Both tables are reproducible — the sweeps are committed, so nobody has to take
this on trust or re-run the bench:

```sh
python3 tools/gbsc-pro-hwtest/precursor.py \
    tools/gbsc-pro-hwtest/sweeps/2026-08-04-akf50-*.jsonl.gz
```

`sweeplog.py` records a fresh one (unfrozen, so preset loads are in the picture —
`hperiod_sweep.py` is the frozen counterpart) and `analyse_sweep.py` prints the
per-transition verdicts behind the summary.

The mechanism below is still the leading explanation, and it unifies this with
[preset-load-clobber.md](../preset-load-clobber.md): a deep sync loss makes the
firmware re-detect and **reload a preset**, and the reload leaves part of the
chip inconsistent. Freezing prevents the reload, which is exactly why every
frozen sweep and both register-replay bisections came up clean — they were
exercising the steady-state write loop, which is the wrong code entirely.

Distinguishing a transient from the fault, since it decides whether a sample
means anything:

- a **transient** `97`/`98` for one or two samples during a mode change is normal;
  it is what `STATUS_SYNC_PROC_VTOTAL` reads mid-relock
- `SP_VTOTAL` **steady** at a non-mode value is the fault

Judge only settled samples — discard ~6 s after any mode change. Raw sampling
across a sweep produces garbage readings at almost every change that resolve on
their own, and scoring those as failures gave 15 false positives in a single run.


## One mechanism is split off and closed: the vertical blank

A window whose start lies beyond the frame stops the input formatter emitting
anything, `HPERIOD_IF` included, and `IF_VB_ST` is solved for the mode. So a
mode change into a SHORTER frame strands it, and the register that would put it
right is only written once the measurement it prevents has succeeded. That is
established by single-register experiments both ways and is fixed;
[`if-vertical-blank-strands-the-measurement.md`](if-vertical-blank-strands-the-measurement.md)
has it.

**Check `IF_VB_ST` against the frame before treating a reading as this page's
fault.** The frame is the source line count, doubled when the line doubler is in
the path. In range, the fault here is still open and none of the below changes.

Two things follow for what is on this page. **The frozen results are consistent
with that mechanism and cannot have exercised it**: `/freeze?on=1` stops anything
writing `IF_VB_ST`, so a window left inside the smallest frame in the sweep stays
inside every larger one, and the divider walk never touched it either. That does
not refute the frozen-sweep conclusion, it narrows what those runs could have
shown. And **the state saved here is NOT that mechanism**:
`snapshots/hperiod-railed-latched-2026-08-20.json` has `IF_VB_ST` 578 against a
line-doubled 311-line source, a 622-line frame, so the window is in range.

A settled 524-line source still reads a steady `HPERIOD_IF` of 50 with the window
correct at 522, the field rate measuring and the picture healthy — so a stable
wrong value survives the fix, and this page has lost nothing.

## The test bus disagrees with the counter, on the same block at the same instant

The field rate timed at `DEBUG_IN_PIN` and `HPERIOD_IF` both come out of the
input formatter. At a mode the counter fails on, they give different answers:

```
at 640x480@60   test pin  60.00 Hz x 524 lines -> 31440 Hz, and the engine solves on it
                HPERIOD_IF  steady 50 in 15 of 15, where 213 is due
```

So this is the **counter**, not the block. Whatever the input formatter needs in
order to time a vertical edge, it has, and it is delivering it correctly while
the horizontal period register is wrong by a factor of four. Anything proposing a
cause that would stop the block measuring has to explain why the other
measurement out of it is unaffected.

It also settles what the fault costs, which is not obvious from the register:
**nothing on the video path**. The geometry solved against the test pin is
correct at both ends of the round trip, the framing returns to 0,0,0,0 and the
picture is clean.

The practical consequence is for anyone tempted to read the line rate here
rather than time it. `HPERIOD_IF` is divider-independent, needs no vsync and
costs one register read instead of up to 250 ms a pulse, so it is an attractive
replacement -- but at 524 lines it is confidently wrong AND perfectly steady,
which no stability check separates from a good reading. Taken at face value it
implies 132 kHz over 524 lines, a 252 Hz field rate, which `lineRateFrom()`
rejects as outside the band -- so the engine would refuse for ever rather than
solve wrongly. 640x480@60 would simply never come up. **It is not usable as the
measurement until this page is closed.**

## Reproduced from a cold boot, current firmware

The table below is re-run with the vertical-blank stranding fixed, so it is the
railing alone. The source is not touched between the first two rows.

```
                     VTOTAL    MD  HT_OK  HPERIOD_IF
as cold booted          311  2250      0  0/6/255/260/511, garbage
at 640x480@60           524  1124      1  50, steady in 15 of 15
back at 320x256@50      311  2250      1  430/431, correct
```

`IF_VB_ST` is 578 against a 622-line frame throughout the first row -- in range,
so the cold-boot garbage is not the stranding fault. `STATUS_IF_HT_OK` does
separate the first row from the others, and is the one place it carries
information; it reads 1 for the steady wrong 50, so it is still not a validity
signal.

## Hypotheses tested and refuted

An earlier version of this section concluded that it "works on 15 kHz sources and
fails on the high-res ones". **That is wrong**, and it was drawn entirely from
unfrozen data where every source change dragged a preset load along with it.

With the firmware frozen (`/freeze?on=1`, so nothing writes a TV5725 register),
a ten-mode sweep across 15.625-37.879 kHz produced **zero railed samples in ~620
reads** — including the 800x600 modes that had railed all morning. Three
hypotheses have now been tested and refuted:

| hypothesis | test | result |
|---|---|---|
| HD bypass takes the IF out of the path | watched the transition | recovered *before* the preset loaded |
| high line rates defeat the IF | frozen sweep to 37.9 kHz | 0/620 railed |
| changing `PLLAD_MD` rails it | 20-point frozen walk, `PLLAD_MD` 1000..2900, 4201 samples logged from `loop()` | 0 railed. See below -- the earlier version of this row wrote 2269 → 2500, both high, and overstated what it had shown |
| `getVideoMode()`'s `random(-2,2)` dither corrupts the Mode Detect thresholds | read s1 `0x66`-`0x75` in the broken state | **byte-identical** to the healthy snapshot |
| the test bus disturbs the IF measurement | froze a healthy unit, replayed all 9 steady-state test-bus writes one at a time | `HPERIOD_IF` never moved off 431 |
| coasting is disabled, so the counter sees vblank equalisation pulses | same method, all 10 sync-processor writes (`SP_DLT_REG`, `SP_H_PULSE_IGNOR`, `SP_PRE_COAST`, `SP_POST_COAST`, `SP_NO_COAST_REG`) | `HPERIOD_IF` never moved off 431 |
| the display clock (`PLL648_CONTROL_01` / `PLL_2XV`) changes the counter's reference | compared snapshots | `0x35` appears in a **healthy** 800x600 capture at `HPERIOD_IF` 176 |

A real preset load did not do it either: `PLLAD_MD` was found at 2269 rather than
the frozen sweep's 2345, so the firmware had detected the source and applied a
preset in between, and the register stayed healthy at 431.

Taken with the earlier session's **79 differing registers applied individually**,
none of which reproduced it, the useful conclusion is negative and worth stating
plainly: **no single register write reproduces this.** Stop looking for one. The
remaining candidates are a *combination* of writes, their ordering or timing, or
something off the TV5725 bus entirely — the Si5351 is the obvious one, since it
appears in no register trace and `/freeze` does not gate FrameSync steering it.


## It does not depend on the divider, over the whole reachable range

The earlier form of this claim rested on one write from 2269 to 2500 -- both
high, both well inside the healthy region -- and was quoted more widely than it
could carry.

Walked properly, on a source parked at 320x256@50 with the mode-cycling script
stopped, firmware frozen, `Tv5725::SamplingLog` sweeping the divider and logging
from the top of `loop()`:

```
PLLAD_MD 1000..2900 step 100, 1000 ms dwell, 4201 samples
HPERIOD_IF: 431 x 3925, 430 x 276.   Rails (0 or 511): 0.
```

- Not one railed sample, at any divider, over 15.6..45.2 MHz of CKO.
- Including the dividers where the **ADC PLL does not lock at all** -- 1300..1800
  and 2600..2900, both measured at 0% lock in the same walk.
- Settled within 4 ms of the latch at nearly every step, so there is no slow
  recovery hiding inside the dwell either.

`STATUS_SYNC_PROC_VTOTAL` held 311 for all 4201 samples and the IF status byte
never moved, so the source was not doing anything during the walk. **Three
hypotheses die here**: that the reliability depends on the divider, that it
depends on the divider only outside the range previously tried, and that it
rails on every transition with only the recovery time differing.

## The latched state survives every software reset

Reached once, on a parked source, and observable for as long as it took to
characterise: `HPERIOD_IF` returning a different wrong answer on almost every
read -- 511, 255, 262, 6 -- while `STATUS_SYNC_PROC_VTOTAL` read a perfect 311,
`STATUS_SYNC_PROC_HTOTAL` echoed the divider, the ADC PLL reported lock, and
`IF_HSYNC_RST` was exactly `PLLAD_MD / 2`. **The picture was perfect throughout**,
which is the part that matters: this is a measurement fault with no video-path
consequence, and nothing in the picture would ever lead anyone to it.

Every ADC, PLL and sync-processor field matched
`snapshots/CLEAN-riscpc-320x256-50-2026-08-15.json` apart from the ones that
legitimately follow the divider. The state is saved whole as
`snapshots/hperiod-railed-latched-2026-08-20.json`.

| tried | cleared it |
|---|---|
| ten `/sc?~` detection passes | no |
| `SFTRST_MODE_RSTZ`, `SFTRST_SYNC_RSTZ`, `SFTRST_DEC_RSTZ`, `SFTRST_IF_RSTZ` pulsed | no -- **but see the caveat below** |
| `IF_HBIN_ST` 32 <-> 0 | no -- same caveat |
| ESP reset: firmware reboot, full chip re-initialisation over I2C | no |
| cold boot, mains and USB | **yes** -- 91/91 samples back at 431 |

So the state lives somewhere no register write reaches. A full re-initialisation
rewrote the chip and did not move it; removing the rails cleared it at once.

**The caveat, and it is load-bearing.** The two rows marked above were taken with
a script that ignored `Probe.write_field()`'s return value, and that function
returns `False` and writes nothing if any register behind the field fails to
read. A dropped write is indistinguishable from a reset that did not help, so
those two nulls are not safe. Redo them with the return checked when the state
is next reachable. The rows either side do not depend on the write path: the
detection passes and the cold boot involved no register writes at all.

**Every remaining candidate is now tried, and none clears it.** These were the
analog-bias ones, on the reasoning that they drop bias rather than digital
configuration, which is the domain a power cycle clears. Run against a live
instance of the fault, each write verified by reading the byte back, each
register restored afterwards:

| tried, writes verified | cleared it |
|---|---|
| `SFTRST_IF_RSTZ` pulsed | no |
| `SFTRST_MODE_RSTZ` pulsed | no |
| all twelve `SFTRST_*_RSTZ` held low together | no |
| `PLLAD_VCORST` pulsed | no |
| `PLLAD_PDZ` powered down and restored | no |
| `ADC_POWDZ` powered down and restored | no |
| ADC and PLLAD powered down together | no |

This also redoes the two rows marked with the write-path caveat above, with the
return checked. They stay negative, so the caveat is discharged rather than
merely outstanding: those nulls are real.

The consequence for `framesync.h` is the one worth carrying. The recovery that
would justify deleting its workarounds -- a register write that clears a state
otherwise needing the plug pulled -- does not exist among these.

## A source mode change clears it, and is the cheapest known clearance

The first section of this page establishes that the value follows the source.
The stronger form: interrupting the source is also what *recovers* it. One round
trip, sampled fifteen times per point:

```
                     VTOTAL    MD  HT_OK  HPERIOD_IF   expected for the mode
railed                  311  2250      0  511/255/19/273, noisy      431
at 640x480@60           524  1124      1  50, steady in 15 of 15     213
back at 320x256@50      311  2250      1  431, steady in 15 of 15    431
```

**The middle row is the fault's third form, not a recovery.** 50 implies a line
rate of 132 kHz through `27e6 / ((hperiod + 1) * 4)`, against the 31440 Hz that
524 lines at 60 Hz actually is. A later sweep read that same VTOTAL 524 at a
correct 213 in three separate modes, so 50 is a stable WRONG value and it is
intermittent.

So the round trip clears it, and the first leg alone does not: the fault goes
noisy -> stable wrong -> correct. What that costs is the obvious shortcut --
changing mode once and reading the new mode's value proves nothing, because the
new mode has its own wrong answer available. **Return to the mode whose correct
value is known, and check against that.**

What the fault is in is the input formatter's lock to the incoming line, and
what shifts it is a real interruption of that line -- which no register write
supplies, and which the firmware cannot generate for itself.

**What this does not settle is whether a cold boot clears it.** One cold boot,
mains and USB pulled, left `HPERIOD_IF` railed, and the mode change above is what
recovered it. But the power-off interval was not recorded, and a short one leaves
the rails standing. So that reading is consistent both with a cold boot being
ineffective and with that particular one being too short. The row in the table
above stands unrefuted and untested against a timed power-off.

## It survives a correct clock group, latched

Alongside the divider sweep above, the whole sampling group has now been
observed correct while the fault stands. Read in one pass on a railed unit,
after a cold boot:

```
PLLAD_MD 2250   PLLAD_KS 2   PLLAD_CKOS 0   DEC1_BYPS 0   DEC2_BYPS 0
ADC_CLK_ICLK1X 1   ADC_CLK_ICLK2X 1   IF_HSYNC_RST 1125
```

Every one of those is the value a healthy bench unit holds, and `PLLAD_LAT` was
pulsed by hand afterwards, so this is not the register-reads-new-PLL-runs-old
trap either. The clock group is not what holds the fault in place.

**One clock combination remains untested**, and it is the one with no register
signature at all: a `PLLAD_CKOS` tap that disagrees with the decimators.
`Adc::applySampleRate()` writes all five together so that the pair cannot be
represented, and every state examined here had them consistent. Whether an
inconsistent pair rails `HPERIOD_IF` is not known. Inducing one is a picture
experiment -- a mismatched tap gives a persistent green screen that a detection
pass repairs -- so it needs the camera and a clip rather than a still.

## Why the noisy form has no consequence, and the stable form would

`HPERIOD_IF` is not read only around a mode change. `updateCoastPosition()` and
`updateClampPosition()` both read it from `loop()`, continuously, and both use it
to place a sync-processor window.

What protects them is a stability guard, not a validity one. Each takes
consecutive reads -- eight and sixteen respectively -- and returns without acting
the moment two differ by 3 or more. The noisy railing returns a different answer
on nearly every read, so every sample fails that window and both functions
no-op. That, rather than nothing reading the register, is why the picture is
perfect throughout.

`updateClampPosition()` has a second protection: it reads `HPERIOD_IF` only on
the csync path, and `STATUS_SYNC_PROC_HTOTAL` otherwise, which stays healthy
through the fault.

**The consequence is which form to fear.** A single stable wrong value passes
both guards untouched and moves the coast and the clamp. These two functions are
where that form bites, and neither compares the value against what the mode
should give.

## The expected value, per mode, measured

`HPERIOD_IF` counts the source line at 27 MHz in units of four ticks, so the
value a mode should give is `27e6 / (4 * lineRateHz) - 1`. That is the check
`tv5725-chip.md` asks for and this is the table behind it, swept over the RISC
PC and read once the mode had settled:

| source line rate | VTOTAL | `HPERIOD_IF` | scan mode | `PLLAD_MD` |
|---|---|---|---|---|
| 15550 Hz | 311 | 431 | doubled | 2250 |
| 15660 Hz | 261 | 430 | doubled | 2250 |
| 21780 Hz | 363 | 308 | doubled | 2250 |
| 26650 Hz | 533 | 251 | progressive | 1124 |
| 31360 Hz | 448 | 214 | progressive | 1124 |
| 31440 Hz | 524 | 213 | progressive | 1124 |
| 37620 Hz | 627 | 178 | progressive | 1856, the bypass literal |

Every settled reading matches the formula within one count. **So a reading can
be checked, and stable-but-wrong is detectable** -- which is the only way to
catch the third form. The 627-line row is the RGBHV bypass trap rather than a
scaled mode; `docs/rgbhv-bypass-trap.md`.

## An OTA reflash is a second trigger, and it needs no test to run

Paired readings across one upload, bench RiscPC at 320x256@50, source untouched
throughout and `STATUS_SYNC_PROC_VTOTAL` a steady 311 on both sides:

| | `HPERIOD_IF` | expected |
|---|---|---|
| settled unit, before the upload | **430** | 431 |
| immediately after `make -C build flash-ota` and a fresh detect | **255** | 431 |
| after the full `--source` suite and an `esptool --after hard_reset` | 254..511 over 12 reads | 431 |

So the state was already reached before any of the `--source` tests ran, on a
source that never changed mode -- the section below names those tests as a route
in, and this says they are not the only one. **The reflash alone is enough.**

The third row confirms the "ESP reset: no" row above by a second route: a reset
that re-initialises the whole chip over I2C leaves it railed, and the recovery
remains a cold boot with mains and USB both pulled.

**The picture was clean and correct throughout**, photographed either side of the
suite, at 1080p on a 1916 x 1125 raster with the framing restored from its slot.
Nothing on screen distinguishes a railed unit from a healthy one.

## A trigger, at last

`pytest tools/gbsc-pro-hwtest/test_geometry_pads.py --host=<ip> --source`
reaches the state, in about four and a half minutes, on a source that never
changes mode. Three tests fail naming the divider, and the unit is left with the
input formatter in the progressive scan mode while a 15 kHz source is attached:
`IF_PRGRSV_CNTRL` 1, `IF_LD_RAM_BYPS` 1, `IF_HS_DEC_FACTOR` 0, `IF_LD_SEL_PROV`
1, `STATUS_SYNC_PROC_VTOTAL` a correct 311, `HPERIOD_IF` railed.

That the scan mode is wrong for the source is itself a defect, and it is the
route in. Whether it is the ONLY route is not known -- the transitions in the
tables above railed without it.

The experiment this unlocks, in order: reproduce, pull mains and USB for a
measured interval, read `HPERIOD_IF` before touching the source.

## What does not reproduce it

Fourteen attempts on a parked source, none of which railed it. Recorded because
each one is a hypothesis somebody would otherwise spend an evening on, and
because the negative result is what says the state is not a deterministic
consequence of the obvious causes.

Five held states, each applied frozen, held, then unfrozen and re-detected:

| held | hold |
|---|---|
| `PLLAD_CKOS` against the decimators the firmware runs | 15 s |
| the same | 90 s |
| `PLLAD_KS` and `PLLAD_CKOS` both off-nominal | 45 s |
| parked at an unlocked VCO -- `KS` 2, `PLLAD_MD` 1600, 100 MHz | 45 s |
| unlocked VCO with the clock chain mismatched too | 90 s |

Then the walk that preceded it, replayed three times: the same hand-written
divider script, killed at the same point, leaving the unit frozen on a divider
it did not choose, unfrozen by hand after 90 s and re-detected. Then the
firmware's own `SamplingLog` divider sweep -- `PLLAD_MD` 1000..2900, which drives
`Adc::applySampleRate()` so the whole clock group moves together -- six times.

All fourteen came back healthy.

Two things fall out of this beyond the negative. **A held mismatch of the ADC
clock chain greens the screen without touching the measurement**: the trials
above ran with the picture green and `HPERIOD_IF` reading 431 throughout, so the
green screen and the railing are separate faults and a session that finds one
should not assume the other. And **parking at an unlocked VCO for 45 s does not
disturb the measurement either**, which is the same conclusion the divider walk
reaches by a different route.

So the trigger is still unknown, and it is not any of: a mismatched clock chain,
an off-nominal post divider, an unlocked PLL, a divider left where the firmware
did not put it, being unfrozen onto a wrong divider, or repeated divider changes
through either the hand path or the firmware path.

## `STATUS_IF_HT_BAD` says when not to believe the value

It reads a constant 0 across a survey of six healthy states, which is what got it
recorded as carrying no information. Against the latched state it separates
cleanly:

| state | `STATUS_IF_HT_BAD` set |
|---|---|
| railed, over ten detection passes | 129 / 191 samples |
| healthy, after the cold boot | **0 / 91** |

Not a per-sample detector -- a third of the railed samples read 0 -- but it never
once set on a healthy reading. **Set anywhere in a sampling window is a reliable
"do not trust `HPERIOD_IF`" gate**, and it costs nothing: it is in segment 0, so
one `read_segment()` gets it alongside the value it judges.

## "Railing" is two different faults under one name

`hperiod_sweep.py` calls a reading bad when it is `0` or `511`. Those are two
**different** phenomena, and merging them is probably why a run of plausible
hypotheses all failed — they were being fitted to a mixed dataset.

Re-analysing 338 samples from one transition capture, split by whether the VDS
was configured:

| state | n | `HPERIOD_IF` |
|---|---|---|
| VDS off (bypass) | 69 | **`0` in 68/69**, 2 distinct values — a genuine, perfectly stable rail |
| VDS on, scaling preset (`PLLAD_MD` 2345) | 269 | **24 distinct values**, 511/255/258/510/2 — noisy garbage, not a rail |

So "railed" describes only the first. The second is a counter returning a
*different* wrong answer on almost every read, and a range check against `0` and
`511` catches barely half of them. When you next characterise this, record the
**distribution**, not a pass/fail — and treat "≥4 distinct values in a settled
window" as the better detector, since a legitimate source mode change produces
one new stable value, never a scatter.

Note also that in **RGBHV bypass the IF is out of the video path**, so garbage
there is not a fault. See [rgbhv-bypass-trap.md](../rgbhv-bypass-trap.md).

**But out of the video path is not out of the MEASUREMENT path, and bypass does
not predict the value.** Measured 2026-08-08 with the unit confirmed in bypass —
`GBS_PRESET_ID` 2, `GBS_OPTION_SCALING_RGBHV` 0, `VDS_ENABLE` 0,
`VDS_HSYNC_RST` 0, `IF_VT_BAD` 1, `IF_HT_BAD` 0, `PLLAD_LOCK` 1 — against an
800×600 source:

```
HPERIOD_IF   177, ZERO spread over 19 reads
             (177+1) x 4 / 27 = 26.37 us = 37.92 kHz
             AKF50 800x600@60 is 37.879 kHz -- 0.11% out. Correct, not garbage.
SP_VTOTAL    627 (+1 = 628, the mode file exactly)
HLOW_LEN     226..227 / HTOTAL 1856 -> duty 0.1217..0.1222 vs AKF50's 0.1212
```

So bypass has now produced `0` (68 of 69 samples, above), `511`
(`snapshots/bypass-800x600-fills-2026-08-02.json`) and a **correct 177** — three
different answers in the same configuration. That settles it in favour of the
finding already recorded under *"It follows the source, not the preset"*: the
counter tracks the incoming signal, and the register configuration does not
determine it. Do not read "we are in bypass" as "`HPERIOD_IF` is meaningless" —
check the value against the mode instead.

Until it is understood, the safe reading is: `HPERIOD_IF` is reliable across the
whole 15.6-37.9 kHz range when the firmware is not writing registers, and rails
in some states the firmware puts the chip into. Range-check it against 0 and 511
before using it, as below.


## Not reproducible across settled states

This is what blocks the one conversion the register would otherwise give, with no
mode file needed:

```
capture units per source unit = IF_HSYNC_RST / (HPERIOD_IF * 4)
                              = 1172 / 1712  =  0.685
```


Steady is not the same as reproducible, and this is the trap. On the *same*
source — `VTOTAL 311` throughout, locked, PLL locked — it has been observed at two
different values, each rock solid: **428** (320 reads) and **361** (2783 reads),
hours apart on 2026-08-02 with `/uc?h` and normal running in between. 16% apart,
zero spread either side.

The ADC oversampling is **not** the explanation, though it looked like one: both
readings were taken with `DEC1_BYPS` and `DEC2_BYPS` at 0. Same sampling state,
different answers. Nothing yet accounts for the difference.

So the useful statements are narrower than they first appear:

- **Independent of `PLLAD_MD`** — established, within one settled state.
- **Stable once settled** — thousands of reads, zero spread.
- **Reproducible immediately after a preset apply** — see below, and this is the
  part that matters.
- **Not reproducible after prolonged running**, cause unknown.

There is also a ~1% gap against first principles: 1712 units implies a 63.4 µs
line, while `VTOTAL 311` at 50 Hz implies 64.3 µs and 1736. Two modes with known,
well-separated line rates would give slope and offset.


## How to reproduce this

Recorded so the numbers above can be challenged rather than believed. Everything
here is reproducible with the repo as it stands, on a RISC PC set to stock AKF50.

**Freeze first.** `curl 'http://<ip>/freeze?on=1'`. Unfrozen, a source mode change
makes the ESP load a preset, which moves `PLLAD_MD` and rewrites most of the chip,
so a reading that moves could be the source, the preset, or the ESP racing the
sampler. Frozen, nothing writes a TV5725 register but the sampler, and the three
refutations above all depend on that separation. This is the single thing that
turned a confusing register into a measurable one.

**Drive the source, not the scaler.** `modesweep.bas` on the RISC PC cycles ten
AKF50 modes, 15.625-37.879 kHz, ten seconds each, repeating. Ten seconds because
the value can take ~6 s to settle after a change; anything shorter samples the
transient.

**Identify modes from the signal.** `hperiod_sweep.py` labels every sample by
`STATUS_SYNC_PROC_VTOTAL`, and the sweep's ten modes each have a distinct VTOTAL.
So the two machines need no clock sync and no agreed ordering, and a mode skipped
on one side cannot mislabel samples on the other. Nothing in the result depends on
the two programs being in step.

```sh
python3 tools/gbsc-pro-hwtest/hperiod_sweep.py --host <ip> --seconds 300 \
        --mdf <path to stock AKF50>
```

3 passes, ~620 samples at 0.4 s. The verdict column calls a mode `ok` only on zero
railed samples *and* at most one count of spread, because one count is 148 ns and
anything wider is not measurement noise.

**Calibrate against a stated number, not a measured one.** The line period comes
from the mode file — 512 pixels at 8.000 MHz is 64.00 µs exactly — never from
`VTOTAL x field rate`. Deriving it from measurements is what produced the earlier
spurious ~1% error: `VTOTAL` reads one line short and the field rate is 50.08 Hz,
not 50. Convert to 27 MHz ticks with `htotal x 27000 / pixel_rate` so the
comparison is in the register's own units and integer where it should be.

**The `PLLAD_MD` test**, which produced a null result and is worth repeating
exactly because of that:

1. Freeze, then confirm the source is stable — six `VTOTAL` reads two seconds
   apart, abort if they disagree. A mode change mid-test is indistinguishable
   from the effect being hunted.
2. Baseline `HPERIOD_IF`, 8 reads.
3. Write `PLLAD_MD` (s5 `0x12` plus the low nibble of `0x13`, read-modify-write so
   only the divider moves). 2269 → 2500.
4. Re-read, 8 more.
5. Pulse `SFTRST_MODE_RSTZ` — s0 `0x47` bit 1, low then restored to exactly the
   byte found, so no other block reset moves.
6. Sample at +1 s and +7 s.
7. Restore `PLLAD_MD` in a `finally`, then unfreeze.

Steps 5 and 6 were never exercised: nothing railed, so there was nothing to
recover. That is the result, not a failed run.

**`VPERIOD_IF` is not usable even when `HPERIOD_IF` is healthy.** In the settled
state above it read **185** against a source of 311/312 lines, while
`STATUS_SYNC_PROC_VTOTAL` read 311 correctly. The firmware's fallback from one to
the other is load-bearing, not defensive.


## The railing has a consumer: the coast stop

`updateCoastPosition()` averages eight `HPERIOD_IF` reads into `accInHlength`
and writes `SP_H_CST_SP = accInHlength x 0.968`. It runs once per source —
`rto->coastPositionIsSet` latches it — so a railed reading is written once and
kept.

Measured on the bench RiscPC at 800x600@60 over VGA, scaling RGBHV, from a clean
restart with no bypass excursion:

```
HPERIOD_IF   511      the rail; 176 is what the mode is due
accInHlength 2044     -> >= 2040, so clamped to 1716
SP_H_CST_SP  1661     against a PLLAD_MD of 1124
```

The coast window therefore stops 537 ADC samples past the end of the line, and
`test_register_bounds.py` reports it. **The picture is correct throughout**, so
this is a reason to look rather than a fault to chase on its own — but it is the
one place a railed `HPERIOD_IF` reaches a register that stays written.

**A full-suite run masks it.** `SP_H_CST_SP` is judged against `PLLAD_MD`, and
tests that move the divider upward leave it temporarily in bounds; the same test
run on its own, on a settled unit, fails. Run it against a unit at its solved
divider or the pass means nothing.
