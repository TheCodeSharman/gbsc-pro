# The composite shear rides a half-locked ADC PLL

On `vga` at 320x256@50 the picture shears a few times a minute on composite
sync and never on separate sync. Each line is displaced further than the one
above, the picture is displaced down with it, and black fills what the playback
does not reach. It lasts 8 or 9 capture frames at 60 fps -- about seven source
frames -- and returns.

**THE SHEAR IS HORIZONTAL AND `picture_stability.py` IS A VERTICAL
INSTRUMENT.** That one collapses each frame to a ROW profile, so it reports
whole-line jumps and is largely blind to this. `picture_shear.py` is the other
axis.

## The waveform is deterministic

Three events in one 150 s clip, at frames 1989, 3568 and 6076, give the same
per-band displacement frame for frame -- the first displaced frame of each
reads `-124 -192 +412 +832 -712 -40 -416` against `-124 -196 +412 +832 -712 -40
-416` and `-120 -196 +412 +832 -712 -40 -416`, and all three end on
`+440 +376 +324 +608 +240 +220 +164 +100`. Noise does not repeat; a settling
transient does.

Resolved per row group at full width, the displacement is a clean ramp:
**-0.333 emitted px per emitted row**, which is 0.58 ADC samples per captured
line out of 2200. The vertical jump at onset is **+138 emitted rows**, and it
is quantised -- +138 for three or four frames, then -69, then back to -2 --
across all three events.

**THE VERTICAL JUMP IS THE COUNT ANOMALY, IN SOURCE LINES.** At `VDS_VSCALE`
488 on a doubled line, one source line is 4.20 emitted rows, so +138 rows is
**32.9 source lines**. The register reads 337 against 308, which is 29 counted
lines, and a NOR'd csync has no H edges for the 3 lines of vertical sync -- so
29 counted plus 3 uncounted is 32 source lines against the 32.9 measured. The
vertical sync is detected about 32 lines late on one frame, and the capture's
vertical origin moves with it.

## What the sync type changes

Same source, same mode, same divider, same raster, 180 s a leg, 1 ms sampling,
four minutes apart:

| | composite | separate |
|---|---|---|
| `STATUS_MISC_PLLAD_LOCK` duty | **74.3%** | **97.3%** |
| lock transitions | 67.6/s | 7.7/s |
| `STATUS_SYNC_PROC_HTOTAL` | 2200 throughout | 2200 throughout |
| `STATUS_SYNC_PROC_VTOTAL` | 308 x26827, 309 x373, 310 x4, **337 x3** | **311 in 28343 of 28343** |
| shear events | 1 in 183 s | 0 in 183 s |

**THE LOCK DUTY REPEATS.** Four runs on composite spanning two hours and two
cadences: 73.8% of 7511 samples at 25 ms, 74.3% of 27207 at 1 ms, and 74.1%
and 74.5% of about 6900 each on two 45 s runs taken afterwards, with the flip
rate 67.5..68.8/s in every one. It is a property of the arrangement, not a
reading.

The lock bit is one bit in one register, so it carries across the comparison
where a wide field does not --
`a-wide-status-field-tears-across-its-two-bytes.md`. 25.7% of a frame unlocked
is 80 lines of 312, and 67.6 flips against a 50.08 Hz field rate is 1.35 per
frame: a once-a-frame disturbance with a long re-settle, not a continuous
jitter.

**337 IS A GENUINE READING**, 29 lines over the usual 308, and it lands beside
the shear in both runs that caught one -- the only such reading in 7511 samples
at 25 ms, 40 ms before the only event, and three readings in 27207 at 1 ms,
immediately before the only event. It never occurs on separate sync.

## What does not move the lock

Scored on the lock duty at 1 ms, 18000+ rows a leg:

| leg | lock duty | flips/s |
|---|---|---|
| composite, the engine's own coast 7/2 | 74.4% | 67.5 |
| composite, `SP_NO_COAST_REG` 1 -- coast off | 77.1% | 63.1 |
| composite, `SP_DIS_SUB_COAST` 1 | 72.7% | 69.2 |

Turning the coast off entirely buys 2.7 points against a 23-point gap, and
disabling the sub coast is worse. **The coast is at most a tenth of the
instability, and its sign is wrong for a mechanism that is working.**

`ADC_SOGCTRL` is flat across its usable range -- 74.4, 75.0, 73.5, 74.5, 74.5,
74.0, 74.2, 73.7 at levels 3, 5, 7, 9, 12, 16, 20 and 26, with flips 67..69/s
throughout. **The slicing level does not reach it.**

