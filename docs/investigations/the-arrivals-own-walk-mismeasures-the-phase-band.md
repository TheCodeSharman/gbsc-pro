# The arrival's own walk mismeasures the phase band, and a re-walk corrects it

`Adc::acquirePhase()` walks `PA_SP_S` across its field, scores each phase by
how steadily the sync processor counts the line, and takes the middle of the
widest clean run. After an INPUT change it picks a phase the picture is
visibly worse at, every time.

**There is one dirty band, at 6..22, and the walk run inside an input-change
acquisition reports it eight to ten phases further on.** Everything else
follows from that: the widest clean run's middle moves from 30 to 6, and 6 is
inside the real band.

Measured on `vga`, RISC PC at 1280x720@60, pass-through, `PLLAD_MD` 2038,
`SYNC 0`. Eight pairs of arrivals run strictly alternating:

| arrival | band the walk reports | clean run | run's middle | picks |
|---|---|---|---|---|
| **a mode change on `vga`** | 6..22, centroid 14.0..14.8, 8 of 8 | 23..5 | 30 | 28..31, good |
| **an input change via `ypbpr`** | 15..29, centroid 22.1..22.8, 8 of 8 | 30..14 | **6** | 5..7, the fault |

**Perfect separation with no overlap**, which a chance split of sixteen
arrivals reaches once in 12870. And the second reading is the wrong one: the
same state re-walked seconds later reports 6..22 and picks 28..30, four times
in four.

**SO IT IS NOT A LOTTERY, AND A ONE-IN-FIVE RATE IS A MIXING RATIO.** It is the
proportion of input changes in whatever was run, not a per-arrival draw: 20 of
23 consecutive input-change arrivals reported the displaced band and 8 of 8
consecutive mode-change arrivals the true one, where an independent draw at
one in five reaches the first of those once in 10^11. **An experiment that
compares two conditions in BLOCKS will therefore charge a run boundary to the
condition** -- which is how settling came to look like a cause, below.

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

## THE BAND DOES NOT MOVE. THE ARRIVAL'S OWN WALK MISMEASURES IT

This is the diagnosis, and it retires the whole bimodal framing above: there
are not two band positions that something selects between. There is ONE band,
at 6..22, and a walk run inside an input-change acquisition reports it eight
to ten phases further on.

Four `ypbpr`->`vga` arrivals, the arrival's own walk read off the console and
then the same state re-walked at once through `/phasesweep`, unfrozen, with
nothing else touched:

| trip | the arrival's walk | re-walked seconds later |
|---|---|---|
| 1 | 22.7, band 16..31, pick **8** | 14.6, band 6..21, pick **30** |
| 2 | 22.2, band 15..28, pick **6** | 13.4, band 5..31, pick **28** |
| 3 | 22.2, band 16..28, pick **6** | 14.7, band 7..23, pick **29** |
| 4 | 22.5, band 16..28, pick **6** | 14.1, band 5..23, pick **30** |

**Four of four, and the correction is total.** Every other instrument agrees
with the re-walk: the deep sampled surface puts the band at 6..22 on every
arrival taken, 21 walks on a frozen settled state all report 14, and the
picture's own dirty band at this divider is 7..22.

**So the harmful pick is not a phase the walk chose from a moved band. It is a
phase chosen from a band that was never there.**

### What the acquisition is doing while it walks

Both arrival kinds walk AFTER `sync pad: driven`, so the pad ordering is not
the discriminator even though the convention calls a phase line after it a
sequencing fault. What the input change adds, and a mode change does not have:

```
  2.28  sync arrangement: composite or SOG for input 4
  2.89  sync arrangement: separate for input 4
  3.62  sync arrangement: separate for input 3
  ...
  4.74  rate match: the HD bypass channel carries the video
  5.04  sync pad: driven
  5.04  sampling phase: sweeping for maintenance, rt 32 md 2038 vt 749 ...
  5.24  phase scan: ..............1.32b7dhgjff822.1.  md 2038 rt 32 st 0
  5.48  rate match: the HD bypass channel carries the video
```

Three sync-type probes rather than one, the sync arrangement set for the
OUTGOING input and then the arriving one, and a second rate match landing
after the walk. The context line reports nothing wrong -- `md` 2038, `rt` 32,
`vt` 749, state acquired, selection and solve agreeing, lock 1 -- which is why
the walk has no way to refuse the reading it is taking.

**Which of those the walk is actually disturbed by is open.** The probe writes
`SP_EXT_SYNC_SEL` and the chip latches that as a SOG switch, which makes the
sync separator the first thing to ask about; nothing has been written to test
it.

### What this makes of the fix

The walk measures correctly a few seconds later on the same state, so the
value is reachable -- it is being taken at the wrong point in the sequence.
**The fix is to move the measurement to where everything it depends on is in
force, not to re-run it until it agrees.** A second walk layered on the first
is a ladder rung, and the convention this project is built on is that a fault
is fixed by taking the measurement once, at the right point.

