# The per-arrival edge ripple is the frame time lock converging, not the sampling phase

A soak that scored each mode-change arrival's emitted picture with
`picture_jitter` found a horizontal ripple on 7 of 26 arrivals of one scaled
leg and on 1 of 79 pass-through arrivals, and the sampling phase the engine
picked did not predict which. The ripple is the **scoring moment**: the frame
time lock is still walking the phase in when the measurement is taken, and the
read/write crossover sweeps across the picture while it does.

## The same arrival, scored twice

Ten arrivals of `MODE X640 Y256 C2 F50` reached from `MODE X800 Y600 C256 F60`
on `vga`, each scored in the soak's own window -- immediately after
`sampling phase: chosen`, 600 frames of warmup then 90 frames -- and again once
the lock had parked. `hsd` is `picture_jitter`'s horizontal displacement sd in
dongle columns.

| | median | max | at or over 0.010 |
|---|---|---|---|
| scored early, as the soak does | 0.0076 | 0.0191 | **2 of 10** |
| scored with the lock parked | 0.0040 | 0.0064 | **0 of 10** |

Early exceeds late on **9 of 10** arrivals, median ratio 1.86. The early median
reproduces the soak's own figure for that leg, 0.0070, and the late median
reproduces its settled leg, 0.0036 -- so the two readings of one arrival span
the whole difference the soak attributed to the phase.

**The scan's own band does not predict either reading**: the dirty band is 7 to
9 phases wide on all ten and `r = 0.31` against the early score.

## Auto gain is not the cause, and the window excludes it

Two things run after the pick. `AutoGainReadyPasses` is 91 against
`PhaseFirstPass` 60, so auto gain ramps the ADC gain from 123 to 203 over about
nine seconds, ending at 13.7 to 18.5 s. The early scoring window is 16.9 to
18.7 s past the mode change, so it is after the gain has settled, and the lock
is the only thing still moving in it.

Nor could gain reach the band: `walkTheField()` scores each phase by how
steadily the sync processor counts the line against the divider, which is a
sync-domain measurement, and on separate sync the sync path never passes through
the converter.

## What a healthy arrival's lock does

One arrival, the full trace, `vga` at 312 lines into 1920x1080:

```
 2.74  rate match: source 50080 mHz, output 50106 mHz, clock -> 107943336
 3.25  err -1063114  out 50080 -> 50050   <- clamped, -0.06%
 4.86  err  -907294  out 50050 -> 50050   <- held at the clamp
 6.62  err  -742450  out 50050 -> 50050
 8.19  err  -581482  out 50050 -> 50050
 9.89  err  -416636  out 50050 -> 50055   <- taper begins
24.94  err    -1646                       <- the crossover crosses the picture
26.69  err   +2622
38.41  err   +11096
68.35  err   +11668                       <- parked
```

The phase error at the first pass is **a third of a frame** and its sign is a
draw -- +941800 on one arrival, -1063114 on the next. The correction is
proportional and bounded at 0.06%, so a third of a frame saturates it, the rate
sits at `input x 1.0006` while the phase closes at 0.03 frames a second, and
**full settling is about forty seconds**. The crossover passes through the
picture around 25 s in, which is where the worst early score was taken: the
arrival scored 0.0191 had an error of -2112 at its scoring window.

## What this retires, and what it leaves

**A picture measurement taken within about forty seconds of an arrival measures
the frame time lock.** That is the trap the 131-arrival soak fell into, and it
applies to any per-arrival scoring -- wait for the lock to park, or the score is
of the transient.

It also **bounds the fix proposed for a rate mis-measured at acquisition**, in
`known-issues.md`. That fix reads a trim saturated in one direction for several
consecutive passes as evidence the coarse step was wrong. A perfectly healthy
arrival saturates it for **four consecutive passes**, so consecutive saturation
alone cannot be the trigger. What separates them is whether the error
**decelerates**: healthy, the per-pass change shrinks from -148438 to -47484 as
the correction tapers; in the fault it holds near -220000 and sails through
target. The residual drift at a correction near zero is the rate error, and a
count of saturated passes is not.

**What is not explained is a residual that survives into the settled picture.**
The late scores correlate with the early ones at `r = 0.68` and span 0.0029 to
0.0064 -- a factor of two above the bench floor, arrival to arrival, with the
lock parked and the band width flat. Any genuine phase-related effect lives
there, not in the figures the soak classified.
