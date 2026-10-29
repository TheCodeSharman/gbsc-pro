# The phase walk's dirty band has two positions and the ARRIVAL KIND picks which

`Adc::acquirePhase()` walks `PA_SP_S` across its field, scores each phase by
how steadily the sync processor counts the line, and takes the middle of the
widest clean run. On a 1280x720@60 pass-through source it sometimes picks a
phase the picture is visibly worse at.

**The pick is entirely explained by where the walk finds the dirty band, the
band has exactly two positions, and which one it lands in is decided by how
the source arrived.** Measured on `vga`, RISC PC at 1280x720@60,
pass-through, `PLLAD_MD` 2038, `SYNC 0`, eight pairs run strictly alternating:

| arrival | band centroid | band | clean run | run's middle | picks |
|---|---|---|---|---|---|
| **a mode change on `vga`** | 14.0..14.8, 8 of 8 | 6..22 | 23..5 | 30 | 28..31, good |
| **an input change via `ypbpr`** | 22.1..22.8, 8 of 8 | 15..29 | 30..14 | **6** | 5..7, the fault |

**Perfect separation with no overlap**, which a chance split of sixteen
arrivals reaches once in 12870. A band eight phases further on moves the
widest clean run's middle from 30 to 6, and that is the whole of the fault.

**SO IT IS NOT A LOTTERY, AND THE RATE IS A MIXING RATIO.** A harmful pick in
about one arrival in five is the proportion of input changes in whatever was
run, not a per-arrival draw: 20 of 23 consecutive input-change arrivals landed
in the second position and 8 of 8 consecutive mode-change arrivals in the
first, where an independent draw at one in five reaches the first of those
once in 10^11. **An experiment that compares two conditions in BLOCKS will
therefore charge a run boundary to the condition**, which is how settling came
to look like a cause -- see below.

The centroid is the summary two runs are compared on: circular, weighted by
the per-phase miss counts that `dither[]` already holds.

**A recorded centroid stability of 13.7..14.9 over eight re-acquisitions is
not contradicted, it is one arrival kind seen alone.**

## The settled surface does NOT have two positions

Deep sampled from `loop()` at 25 ms, about 140 readings per phase, `PA_SP_S`
held by `/phase?sp=`, same source and divider:

| phase | 0..5 | 6 | 7 | 8 | 9 | 10 | **11** | 12 | 13..19 | 20 | 21 | 22 | 23..31 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| share of samples reading `HTOTAL` exactly | 1.000 | .936 | .871 | .818 | .745 | .691 | **.572** | .618 | .75..78 | .867 | .929 | .978 | 1.000 |

That reproduces the picture-measured arc -- good 23..5, harmful 7..22 by
grating contrast -- to within one phase at both edges, with no camera.
`STATUS_SYNC_PROC_HLOW_LEN`'s spread discriminates INSIDE the plateau where
the exact share saturates: 0.000 at 30, 31 and 0..4, rising to 0.485 at 23,
and the three phases that measure best on the picture are exactly the 0.000
group.

**The same sweep on a settled arrival and on one where the walk had just
chosen 7 differs by at most 0.125**, with the clean sets identical but for one
phase and the band at 6..22 both times. Applying the walk's own binary rule to
either surface gives 30.

**So the surface does not move between arrivals, and only the walk does.** The
bimodality is a property of the instrument, not of the chip's state.

## Nothing written to the chip moves the band

Every arm interleaved with controls, each write confirmed by read-back, band
centroid as the score, all on arrivals in the common mode:

| arm | centroid, against controls at 14.0..15.0 |
|---|---|
| `SFTRST_SYNC_RSTZ` pulse | 14.8, 14.1 |
| `SFTRST_DEC_RSTZ` pulse | 14.5, 14.7 |
| `/sampleclock` PLL re-latch at the same divider | 14.6, 14.7 |
| `PA_SP_LOCKOFF` = 1, latched | 14.2 |
| `PA_ADC_LOCKOFF` = 1, latched | 14.2 |
| `PLLAD_R` 0 / 1 / 2 / 3 | 14.7 / 14.2 / 14.1 / 14.6 |
| `PLLAD_S` 0 / 1 / 2 / 3 | 15.1 / 14.4 / 14.8 / 14.1 |
| `SP_RT_HS_SP` written 1959 | 14.4, 13.9 |
| `SP_RT_HS_SP` 1959 with the sync processor re-locking on it | 14.4, 13.9 |

The last two matter because the capture origin is derived from the measured
sync width -- `PLLAD_MD - STATUS_SYNC_PROC_HLOW_LEN + lag` -- and an arrival in
the second mode runs its early sweep at `rt` 1959, which back-solves to a
`HLOW_LEN` of about 163 against this source's 52. **The chain is real up to the
last link and then stops: the origin does not place the band.**

`PA_ADC_LOCKOFF` reading inert here is latched, which is what retires the
objection that it had never reached the adjuster.

## The early sweep is not a defect

Arrivals in the second mode run an extra phase sweep at about 0.25 s where
common-mode arrivals almost never do -- 6 of 6 against 15 of 16 -- and
injecting one induces the second mode 2 of 3 against 0 of 13 elsewhere, p about
0.025. **With the state logged beside it the sweep is legitimate:**

```
0.25  sweeping for maintenance, rt 1959 md 2038 vt 524 solved 525 state 2 in 4/4 lock 1
```