## Narrowing it: the probe is out, and a divider excursion carries part of it

All interleaved with controls, all on `vga` at 1280x720@60 pass-through,
`PLLAD_MD` 2038, band centroid as the score.

**THE SYNC-TYPE PROBE IS NOT IT**, which was the standing candidate.
`/input?src=vga` issued while already on `vga` runs the whole selection path
and TWO sync-type probes, and the walk stays right; the same request after a
trip to `ypbpr` runs three and the walk is wrong. Four rounds:

| arm | probes | centroids | picks |
|---|---|---|---|
| a mode change on `vga` | 0 | 14.5, 13.9, 14.2, 14.1 | 30, 30, 30, 30 |
| **`/input?src=vga` while on `vga`** | **2** | 14.5, 13.9, 14.2, 14.7 | 30, 29, 30, 31 |
| `/input?src=ypbpr` then `vga` | 3 | 22.1, 22.5, 22.3, 22.1 | 6, 7, 6, 6 |

**And the excursion needs no dwell.** Four seconds on `ypbpr` displaces it as
far as a full acquisition does -- 22.2, 22.5, 22.0, 22.8 against 22.5, 22.3,
22.3, 22.6 -- so whatever carries it is established by the move rather than by
the other source being solved for.

**No register the selection writes reproduces it.** Each bounced on a settled
true-band state with the walk queued straight after, because the disturbance
is transient:

| arm | centroid, controls at 14.0..14.7 |
|---|---|
| `SP_SOG_MODE` 0 -> 1 -> 0 | 14.6 |
| `ADC_SOGEN` 1 -> 0 -> 1 | 14.4 |
| `ADC_INPUT_SEL` 1 -> 0 -> 1 | 13.4 |
| all three together, as a `ypbpr` selection sets them | 14.2 |
| `SP_SOG_MODE` LEFT at 1 | scatter at every phase |

The last arm is the control on the method: leaving the separator on sync on
green makes a source that has none uncountable, so the writes do reach the
hardware and a null from the others is a null.

### A DIVIDER EXCURSION CARRIES ABOUT HALF OF IT

A PLL re-latch at the SAME divider is eliminated. An excursion to the other
source's divider and back is not the same thing, and it moves the band:

| arm | centroid | band's low edge |
|---|---|---|
| controls, six of them | 13.7..14.7 | 5..7 |
| divider to 1446 and back, oversample 1 | **16.2** | **11** |
| the same at oversample 2 | **16.0** | **10** |
| the same at oversample 4 | **16.4** | **11** |
| coast 7/6 and back | 14.3 | 8 |

Three of three and about ten standard deviations clear of the controls -- but
**it is an artefact of the instrument, not a contributor to the fault**, and
two further arms say so. Putting ANY other write between the excursion and the
walk removes it: the same excursion followed by `/coast?clear` scores 14.5 and
followed by a `/refclock` install scores 14.3 and 14.1. `/sampleclock`
restarts the PLL and the phase adjusters, so what the +2 measures is a walk
run before that restart has settled, and the trailing write is the delay that
lets it.

**So it is a second, much smaller effect with the same shape as the fault, and
it must not be read as the fault scaled down.** Four phases against ten, and
removable by a delay the real one does not answer to -- see below.

**The PLL's reference is out as well.** `/refclock` installs the other
source's reference divider, which is what an input change does and
`/sampleclock` does not, and it moves nothing: 14.4 and 14.1 at reference 2506
against controls at 13.8..14.4, with the divider excursion added and without.

## The correction is NOT a settling time, which is what rules out waiting

If the arrival's walk were simply early, a re-walk would be wrong at first and
become right. It does not: timed from the moment `vga`'s count returns, the
FIRST re-walk is already correct, and nothing changes after it.

| | the arrival's walk | +0 s | +2 s | +5 s | +10 s | +20 s |
|---|---|---|---|---|---|---|
| trip 1 | 22.5 | **14.4** | 14.2 | 13.9 | 14.5 | 14.2 |
| trip 2 | 22.2 | **13.7** | 14.3 | 14.0 | 14.1 | 14.2 |
| trip 3 | 22.3 | **14.6** | 14.4 | 14.2 | 14.0 | 14.6 |

The +0 s walk lands 3.6 s after the count returns, which is about when the
arrival's own walk runs, and it is right while that one is wrong. **So the
difference is not WHEN the walk runs but what it is run BY** -- the
acquisition's call against `/phasesweep`'s -- and a fix that delays the walk
has nothing to wait for.

**That instrument now exists and has answered**: `/phasewalk?repeat=N` runs
the walk N times from whatever called it. Both walks of an input arrival are
wrong, so the fault is not the first walk after the adjusters restart -- it is
the calling context. See the section above.

## IT IS THE CALLER, NOT THE CLOCK: thirteen walks of one acquisition, all wrong