## A third arrangement puts the lock duty and the shear on different axes

The Wii at 480p on `ypbpr` is sync on green -- `SP_SOG_MODE` 1, a sliced
arrangement like composite -- on a direct analog path, with a serrated vertical
interval where the RISC PC's NOR'd csync has none. Measured the same way, 180 s
at 1 ms:

| | lock duty | flips/s | shear events |
|---|---|---|---|
| RISC PC, composite | 74.3% | 67.6 | yes |
| **Wii, `ypbpr`, sync on green** | **84.7%** | **47.0** | **none in 183 s** |
| RISC PC, separate | 97.3% | 7.7 | none |

**SO A DEPRESSED LOCK DUTY IS NOT SUFFICIENT FOR THE SHEAR.** The Wii sits
between the two and does not shear, and a long session of play shows none. The
duty tracks whether the arrangement is SLICED, not whether it shears, so it is
a companion of the sync type rather than the mechanism. 183 s is about one
expected event's worth at the composite rate, so the Wii leg bounds the rate
rather than proving zero.

The Wii also carries the byte tearing independently: `hlow_len` 87 and 1122 are
genuine and 98 (`0x062`) and 1112 (`0x458`) are their crossings, which is the
same structure on a different source and divider.

## Registers that do not move the lock

Recorded so they are not tried again. Everything here is scored on the lock
duty at 1 ms, 7000 to 18000 rows a leg.

| leg | lock duty | flips/s |
|---|---|---|
| composite, the engine's own coast 7/2 | 74.4% | 67.5 |
| composite, `SP_NO_COAST_REG` 1 -- coast off | 77.1% | 63.1 |
| composite, `SP_DIS_SUB_COAST` 1 | 72.7% | 69.2 |
| `ADC_SOGCTRL` 3, 5, 7, 9, 12, 16, 20, 26 | 73.5..75.0% | 67..69 |

**`SP_HS2PLL_INV_REG` AND `SP_HS_POL_ATO` ARE STILL UNTESTED ON COMPOSITE**, and
a run that appeared to test them did not: the source had been left on separate
sync by an interrupted script, so all four of its legs read the separate-sync
figure of about 97.6%. The two fields do differ between the arrangements --
composite holds `SP_HS2PLL_INV_REG` 1 and `SP_HS_POL_ATO` 0, separate sync 0
and 1, and `applyForSyncType()` writes neither on the csync branch. On separate
sync, inverting `SP_HS2PLL_INV_REG` costs 2.5 points and doubles the flip rate,
which is the only thing that run established. **Read `SP_SOG_MODE` back before
scoring any leg.**

## A burst of mis-measured pulses sits on the shear

Measured 2026-10-20 with `/samplinglog?width=10`, 900 s, 2246040 readings at
2496 a second -- one source line in 6.3, against one in 420 for the interval
modes. One shear in the run, and beside it:

```
T+316.348  n=40  min=153  max=154   below=0   cross=1
T+316.359  n=16  min=118  max=153   below=1   cross=0
T+316.369  n=40  min=98   max=153   below=16  cross=0
T+316.379  n=16  min=0    max=154   below=14  cross=0
T+316.442  n=16  min=153  max=2661  below=0   cross=0
```

**31 out-of-band readings in three consecutive 10 ms windows**, the minimum
collapsing 118 -> 98 -> 0, on the one shear event of fifteen minutes. None is a
crossing: the census test passed them as genuine, and 118, 98 and 0 are not
products of the two values in circuit.

So the pulse is not merely mis-measured once. It is mis-measured for about 30
ms -- roughly 470 source lines, one and a half fields -- which is the right
order for a disturbance that displaces the capture progressively and recovers.

**THE SAMPLING RATE IS WHY NO EARLIER SESSION SAW IT.** At 25 ms the log reads
one line in 420, and at 1 ms one in 55: a 30 ms burst lands 0.07 and 0.5
readings respectively, so it cannot appear as a burst and its members read as
isolated singletons. Twelve such singletons in the 1 ms run, at a phase
concentration of 0.151, are what this resolves.

## The fault went quiet, and the three markers went quiet together

Measured 2026-10-20 across the session, on `vga` at 320x256@50 into 1920x1080,
composite:

| run | duration | shears | 30 ms pulse bursts | `sp_vtotal` 337 |
|---|---|---|---|---|
| monitor, 25 ms | 203 s | 1 | not measurable | 1 |
| monitor, 1 ms | 183 s | 1 | not measurable | 3 |
| width, 10 ms | 900 s | **1** | **1** | -- |
| width, separator level 12 | 1500 s | 0 | 0 | **0** |
| width, separator level 9 | 1200 s | 0 | 0 | **0** |

