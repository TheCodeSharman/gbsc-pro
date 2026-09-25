# The divider is an actuator inside the loop that measures it

`PLLAD_MD` is derived from the measured line rate, and the line rate is measured
through `PLLAD_MD`. The field rate is timed off the input formatter's vertical
output, and the input formatter's line counter **is** the divider
(`IF_HSYNC_RST`), so writing a divider moves the instrument that chooses the next
one.

Re-derived on every measurement pass, the pair limit-cycles.

## What it looks like on the bench

One input switch to the Wii on `ypbpr` 480p, console captured with timestamps.
The engine committed a divider 48 times over 40 s, alternating:

    sampling: rate 31519 doubled 0 -> divider 1444
    sampling: rate 31440 doubled 0 -> divider 1448
    sampling: rate 31519 doubled 0 -> divider 1444
    sampling: rate 31440 doubled 0 -> divider 1448

Installing 1444 makes the next reading fall, which asks for 1448. Installing
1448 makes it rise, which asks for 1444. **Neither value is a fixed point**, and
the divider the true rate asks for — 1446, from 31468 — is never visited.
`STATUS_SYNC_PROC_HTOTAL` read 1703 against the divider's 1448 throughout, with
an explicit `UNLOCKED`, because every write re-latches the ADC PLL and restarts
its settle.

There were **no `source moved:` lines** in that window. Nothing about the source
changed; the loop is self-sustaining.

## The scatter is the loop's, not the source's

`SamplingLog::rates()` on the same source once settled, 250 back-to-back
timings:

| | distinct values | span |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 1 of 250 (524) | 0.00 per mille |
| field rate | 2 of 250 (59.939, 59.940 Hz) | 0.02 per mille |
| line rate | 1 of 250 (31468) | 0.00 per mille |

So the instrument is exact on a settled source, and the 5.8 per mille spread the
churn reads is produced by the churn. **Holding the divider costs nothing**,
because there is no scatter to track.

## Why a tolerance on the divider cannot fix it

The guard that was there compared the re-derived divider against the one in force
within 2 per mille, ANDed with a rate comparison. Widening it does not help and
narrowing it does not either: the cycle **steps past** the value it wants on both
sides, so there is no width at which the comparison converges on 1446. It only
decides how far each swing travels.

That tolerance was sized on a divider of 2506, where one quantisation step is
0.8 per mille. At 1446 the same step is 2.8 per mille, so it stopped holding —
but that is why it stopped working here, not why the approach is wrong.

## What the divider is held against instead

The rate the divider in force was sized from, at the tolerance that means two
readings are the same source. Same source, same divider — one install, and the
measurement is then taken through a settled clock.

Two dividers are **choices rather than measurements** and are compared exactly:

- the one a mode change asks for, because the output moves the bound the divider
  is sized against. 480p affords 1876 samples of a line and 576p 1952, 4.1%
  apart with no part of it noise; forgiven, the two SD modes would share
  whichever clock was arrived from.
- one commanded through `VideoPath::holdDivider()`, which `/sampleclock` uses. A
  command must not be swallowed by a rate that did not move, and **releasing** a
  hold is equally a command — so the hold forgets the rate the clock in force was
  installed from, which is what makes the next install unskippable.

## What this is not

It is not the reference divider returning.
`the-reference-divider-was-the-bootstrap.md` records why that went: two clocks
per mode change had the duty read through one and spent in the other's units, an
11% error in the quantity the capture window's head is placed from. The ordering
it established — measure the rate, install the divider that rate asks for, then
read the duty through it — is unchanged. What changes is how often the install
happens: once per source, rather than once per measurement.

It does not reach the acquisition time that detection spends. Measured across
five switches to `ypbpr`, detection costs 7.4 s on every one of them, of which
6 s is a separator search whose early exit cannot fire.
