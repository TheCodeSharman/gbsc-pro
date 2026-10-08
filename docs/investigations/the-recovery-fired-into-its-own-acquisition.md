# The recovery ladder fired into the acquisition it was recovering

Twelve named rungs sat at fixed positions on a counter of failed detection
passes. A pass is `DetectionIntervalMs`, 20 ms, so the positions were times
whether they read as times or not:

| rung | position | time |
|---|---|---|
| `LiftSogFloor` | 2 | 40 ms |
| `CoastWindow` | 8 | 160 ms |
| `SyncProcessorDynamic` | 27 | 540 ms |
| `ReleaseCapture` | 32 | 640 ms |
| `HoldClamp` | 34 | 680 ms |
| `NudgeModeDetect` | 38 | 760 ms |
| `ReprobeSyncType` | 44 | 880 ms |
| `HsyncOverflowProtect` | 48 | 960 ms |
| `RestartSamplingClock` | 60 | 1.2 s |
| `FullReset` | 150 | 3.0 s |
| `ToggleInput` | 413 | 8.3 s |
| `ReopenSogSeparator` | 450 | 9.0 s |

A mode change is shown in 1.2..2.0 s and a component selection in 4.4..6.9 s.
**So every rung to `FullReset` fell due inside an ordinary acquisition**, and the
whole list cycled in 9.0 s — the input toggle moving the ADC mux every nine
seconds on a source that had not arrived.

## The race, measured

A `ypbpr` leg that then failed:

    2.15  sampling: 263 lines x 59.00 Hz -> line rate 15576
    2.16  sampling: rate 15576 doubled 1 -> divider 2200
    2.18  recovery: coast window at pass 8
    2.28  sampling: 269 lines x 70.15 Hz -> line rate 18941
    2.80  duty: 243 pulse / 2200 divider, htotal 3268, negative, UNLOCKED

The engine had the source measured and its divider installed, and the rung reset
the coast window and discarded the placement 20 ms later. The first reading
lands at 1.8..2.7 s and pass 8 at 1.84 s, so **which came first was decided by
milliseconds** — which is why one build gave 4.4..6.9 s on some legs and nothing
within 32 s on others, and why four variants were once compared against noise
and three conclusions withdrawn.

Both measured mode changes fired `lift SOG floor at pass 2`, `coast window at
pass 8` and one of them `sync processor dynamic at pass 27`, inside a change that
completed in 1.2..1.8 s. Nothing broke; nothing was gained either.

## A first-acquisition grace is not the answer, and nor is a hold

`FirstAcquisitionGraceMs` held the position below the first disruptive rung for
15 s. It left the CONFIGURING rungs withheld too, which is what a sync-on-green
source needs before the sync processor can count it at all, so it was relaxed to
let those through — and the race above is what that relaxation exposed.

**A 3 s hold before the first rung was tried and is not the answer**: it did not
improve the success rate and made `vga` slower, 5.0..17.3 s against 4.1..7.8 s.

## The last rung bricked the separator

`ReopenSogSeparator` called `SyncOnGreen::choose(0)`, which `SyncOnGreen.h`
records as *"the sync separator fully open and the one value no ratchet can climb
back out of"*. Any leg that failed for 25 s reached it, and every leg after that
failed too: `ADC_SOGCTRL` 0, `STATUS_SYNC_PROC_VTOTAL` 97, `VPERIOD_IF` a
correct 524 beside it. `/sc?~` does not recover it, because the held level is
what `apply()` writes back; `/restart` does, detection choosing
`ComponentLevel`.

So a measurement run had to cap its legs below 25 s or it poisoned itself, and a
poisoned run reads exactly like the change under test having broken the input.

## What replaces it

Three acts against the acquisition BUDGET, on a wall clock: reconfigure at 10 s,
reset the blocks at 20 s, move the mux at 30 s, cycling. They are the three acts
no reconfigure can perform for itself.

A wall clock rather than the pass count because passes are missed wherever
`loop()` stalls, so a count under-reads how long a source has been missing — and
the budget is what the acts are timed against. An act gets a selection's worth of
time to show its effect, because an act is a selection's worth of work.

**The ordering replaces the grace.** The act that disturbs nothing comes first
and the teardown lands at 20 s, past where the grace ended, so there is nothing
left for a grace to defer.

### What went, and the measurement for each

| rung | why |
|---|---|
| `LiftSogFloor` | a no-op on both bench sources: `liftOffFloor()` returned early above `LowestSteppable` 2 and the level is 13. `reacquireSeparator()` walks from the level the input is due, which subsumes it |
| `CoastWindow`, `HoldClamp` | repeat what a selection applies, and each discards a placement through `forgetPositions()` |
| `SyncProcessorDynamic` | the tick applies these |
| `HsyncOverflowProtect` | a blind toggle of a field `applyForSyncType()` owns |
| `ReopenSogSeparator` | parks the separator where nothing recovers it |

## The search configuration was never the selection's

`SyncProcessor::Dynamic` carried `searching` and `hunting`, and **nothing a
caller knows could decide between them**: a source with no measured line length
wants the windows the search places, whatever the reason it has none. The
selection asked for the lesser answer — the pulse-width difference alone — so the
only writer of the coast window on a source nothing could count was a rung.

Measured on the Wii in 480i over `ypbpr`: the ADC PLL locks at the reference
divider inside two seconds and `STATUS_SYNC_PROC_VTOTAL` then sits at 97 — the
value it holds when it is not following the source at all — for **13.65 s** of
total console silence, until a rung wrote the search configuration and the
source acquired 1.6 s later.

One fact now: a searching source gets the search configuration, the selection
has it from the outset, and `takeBackSearchSettings()` takes it back on the
first count.

## What the collapse did and did not buy

Measured on the bench, RISC PC on `vga` and the Wii in 480i on `ypbpr`:

| | before | after |
|---|---|---|
| mode change, `X800 Y600 C256 F60` | 1.2..1.3 s | 1.2..1.3 s |
| mode change, `X320 Y256 C256 F50` | 1.6..2.1 s | 1.5..2.0 s |
| `vga` selection | 4.1..7.8 s | 5.0..6.0 s, 6 of 6 |

So the budget it most risked is unspent. What it did NOT buy is `ypbpr`
reliability: that turned out to be a separate defect the rungs had been masking,
and the collapse is what made it legible —
[`the-sub-coast-waited-on-a-measurement-it-made-possible.md`](the-sub-coast-waited-on-a-measurement-it-made-possible.md).