`in 4/4` is the ypbpr selection against a solve FOR ypbpr, `state 2` is
acquired, and `solved 525` matches the count. `md 2038` is not the arriving
source's by coincidence -- the divider policy maximises kept samples under the
same 2047 counter cap whatever the source, so 480p lands near 2038 too. It is
the engine maintaining the OUTGOING source correctly while the input is being
switched away.

Both gates written to suppress it therefore guard a legitimate state, and the
second mode held at 2 of 16 with them in force.

**It is a MARKER of the arrival kind rather than a cause of the band.** An
input change is what leaves a maintenance sweep running against the outgoing
source, so the early sweep and the second band both follow from the same
excursion, and suppressing the sweep cannot move the band.

## The entry phase does not latch the sweep

The walk is strictly cyclic -- `phase = (phase + 1) & PhaseMax` -- so the
predecessor of every phase is `phase - 1` whatever the entry, and `SweepSteps`
34 scores the first two twice and keeps the second reading. A smear model in
`test_adc.cpp` passes without any change to the walk. **Do not re-propose that
the sweep self-latches from its entry phase.**

## SETTLING IS NOT IT, and it is the worked example of the block-comparison trap

The walk gives each phase `delayMicroseconds(256)` and 20 back-to-back
readings where the deep sweep gives two seconds and 140 spread at 25 ms, which
left settling as the last difference between two instruments that disagree.
It is eliminated.

`acquirePhase()` discards `settleSamples()` readings after the adjuster moves
before scoring, and `/phasewalk?settle=N` holds the count. Interleaved one
settle per arrival, so that a run boundary cannot land on a single point:

| settle, readings | 0 | 4 | 8 | 16 |
|---|---|---|---|---|
| band centroid | 22.4, 22.7 | 22.2, 22.2 | 22.5, 22.1 | 22.0 |

Every point lands in the same position, so the dwell does not place the band.
The default is zero.

**The trap is worth more than the result.** Taken in BLOCKS the same two
settles appeared to decide the band outright -- 13 of 16 arrivals in the
harmful position at a settle of 8 against 2 of 16 at a settle of 0 -- because
the two blocks were a run apart and a run is what the band actually has. The
same error, in the same session, made an arrival-kind comparison read backwards
until it was interleaved. **On this bench a comparison is interleaved or it is
not a comparison**, and that holds for anything scored per arrival, not only
for the settle.

## WHAT THE TRANSIT CARRIES IS OPEN, and the obvious diff has not been taken

The band is displaced by an input change and not by a mode change, so
something the `ypbpr` excursion leaves behind on `vga` moves it. What that is
is not established.

A register diff across one transit found every sync-path field identical --
`SP_SOG_MODE`, `ADC_SOGEN`, `ADC_SOGCTRL`, both coast lengths, `SP_H_CST_SP`,
`SP_RT_HS_SP`, `SP_CLP_SRC_SEL`, `SP_HS_INV_REG`, `SP_DLT_REG`,
`SP_H_PULSE_IGNOR`, `SP_EXT_SYNC_SEL`, both clamp edges, the divider and
`STATUS_SYNC_PROC_HLOW_LEN`. **That diff does not count**, because the arrival
it was taken across came out in the FIRST band -- `PA_SP_S` 30 -- so it
compares two arrivals of the same kind. A transit is only an input-change
arrival if the other source actually acquired, and a wait that times out and
carries on leaves a short excursion that looks like one.

So the measurement to take is the same diff across a transit CONFIRMED to have
landed in the second band, read by its `phase scan:` line rather than assumed,
and interleaved with a mode arrival for the control. A full
`snapdiff.py --save` at both ends answers it over all 1536 addresses rather
than over a chosen seventeen, which matters here because the chosen seventeen
were chosen from a model of what a `ypbpr` excursion ought to leave.

## Instruments

- **`sampling phase: sweeping for <reason>, rt .. md .. vt .. solved .. state
  .. in <selected>/<solvedFor> lock ..`** is the pre-sweep context line.
  `state` is 0 absent, 1 unlocked, 2 acquired. It settled in one arrival what
  sixteen arrivals of outside inference had not.
- **`phase scan:` decodes to the per-phase miss count** -- `.` is zero, digits
  1..9, lowercase 10 and up, UPPERCASE a phase further off than a single
  count. Its `md` and `rt` are read AFTER the walk, so they are not necessarily
  what it ran against.
- **The console drops lines in a burst**, so a `phase scan:` with no
  `sweeping for` line above it is a dropped line rather than a second code
  path.
- **Polling a register over HTTP cannot time a sub-second window.** An
  injection triggered at a fixed 0.25 s lands 3 times in 8, and triggering on a
  polled `SP_RT_HS_SP` is no better. Put the information in the firmware log.

## Refuted, with the evidence that killed each

| model | what refutes it |
|---|---|
| the band's position is chip state a reset can clear | nine arms above, every one at the common-mode centroid |
| the capture origin places the band | `SP_RT_HS_SP` 1959 written, and re-locked on, both at 14.4 |
| the sweep self-latches from its entry phase | the walk is cyclic and scores its first two phases twice |
| the early maintenance sweep is a bug to gate out | its logged state is a correct solve for the outgoing source |
| the surface itself is bimodal | two deep sweeps on opposite picks differ by 0.125 |
| the walk's settle places the band | 0, 4, 8 and 16 readings interleaved, all at centroid 22 |
| the band is a per-arrival lottery | the arrival KIND separates it 8 of 8 against 8 of 8 |
