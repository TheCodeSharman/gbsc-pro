# Leaving bypass needs a count the divider cannot give

Reproduced on the bench, 2026-09-13, by accident: drive the RISC PC to 1920x1080
on `vga`, then back to 320x256. The unit enters bypass on the way up and **never
leaves it on the way down**.

## What it looks like

The console, for as long as you care to watch:

```
evt,31292746,rgbhv-leave-bypass,251,15
evt,31293375,rgbhv-leave-bypass,250,15
evt,31293461,rgbhv-leave-bypass,255,15
evt,31293505,rgbhv-leave-bypass,247,15
...
```

about nine times a second, with the line count wandering 234..267 and the
standard byte stuck at 15.

Registers at the same moment:

| | reads | should read |
|---|---|---|
| `PLLAD_MD` | 2039 | 2208 |
| `STATUS_SYNC_PROC_VTOTAL` | 239..267, never still | 311 |
| `STATUS_MISC_PLLAD_LOCK` | 0 | 1 |
| `OUT_SYNC_SEL` | 1 | 0 |

## The deadlock

**The leave-bypass arm is not gated out — it runs.** It fires on every pass and
gets nowhere, which is a different fault from the one
`the-rgbhv-ladder-is-the-only-one-on-that-path.md` describes, and it is worth
keeping the two apart:

- the arm needs a **settled line count** before it will act;
- the count comes from the sync processor, which counts in ADC clocks;
- the ADC PLL is still divided for the mode being LEFT — 2039 suits a 67.5 kHz
  line, the source is now 15.6 kHz — so it does not lock and the count wanders;
- nothing re-derives the divider, because that is what the solve does and the
  solve is what the arm is trying to reach.

**The divider is an input to the measurement that is supposed to replace it.**
That is the shape of the fault, and it is the same circularity as
`docs/sync-type-selection.md`'s: a decision taken through state that only reads
correctly once the decision is already right.

## What does and does not clear it

| tried | result |
|---|---|
| waiting 50 s with the source settled | no change |
| `/sc?~` | **no change** — it does not recover this |
| `/input?src=vga` | no change |
| `/sampleclock?md=2208&os=4` | **clears it at once** — PLL locks, `VTOTAL` settles to 311, the engine acquires at 15625 and the picture comes back |

**`/sc?~` failing here is the notable half.** `CLAUDE.md` carries it as the
recovery for a stuck divider, and that entry is about a divider stuck on the
scaling path. A divider stuck while the output is in BYPASS is not the same
state and the same recovery does not reach it.

`/sampleclock` works because it writes the whole ADC clock group through the
call the bypass switch makes and restarts the PLL afterwards — it supplies the
divider the solve would have chosen, from outside the loop that cannot reach it.
It is behind `GBS_DEBUG`, so it is not a recovery a user has.

## Why it matters to the plan

A measurement-driven escalation has to be able to run when the measurement is
the thing that is broken. The reference sampling clock exists for exactly this —
`SourceMeasurement::applyReferenceSampling()` puts a known divider on the part
so a count can be taken through something other than the last mode's clock — and
the leave-bypass arm does not use it.

**The first reading taken after a source event must be taken through a reference
divider, not through the divider the previous mode left.** That is already the
rule on the scaling path. Bypass is the path it has not reached.
