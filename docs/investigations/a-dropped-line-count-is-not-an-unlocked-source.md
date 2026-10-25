# A dropped line count is not an unlocked source

The Wii at 480p on `ypbpr` re-locked about twice a second. The console carried
96 `source acquired` and 88 `source UNLOCKED` lines in 45 s, alternating, with
the count steady at 525 and the divider, the coast pair and every colour
register correct throughout.

## What the sync processor does on this source

`STATUS_SYNC_PROC_HTOTAL` leaves the divider for 25 to 75 ms, about twice a
second. Sampled from inside `loop()` at 25 ms over 30 s:

| state | samples | HTOTAL off the divider | episodes | longest |
|---|---|---|---|---|
| `ypbpr`, automation running | 681 | 14.7% | 64 | 3 samples |
| `ypbpr`, automation FROZEN | 1122 | 14.6% | 104 | 3 samples |
| `vga` 320x256@50, composite | 1101 | **0.0%** | 0 | — |

It reads 6 to 21 against a divider of 1446, beside a `STATUS_SYNC_PROC_HLOW_LEN`
of 104 — a sync low period longer than the whole line it is read next to.

**The frozen column is what settles the cause.** With nothing written, no phase
search and no arm, the collapse runs at the same rate. Nothing the engine does
provokes it, so the phase search re-latching 32 phases per cycle is a
consequence of the verdict and not its cause.

**The lock bit is not the discriminator.** The healthy `vga` source reads
`STATUS_MISC_PLLAD_LOCK` set in 75.7% of samples with 16.3 transitions a second
against the Wii's 78.0% and 6.6, and never once loses the line. A bit that is
noisier on the good source cannot separate the two.

## What the engine did with it

The horizontal half of the presence verdict forgave no dropped reading: one
sample of a line the divider does not clock set `SourceUnlocked`, where the
vertical half holds its verdict for `AbsentPasses`. The state is what zeroes the
acquired run, and every maintenance threshold counts in that run —
`ClampReadyPasses` 4, `CoastReadyPasses` 7, `AutoGainReadyPasses` 91, and the
sampling phase's window at `PhaseFirstPass` 10 every `PhaseEveryPasses` 10. A
collapse every 0.47 s holds the run under about 23 passes for ever, so the
phase search re-runs on each cycle and nothing keyed past 23 is ever reached.

Both halves forgive a dropped reading for the same run now. The measured worst
episode is 75 ms against a run of `AbsentPasses` × `DetectionIntervalMs` =
320 ms, and the source's own behaviour is unchanged by the fix: 16.4% of
samples in 108 episodes afterwards, with the console silent for 30 s where it
had carried 184 state changes in 45 s.

## What it does and does not explain about the magenta

The picture is magenta before and after the fix, so the colour is not the
verdict's doing. `auto-gain` reads `0` in the stored preferences, so auto gain
had never run on either bench source and could not be starved by anything;
51/51/51 is `ColourSpace::ComponentGain`, the component colour space's
prescribed resting value, as 123/123/123 is `ColourSpace::RgbGain` on `vga`.

**What the fix does buy is the ability to reach it at all.** The sketch gates
auto gain on `acquiredPasses() > 90`, which is 1.82 s of unbroken acquired run,
and the collapse held that run under about 23 passes for ever. With the verdict
forgiving the drop, turning the preference on settles the gains at 243/243/243
and the picture comes out correct. `known-issues.md` carries the resting value
that is left open by that.
