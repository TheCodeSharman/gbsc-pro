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

## What this supersedes

**The remaining bad legs are not searches that scored a transient band.** The
band 16..29 that
`the-sampling-phase-is-searched-before-the-source-settles.md` measured 0.6 s
after a bypass switch **did not appear in any of the 34 profiles here**, pad
away or pad driven. What those profiles do show is the pad state mattering to
the SPREAD rather than to the band's position: 16 sweeps with the pad away
scatter from 1..22 to 5..29, where all 10 with it driven read 6..22 to a unit.

So `!syncOutAway()` is a sound settle criterion and is not what is left. What
is left is a gate sized at half the sweep on a source that scores half the
sweep, and a fallback onto a constant the scan has just called dirty.

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

Every answer the engine holds is then settled by construction, which retires
the provisional machinery -- `phaseSettled_`, `phaseSearched_` and the three
cases that pinned an early answer being replaced.

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