The handover's own clips, recorded earlier the same day, carry 3 events in
150 s and 4 in 240 s. So the rate fell by more than an order of magnitude
within the session and then stopped.

**ALL THREE MARKERS MOVE TOGETHER**, which is the useful part: a run with no
shear has no burst and never sees the count leave 308/309. That is consistent
with one mechanism and makes any of the three usable as the trigger, but it
also means a quiet unit cannot test anything.

**IT IS NOT THE INSTRUMENT.** The width mode puts about 2500 reads a second on
the bus, and the run that caught the burst was a width run. A quiet result on
one is not the bus being busy.

**THE SEPARATOR LEVEL IS NOT THE LEVER.** `ADC_SOGCTRL` was 9 while the
handover's clips were taken and the engine re-acquired it to 12 during a
`/sc?~` in this session, which made it the obvious suspect for the collapse.
Put back to 9 it gives 0 events in 1200 s against 0 in 1500 s at 12. The level
is also flat against the lock duty across 3..26.

The only thing that changed between the last run to carry an event and the
first to carry none is an OTA flash, which resets the ESP and re-runs
detection. Nothing establishes that as the cause.

**BUT THE ENGINE'S OWN MEASUREMENT OF THE SOURCE MOVED.**
`acquireSeparatorLevel()` chooses `ADC_SOGCTRL` from what the sync looks like,
and it chose **9** at the start of the session and **12** twice at the end,
hours apart on the same source, cable and mode -- the second time after the
level had been set to 9 by hand and the choice handed back with `/sc?~`. That
is a reading, not a setting, so something about the source's sync is not what
it was this morning.

It makes a falsifiable prediction, and it is cheap: if the shear is marginal
slicing of a composite sync whose amplitude drifts as the machine warms, a
COLD RISC PC should bring it back, and the level should land near 9 again when
it does. A source that has been on all day is the condition under which this
page's quiet runs were taken.

## The chain, measured in one acquisition

Measured 2026-10-20 with `/samplinglog?width=10` beside the emitted frame, on
`vga` at 320x256@50 into 1920x1080, composite. One 900 s run, 1902680 readings:

```
T+563.465  min=153 max=2661              the vertical interval, as every frame
T+563.551  min=0            below=5      the pulse measurement collapses
T+563.566  min=0            below=16     21 out-of-band readings in 35 ms
T+563.588                   vt=99..308   the line count collapses to 99
T+563.664  min=98           below=1
           the emitted frame shears, T+563.63..T+563.78, 10 frames
T+563.685  min=153 max=2660              recovered
```

The count reaches **99** against the usual 308, which is the separator losing
vertical sync for a frame rather than miscounting it -- far past the 337 the
earlier runs caught, and 337 is now better read as a mild case of the same
thing.

## The state carries it, and three fields are the candidates

The fault is reproducible by register state rather than by waiting.

| state | duration | shears | bursts |
|---|---|---|---|
| as the engine left it after a separate-sync excursion | 3600 s | **0** | **0** |
| `PA_SP_S` 13, `SP_HS_POL_ATO` 0, `SP_VS_POL_ATO` 0, `ADC_SOGCTRL` 11 | 900 s | **1** | **2** |

Those four are what a 1536-register diff leaves after the differences that are
three days of commits rather than drift --
`tools/gbsc-pro-hwtest/snapshots/csync-320x256-50-quiet-2026-10-20.json`
against `snapshots/csync-320x256-50-full-rolling-2026-10-17.json`.
`ADC_SOGCTRL` is already refuted: 0 events in 1200 s at 9 and 0 in 1500 s at
12.

**`SP_HS_POL_ATO` AND `SP_VS_POL_ATO` ARE WRITTEN ON ONE BRANCH ONLY.**
`SyncProcessor::applyForSyncType()` writes both 1 in its separate-sync branch
and neither in its csync branch, so on composite they hold whatever the last
separate-sync excursion left. A unit that has never been on separate sync
since boot runs composite with them 0. `SP_HS_POL_ATO` is the retiming
module's hs polarity auto-correct, and a NOR'd csync inverts polarity through
the vertical interval -- which is exactly where the count collapses.

**THE POLARITY PAIR IS REFUTED.** Put back to 1 with the phase left at 13, a
900 s run gives one shear, one burst and the same `vt=99..308` collapse. So the
one-branch write is still an unexplained divergence worth closing on the
convention, but it does not carry this fault.

