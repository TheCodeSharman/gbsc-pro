# The count is unstable while the divider still describes the mode that left

The 640x480@60 -> 320x256@50 leg of a mode change cost seconds that no rung of
the escalation ladder was paying for, while the instrument that could measure
the source read correctly throughout. **Fixed** -- the cost was
`UnsettledArmPasses`, and the leg is now 1.36 s. What follows is the
measurement, one refuted fix that is still the trap to avoid, and the fix that
holds.

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

## The fix: the arm was the cost, and it was 3.0 s

`UnsettledArmPasses` is the escape from the deadlock -- the one thing that opens
a re-measure on a count that is plausible every sample and steady on none, and
so the one thing that reaches `prepareToMeasure()` and rewrites the divider. At
150 polls of `DetectionIntervalMs` it was 3.0 s against a leg measuring 3.1 s.
**The arm WAS the remaining cost.**

What it has to sit through is noise, not seconds: one disagreeing sample
restarts the idle run, so a single glitch costs a whole run of `SteadySamples`
to recover. It is derived as four of those back to back. The arm is one-shot per
unsettled episode either way, so a source that genuinely never settles -- the
Wii in 480i -- still arms exactly once.

Measured on `vga`, RiscPC via ModeServ, 640x480@60 -> 320x256@50:

```
11.93  source absent: 122 lines, 1458 samples against divider 1096
12.46  source moved: unsettled count (246 lines, solved 524)
13.26  sampling: 311 lines x 50.08 Hz -> line rate 15625
```

The arm fires 0.53 s after the source is seen absent, and the correct rate is
measured 1.33 s after. The per-pass grind above is gone from the console
entirely.

| leg | before | after |
|---|---|---|
| 640x480@60 -> 320x256@50 | 3.11, 5.23 s | 1.34, 1.38 s |
| 320x256@50 -> 640x480@60 | 1.26, 1.33 s | 1.02, 1.10 s |

## The asymmetry is real, and it is the scan mode

The open question was whether the instability is the divider alone, given that
the slow leg is also the leg that changes the scan mode. Walked in both
directions over four modes, two passes each, 24 of 24 acquired:

| opposed pair | slower | faster | gap |
|---|---|---|---|
| 800x600@60 <-> 320x256@50 | **1.56** | 1.18 | 0.38 |
| 640x480@60 <-> 320x256@50 | **1.36** | 1.06 | 0.30 |
| 720x576@50 <-> 320x256@50 | **1.38** | 1.16 | 0.22 |
| 640x480@60 <-> 800x600@60 | **1.20** | 1.07 | 0.13 |
| 720x576@50 <-> 640x480@60 | **1.09** | 1.04 | 0.06 |
| 800x600@60 <-> 720x576@50 | **1.12** | 1.11 | 0.02 |

**Every leg landing on 320x256@50 is the slower of its pair**, and those are the
three largest gaps. 320x256@50 is the only line-doubled mode of the four, so the
legs that pay are the legs that change the scan mode -- which is what the
reference divider is a function of. Among the progressive modes, which share a
scan mode, the gaps fall to 0.02 to 0.13 s.

The cost is no longer seconds, so what is left is an ordering question rather
than a deadlock: arriving in a line-doubled mode costs about 0.3 s more than
leaving one.

## What is not yet known

What the ~1.0 s floor common to every leg is spent on. It is now the dominant
term and nothing here separates it into the source's own settling, the sync-type
probe, and the engine's solve.