`acquirePhase()` repeats its field walk `walkRepeats()` times and reports every
scan, so both readings come from the SAME caller -- which is what the arrival
against `/phasesweep` comparison could not give. Held at 8, and the
acquisition sometimes runs it again, so up to 13 walks land inside one
arrival, spanning about four seconds:

| arrival | walk 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | .. 13 |
|---|---|---|---|---|---|---|---|---|---|
| mode | 14.4 | 14.2 | 14.9 | 14.5 | 14.0 | 14.4 | 14.1 | 14.4 | |
| input | 22.7 | 22.4 | 22.4 | 22.2 | 22.2 | 22.1 | 22.3 | 22.2 | |
| input, another | 23.8 | 24.6 | 24.6 | 24.2 | **22.7** | 22.4 | 22.5 | 22.2 | 22.1 |
| mode, another | 19.2 | **scatter** | scatter | scatter | **14.7** | 14.2 | 14.4 | 14.5 | 14.3 |

**Every walk of an input arrival is wrong and every walk of a mode arrival is
right**, and the run of thirteen covers four seconds. A walk from the idle loop
moments after the same acquisition is right. **So the disturbance is not
something that decays, at any scale between a third of a second and four
seconds, and waiting inside the acquisition does not reach it.**

This is what retires the settling account for good. An earlier reading that
the first re-walk is already right only bounded it above 3.6 s and below the
600 ms between two walks; thirteen walks close the middle.

**AND THERE IS A SEPARATE EARLY TRANSIENT, worth not confusing with it.** The
first three or four walks of an arrival report something else again -- a
narrow band at 23..25 on one input arrival, and pure scatter at every phase on
one mode arrival -- settling by walk 5 into the band that arrival then holds.
So an arrival's FIRST walk is unreliable in a second, independent way, and the
engine runs exactly one.

### What is left

The acquisition's walk is wrong and a walk from the idle loop is right, with
no register state reproducing the difference and no amount of waiting inside
the acquisition closing it. What distinguishes the two callers is what the
acquisition has not finished doing: on an input arrival a second
`rate match: the HD bypass channel carries the video` lands AFTER the walk,
where a mode arrival has one before the walk and none after.

That is the next thing to test, and it needs the rate match moved or
suppressed rather than a register set -- nothing in a dump describes it.

## NO REGISTER CARRIES IT, which is what leaves the walk itself as the fault

If the band really moved, something the `ypbpr` excursion leaves behind on
`vga` would have to carry it. Nothing does, and that is what points at the
walk.

A full 1536-address `snapdiff.py --save` either side of a confirmed
displacement differs in SIX bytes, against THREE between two arrivals of the
same kind: the three ADC gain registers, which drift in the control too,
`MADPT_Y_MI_DET_BYPS` 0 -> 1 and `MADPT_Y_MI_OFFSET` 0 -> 127, and `PA_SP_S`
itself, which is the walk's output rather than an input to it. **Written into
a true-band state and re-walked, none of them moves the band**: the gain at
139, the `MADPT` pair, and both together all score 13.6..15.3 against controls
at 13.8..14.9, every write confirmed by read-back.

An earlier seventeen-field diff found every sync-path field identical --
`SP_SOG_MODE`, `ADC_SOGEN`, `ADC_SOGCTRL`, both coast lengths, `SP_H_CST_SP`,
`SP_RT_HS_SP`, `SP_CLP_SRC_SEL`, `SP_HS_INV_REG`, `SP_DLT_REG`,
`SP_H_PULSE_IGNOR`, `SP_EXT_SYNC_SEL`, both clamp edges, the divider and
`STATUS_SYNC_PROC_HLOW_LEN`. **That diff does not count**, because the arrival
it was taken across came out in the FIRST band -- `PA_SP_S` 30 -- so it
compares two arrivals of the same kind. A transit is only an input-change
arrival if the other source actually acquired, and a wait that times out and
carries on leaves a short excursion that looks like one.

**Read the `phase scan:` line to confirm which band an arrival landed in
rather than assuming the excursion produced one.** A transit is only an
input-change arrival if the other source actually acquired, and a wait that
times out and carries on leaves a short excursion that looks like one.

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
| the band moves at all | re-walking the same state corrects it 4 of 4, and no register carries it |
| a register the transit leaves behind carries it | the 6-byte diff written back moves nothing |
| the sync-type probe disturbs the walk | a same-input reselect runs two and stays right |
| the SOG switch, the ADC mux or the coast carries it | each bounced against controls, all null |
| the excursion must solve the other source | four seconds on it displaces as far as a full acquisition |
| the PLL's reference carries it | `/refclock` to the other source's divider, null with and without the divider |
| the arrival's walk is merely early | the first re-walk is already right, at +0 s and at +20 s |
| the first walk after the adjusters restart is the wrong one | eight to thirteen walks of one arrival, all wrong |
| the disturbance decays and the walk must wait | thirteen walks over four seconds, no trend |
| the search's `SP_H_PULSE_IGNOR` 16 carries it | held at 16 against controls, null, twice |
