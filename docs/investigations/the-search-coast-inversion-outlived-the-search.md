# The search's coast inversion outlived the search

A `ypbpr` selection from a 37.9 kHz predecessor took 24..25 s, every time, and
the wait was a fixed ladder POSITION: `recovery: full reset at pass 150` ran and
the source acquired half a second later. Three sessions asked which act of
`FullReset` recovers the ADC PLL. None of them does. The PLL was carrying a
broken reference, and `FullReset` happened to be the first rung that stopped
breaking it.

## The register that did it

`SP_COAST_INV_REG` inverts the coast gate -- the datasheet calls it "Out control
Coast invert". Upright, the gate brackets the vertical interval, so the block
coasts over a serrated source's serration pulses instead of counting them.
Inverted, it brackets the active line: the serrations are counted as lines and
the hsync reaching the ADC PLL is interrupted every line.

`SyncProcessor::applyForSearch()` set it, and `applyDynamic()` returned before
the line that cleared it:

```
if (source.searching) {
    if (source.hunting) applyForSearch(source.csync);   // set it
    else applyPulseWidthDifference();
    return;                                            // never reached the clear
}
if (source.csync) setCoastInvert(false);
```

Upstream's `updateSpDynamic()` has the identical shape, and the same block's
other leftover -- `SP_H_PULSE_IGNOR` 0x02 -- was fixed one session earlier.
`the-pulse-ignore-value-is-measured-not-chosen.md`.

## What the measurements say

A full `snapdiff.py` either side of the stall: every field of the ADC PLL group
is byte-identical, so the PLL is not mis-set. The differences in the sync path
are `SP_COAST_INV_REG` 1 -> 0, `SP_DIS_SUB_COAST` 1 -> 0, `SP_H_PROTECT` 1 -> 0,
`SP_NO_CLAMP_REG` 1 -> 0 and `ADC_SOGCTRL` 13 -> 14.

Polling the configuration every 0.25 s through a selection localises it to one
of them: the field goes 0 -> 1 at 2.30 s, which is the `sync processor dynamic`
rung at pass 27, and `STATUS_SYNC_PROC_VTOTAL` jumps to 263 on the same sample.
It returns to 0 at 23.38 s, which is `FullReset`'s prefix, and
`STATUS_SYNC_PROC_HTOTAL` reads 2200 against a 2200 divider on that sample.

Writing the one field to 0 in the stall settles it, with everything else the
rung wrote still in force:

| | before the write | 0.7 s after |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 265 | 259/260 |
| `STATUS_SYNC_PROC_HTOTAL` vs 2200 | 2258 | 2200 |
| `STATUS_MISC_PLLAD_LOCK` | 0 | 1, and held |

`SP_DIS_SUB_COAST` stayed 1 throughout and cost nothing, so the stale serration
reading it takes is not implicated.

**IT LATCHES, which is why it was expensive.** The wrong count is what keeps the
source searched for, and a search is the only state that writes the field, so
nothing reached the clear. The escape was the next rung 21 s later.

## Why the clear could not be reached

`applyDynamic()` runs only where something calls it. On the acquisition tick the
one call site sits inside `if (!Adc::inputIsComponent())` -- the separator
tuning -- so on a component source the dynamic settings are reached by escalation
rungs and by nothing else. Between pass 27 and pass 150 no rung fires, and that
is the 21 s.

## What the engine does now

- The inversion is written AHEAD of the branch that returns, and is a pure
  function of whether the pass could count the source. One owner.
- `VideoSourceAcquisition::takeBackSearchSettings()` runs on the tick and applies
  the dynamic settings once the source can be counted, so a rung's configuration
  is taken back within a pass rather than at the next rung.
- The escalation POSITION no longer advances on a pass that measured the source.
  It used to advance on every pass that had not reached an *acquired* source,
  which a healthy selection spends several of.

## The models that were refuted on the way

- **The sub coast.** `SP_DIS_SUB_COAST` is set from `hasSerratedSync()`, which
  is `lowLineRate() && isCsync()` -- and `lowLineRate()` is the OUTGOING source's
  held rate, so a 37.9 kHz predecessor leaves it wrong. It fitted the
  fast/slow split exactly and is inert: measured wrong for the whole of a leg
  that acquired correctly.
- **Never inverting.** The inversion does real work: uninverted from the start
  the block counts nothing at all, holding 97, and no leg acquired in 40 s.
- **Inverting per pass with no hysteresis.** The count a pass reads flickers
  between the source's and 97, so the field toggled 17 times in 20 s and the
  field rate read 15.4, 28.8 and 113.8 Hz where 59.94 was due.
- **Spending the inversion on the first count in range.** An unlocked block
  produces readings inside the range: a single 273-line reading spent it and the
  block then counted nothing.
- **Spending it on the first STEADY count.** The steadiness gate settled on a
  half-rate, 7892 Hz against 15584.

## What is still open, and it is the ladder

A rung fires into an acquisition that is still reading the source and destroys
it. Measured on a leg that failed:

```
1.83  sampling: 263 lines x 59.08 Hz -> line rate 15657
2.15  sampling: 263 lines x 59.00 Hz -> line rate 15576
2.16  sampling: rate 15576 doubled 1 -> divider 2200
2.18  recovery: coast window at pass 8
2.28  sampling: 269 lines x 70.15 Hz -> line rate 18941
2.80  duty: 243 pulse / 2200 divider, htotal 3268, negative, UNLOCKED
```

The engine had the source measured and its divider installed, and the rung reset
the coast window and discarded the placement 20 ms later. The first reading lands
at 1.8..2.7 s and pass 8 lands at 1.84 s, so which comes first is a race decided
by milliseconds -- which is why the same build acquires in 4.4..6.9 s on some
legs and not within 32 s on others.

Holding the first rung off for 3 s does not close it and makes `vga` slower.
What the evidence points at is that the configuring rungs repeat what a selection
already applies, and each one discards a placement or resets a window, so they
have nothing to add to a source that is being read.