**`PA_SP_S` is what is left**, and it is the sync processor's phase. The phase
search lands somewhere different on every acquisition, which is the shape of a
fault that changes between acquisitions and never within one. `PA_ADC_S` read 0
in both states, so the ADC sampling phase is not involved.

| `PA_SP_S` | `ADC_SOGCTRL` | `SP_*_POL_ATO` | duration | shears | bursts |
|---|---|---|---|---|---|
| 16 | 12 | 1/1 | 3600 s | **0** | **0** |
| 13 | 11 | 0/0 | 900 s | 1 | 2 |
| 13 | 11 | 1/1 | 900 s | 1 | 1 |

## The sync processor's phase is the lever, and the search selects the bad one

`PA_SP_S` decides it, measured on `vga` at 320x256@50 into 1920x1080,
composite, with the width mode and the emitted frame on one clock:

| `PA_SP_S` | `ADC_SOGCTRL` | `SP_*_POL_ATO` | duration | shears | bursts | count collapse |
|---|---|---|---|---|---|---|
| 16 | 12 | 1/1 | 3600 s | 0 | 0 | no |
| 13 | 11 | 0/0 | 900 s | 1 | 2 | **99** |
| 13 | 11 | 1/1 | 900 s | 1 | 1 | **99** |
| **16** | **11** | **1/1** | **900 s** | **0** | **0** | no |
| **13** | **11** | **1/1** | **900 s** | **1** | **1** | **100** |

The last three rows are an A-B-A moving `PA_SP_S` alone: fault, clean, fault.
Across the set, 4500 s at 16 carries nothing and 2700 s at 13 carries three
shears, four bursts and three count collapses -- 99, 99 and 100. The same rate
would have put about five events in the quiet time.

An A-B-A rather than two legs because every comparison earlier in the session
was undermined by the rate moving on its own; moving one register back and
forth is what separates the register from the drift.

**13 IS WHAT THE SEARCH CHOOSES AND 16 IS WHAT IT LEAVES.**
`Adc::phaseSyncProcessor_` is 16 at rest, and `Adc::acquirePhase()` returns
before `choosePhaseSyncProcessor()` when the sweep is not good enough, so the
default stands. A successful sweep takes
`middleOfWidestCleanRun()` of a band this bench measures at 10..16, whose
middle is 13. So the search is not failing to prevent the fault. It selects it.

**ITS CRITERION CANNOT SEE THIS FAULT.** Each phase is scored on how far
`STATUS_SYNC_PROC_HTOTAL` departs from `PLLAD_MD` over
`SamplesPerPhase` readings -- the stability of the LINE LENGTH. The sync
processor counts in ADC clocks against a PLL locked to hsync, so that register
reports the divider back whatever the vertical separation is doing, and the
scan is blind to the count collapsing to 99. A phase can score a perfect zero
on it and lose vertical sync once a minute.

`the-sampling-phase-cannot-reach-the-shimmer.md` records the search being tuned
against a different symptom, so a change of criterion has to be measured
against both.

## What is open

Which part of the composite path holds the ADC PLL at three-quarters lock --
though the Wii leg makes this a side question rather than the fault.

**THE LIVE CANDIDATE IS THE TWELVE SINGLETON `hlow_len` READINGS.** They are
the only outliers the tearing does not explain: measured against the frame
period the interval's own readings find, they sit at a phase concentration of
0.151 against a control of 0.124, so they fall anywhere in the frame, where the
crossings sit on the interval at 0.906. One of them -- `hlow_len` 1 -- is the
sample immediately before the shear, 30 ms ahead of the 337 count. Ten
occurrences in 27207 samples at 1 ms is one line in 420 sampled, so the rate
they really occur at is unknown and needs an instrument that reads the register
rather than the interval.

Whether the 74% lock duty is the shear's cause or its companion. The duty does
not dip at the event: 0.738 inside the window against a 0.743 run mean. So the
proximate trigger is the 29-line count anomaly, and the lock duty is so far a
property of the arrangement rather than of the event.

`STATUS_SYNC_PROC_HTOTAL` reads 2200 in every sample of both legs, which is not
evidence either way: it counts ADC clocks per line against a PLL locked to
hsync, so it echoes the divider by construction and can see a frequency error
but not a phase one.

## The instruments

`picture_shear.py` reads the displacement per band and, with `--rows`, the ramp
itself. `sampling_stats.py` queues the log at 1 ms, holds the socket, and flags
an outlier that decomposes as a byte crossing.
