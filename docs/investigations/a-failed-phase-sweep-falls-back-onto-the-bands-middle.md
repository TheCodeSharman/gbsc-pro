# A failed phase sweep falls back onto a seed inside the band

`PA_SP_S` 8 after an input change is not a phase the search chose. It is the
value the sweep was entered at, restored because the sweep **failed**, and on
the pass-through path that value is a setup constant sitting inside the
dithering band the sweep had just measured.

## What the device says

34 sweeps over 16 ypbpr -> vga round trips, read off `/dev/ttyUSB0` with the
`phase scan:` profile beside each outcome:

| outcome | count | clean steps in the profile |
|---|---|---|
| `sampling phase: chosen` | 17 | 16..17 |
| `sampling phase: no clean window` | 17 | 13..16 |

**`SweepGoodEnough` is 17 of `SweepSteps` 34 -- exactly half the sweep -- and
this source scores 13..18.** So the gate sits on the source's own clean count
and half the sweeps fail. The two counts overlap at 16 because `clean` counts
STEPS and the sweep is two steps longer than the field, so the two phases
walked twice score twice.

Every profile taken, failed or chosen, puts the band at about **5..22**:

```
chosen           clean 17  ......17339cd76b75a39...........
no clean window  clean 14  ......21339c9ea3946a1211........
no clean window  clean 16  ......24555eea6bc47931..........
```

## Why the failure costs the picture

On `clean < SweepGoodEnough` the sweep restores the phase it was entered at,
so that a refusal leaves nothing the engine did not choose. **On the
pass-through path what it was entered at is a seed, not a previous answer**:
`Adc::SetupPhaseSyncProcessor` and `HdBypass::EntryPhaseSyncProcessor` are both
**8**, and 8 is inside the 5..22 band on every profile above.

So a leg whose every retry in `SourceMaintenance`'s window failed is left on
the worst phase the field offers, and once the window closes that value stands
for the life of the lock. Measured 0.22..0.36 px of per-line wander there
against 0.117 outside the band.

The retries are visible: one leg ran eight sweeps in 3.5 s, every one
`no clean window`, and `PA_SP_S` read 8 afterwards.

## The displaced band is real, and the pad does not cover it

**The band 16..29 that
`the-sampling-phase-is-searched-before-the-source-settles.md` measured is
genuine, and a sweep taken with the pad DRIVEN scores it.** It is the FIRST
sweep of an arrival that scores it, whatever pass that sweep runs at.

Measured on vga at 1280x720@60 into pass-through, 62 sweeps logged with their
pass counts:

| sweep of the arrival | displaced / total | bands |
|---|---|---|
| first | **6 of 11** | 15..29, 16..28, 16..29, 17..29 against 5..22 |
| every later one | **0 of 51** | 4..24 |

Within one arrival it corrects on the next sweep: first 16..29, second 6..21,
and the remaining four 6..22. So the displacement is transient and lasts
exactly one sweep.

**`!syncOutAway()` is therefore NOT a sufficient settle criterion**, which is
what the 34-profile reading above concluded and what it got wrong -- those
profiles were taken through a round trip that leaves the previous source
selected long enough for a sweep to have already run.

**THE PASS COUNT IS A CORRELATE AND NOT THE RULE.** `PhaseFirstPass` moved
from ten to twenty on the reading that the displacement followed the pass,
since the sweeps at twenty and later all read the settled band. It measures 4
of 6 bad: at twenty the sweep is then the first of its arrival. The
displacement follows the sweep's ORDER.

## What the two changes were, and what they measure

**The run decides, not the clean count** (`c6573d667`). Moving
`SweepGoodEnough` would have been tuning a threshold until the gate passed;
deleting it is not, because the question it was asked -- is this a window or is
it scatter -- is answered by the widest clean RUN, which the refusal branch
already computed and used only to blame the clock. So the run decides
acceptance and `SweepGoodEnough` goes.

**Genuine scatter still refuses and still restores the entry phase**, because
there no phase read the divider: `middleOfWidestCleanRun()` answers `MidField`
out of noise, which is the same hardcoded fallback one layer along. That case
did not occur once in the 34 sweeps.

**And nothing is searched inside a transition** (`17aed2510`). The sixteen
pad-away sweeps are what scattered, and `acquireSamplingPhase()` now refuses
while the output is away -- in the search rather than at its callers, the
sketch having three of its own and the cadence not expressing it, the window
opening ten passes into the run where the pad returns after
`MinimumSyncAwayMs` fifteen. Deferring costs no picture: `showOutput(true)` is
unconditional and ignores what the search returned, and the sweep sat inside
the pad-away window, so leaving it out presents sooner.

That retired the provisional machinery -- `phaseSettled_`, `phaseSearched_`
and the three cases that pinned an early answer being replaced -- on the
premise that every answer held was then settled by construction. **That premise
is refuted by the table above**, and removing the machinery is what made the
first sweep's answer final: `samplingPhaseFound()` became true on the first
success, and `SourceMaintenance`'s window closes on it.

### The soak

