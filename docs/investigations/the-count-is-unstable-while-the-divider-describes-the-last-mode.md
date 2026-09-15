# The count is unstable while the divider still describes the mode that left

The 640x480@60 -> 320x256@50 leg of a mode change costs seconds that no rung of
the escalation ladder is paying for, and the instrument that could measure the
source is reading correctly throughout.

## What was measured

Per-pass, on the recovery path, with the sync processor and the input formatter
read adjacently:

```
RECOVER: pass  2 count 185 samples 1462 hperiod 431 step 1
RECOVER: pass  4 count 181 samples 2194 hperiod 431 step 0
RECOVER: pass 20 count 191 samples 1462 hperiod 431 step 0
RECOVER: pass 50 count 188 samples 1461 hperiod 431 step 0
RECOVER: pass 100 count 176 samples 2194 hperiod 431 step 0
```

Three things at once:

- **The count is wrong and unstable** -- 176..196 where 311 is due.
- **`STATUS_SYNC_PROC_HTOTAL` jumps between ~730, ~1462 and ~2194**, which are
  one, two and three times the same number: the ADC PLL locking to every first,
  second or third hsync, not holding any of them.
- **`HPERIOD_IF` reads 431 the whole time**, which is exactly right for a
  311-line 50 Hz source. The input formatter is measuring the NEW mode
  correctly while the sync processor cannot count it.

So nothing is wrong with the source, the sync path, or the input formatter. The
divider in force still describes the mode the source left.

## The ladder is not what is being paid for

`step 0` on almost every pass. `SyncRecovery`'s rungs fire once each at their
positions -- 2, 8, 27, 32, 34, 38, 48, 150, 151, 413, 450 -- so between 48 and
150, and again between 151 and 413, **nothing escalates at all**. The passes
that look like an escalation ladder climbing are the ladder idling.

What eventually moves is `ReprobeSyncType` at 151, which answers
`own V sync: yes after 2ms` and restarts the run. The source had its own V sync
for the whole three seconds; the probe discovers nothing and the restart is what
helps.

## The obvious fix was tried on the bench and is REFUTED as written

A `ReferenceSamplingClock` rung at position 4, calling
`VideoPath::prepareToMeasure()` -- the same guarded entry the measure path uses,
so the bypass exclusion and the line doubling keep one owner.

It made things worse: the unit reached `count 0 samples 1023` and stayed there,
recovering only on an explicit `/input?src=vga`.

The mechanism is in `Adc::applySampleRate()`, and it is deliberate there: with a
line rate of 0 there is no crossover row to read, so it writes `PLLAD_MD` and
latches it while leaving `PLLAD_KS` alone. On the recovery path the rate
estimate IS 0 -- that is what being unable to measure means -- so the rung
installs a divider against whatever octave the previous mode left, and the PLL
cannot lock to it.

**So a re-reference is only safe where a rate estimate exists to size the
crossover row from.** Any future attempt has to establish that first, and the
bench is the only place that settles it.

## What is not yet known

Whether the instability is the divider alone. The reference divider is a
function of the scan mode, and 640x480 progressive and 320x256 line-doubled do
not share one -- so the leg that is slow is also the leg that changes it, and
those two have not been separated.