| build | result |
|---|---|
| no provisional rule | 2 of 2 bad |
| `c3d2872b4`, provisional on the acquired run | 3 of 10 bad, re-read 1 of 3 |
| `ce9098f38`, settledness on the sync pad | 3 of 12 bad |
| **`c6573d667` + `17aed2510`** | **0 of 24 bad**, two runs of 12 |

The spread closed with it: every leg held 29, 30 or 31, against 8, 26, 29, 30
and 31 before. The build it replaces failed about a quarter of its legs, so 24
clean legs is the reading rather than a quiet run.

## The search is not choosing badly -- its MEASUREMENT is displaced

**THE SCOREBOARD ITSELF MOVES ALONG THE PHASE AXIS, AND THE SEARCH THEN PICKS
THE WIDEST CLEAN RUN CORRECTLY.** Measured on vga at 1280x720@60 into
pass-through, 43 sweeps captured off the console across 80 input round trips,
34 of them on vga, reading the first and last scored position of each board:

| population | band starts at | width | sweeps |
|---|---|---|---|
| settled | 4, 5, 6, 7 or 8 | 15..19 | 29 |
| displaced | **14 once, 16 four times** | 12..15 | 5 |

Nothing starts between 9 and 13. The displaced boards are as CLEAN as the
settled ones and their band is the same shape -- so the walk is scoring a
healthy source, and the clean run it then picks from is simply the one at the
LEFT, which is where a `PA_SP_S` of 4, 5, 6 or 7 comes from.

**The displaced profile IS the settled profile, shifted about eight of the
thirty-two positions.** Cross-correlating the mean scores:

```
settled    .......1247999668896331.........
displaced  ................2449#####9561...
settled+8  ...............1247999668896331.     r = +0.878
```

Shifts of 7 and 9 give r = 0.850 and 0.858, so the quantity is about a quarter
of the phase range rather than exactly eight.

**`SP_RT_HS_SP` IS REFUTED AS THE CAUSE.** The capture counter's origin is the
obvious suspect, the phase being scored on how steadily the sync processor
counts the line. It is identical on both populations -- `PLLAD_MD` 2038 and
`SP_RT_HS_SP` 32 on every displaced sweep and on every settled one, measured
with both printed beside the board for exactly this question.

**What shifts it is not established.** Five boards landing on two adjacent
starts, with a clear gap below them, is a QUANTISED displacement rather than a
drift, and that is what any candidate has to explain.

Nine further sweeps in the same capture are NOT this fault: a band only two to
five wide at 22..26, `SP_RT_HS_SP` 1959, oversample 2. They are the Wii on
ypbpr, the input each leg transits through.

## The two-sweep rung, and why it was reverted

`SweepsBeforeAnswerHolds` was two: `samplingPhaseFound()` reported unfound
until an arrival had had two successful sweeps, leaving `SourceMaintenance`'s
window open for the second. It is reverted, and the row for it in the bisect
table below is the one reading that does not survive.

**It is a recovery rung and nothing derives the two.** The conventions already
rule that out -- a fault is fixed by deriving the right value, not by trying
one and backing off -- and the rung's whole case was a measurement.

**THAT MEASUREMENT WAS BLOCKED A-THEN-B AGAINST A FAULT WHOSE RATE DRIFTS.**
Spread over the hours that followed, a build behaviourally equal to one that
had read 4 of 12 bad read 0 of 8, with no firmware difference between them, and
manual input switching at the bench in the same spell switched cleanly too. So
the fault goes quiet on its own and a comparison taken across one measures the
drift.

**The within-arrival table above is not affected by that**, because it compares
sweeps inside the SAME arrival: the first against its own later ones, minutes
apart at most. It is the cross-build rows that cannot be read.

**So the measurement discipline is the finding.** Run the baseline arm and
confirm it FAILS before scoring anything, and ALTERNATE the arms leg by leg so
each pair sits inside the same two minutes. A knob that switches arms at
runtime is what makes alternating cheap -- it costs no reflash, no reboot and
no re-acquisition between arms.

### The bisect

Same recipe, same bench, same source, `PA_SP_S` read 25 s after the switch
back; 29..31 is the settled band's answer and 5..19 is the displaced band's.

| build | result |
|---|---|
| `0c8443e22`, before the three | 30, 31, 31 |
| `c6573d667`, the middle of the three | 31, 31, 30 |
| `e6241e199`, the tip | **6, 7, 7** |
| `PhaseFirstPass` 20 | **6, 31, 30, 6, 6, 5** |
| two sweeps before the answer holds | 31, 30, 30, 30, 30, 30 -- **withdrawn, see above** |

The first four rows are four runs spread over hours and consistently split, so
the bisect stands and `17aed2510` is the commit. The last row is one block of
six taken against no interleaved baseline, and it is the reading the revert
withdraws.

### The reproduction is not the menu

An `/input` round trip that selects the other source and switches back about
three seconds later reproduces it on its own. The described menu is not the
trigger, and neither is the previous source failing to report a count -- one
leg switched back with no count read at all and landed on 31.

The earlier 24-leg soak reading 0 of 24 waited for the previous source to
report a count before switching back, which spends long enough on it for a
sweep to have run.
