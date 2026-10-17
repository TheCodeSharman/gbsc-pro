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

## The duty is a pass behind the install, and the route decided the divider

**Measured on the bench at 640x480@60 on `vga` with the output, the shape and
the framing held: the divider landed 1212 arriving as a source mode change from
320x256@50, four times of four, and 1272 arriving as a re-detection, six times
of six.** One source, two framings, and 5% of the sampling density between them.

The divider's binding bound there is the one-line delay's -- `1018` units of
line buffer over the share of the line the source's published raster gives its
active video, `1018 / 0.800` for DMT's 640x480@60. Which published raster the
key matches is decided BY THE DUTY, and the duty is the one measurement that
cannot be in hand when the divider is chosen:

- `takeDuty()` refuses a reading taken through an unlatched clock, correctly --
  a count taken while the processor is counting another line length divides by a
  divider it never saw.
- only installing a divider latches one.

So the first solve of any source chooses with no duty, and what it used instead
was **the duty of the source that had just left**. `modeChanged()` cleared
`dutyMeasured_` and left `hsync_`, which is what every consumer asks for, and
`VideoPath` held a second copy in `reading_` with the `SourceTiming` matched
from it. Both are cleared on a mode change now, so the key carries no duty until
a latched pass measures one.

A host case reproduces the pair exactly, 1212 against 1272, by solving the
640x480@60 source alone and after the bench mode with the line filter in
circuit. Both land on 1272 afterwards, which is the bound itself rather than
mere agreement: agreeing on the output's own ceiling instead would be the delay
destroying the tail of every line.

### And two registers were being sized from the stale duty without anyone noticing

Stopping the duty at the mode change exposed what had been living off it.
`SP_CS_HS_SP` and the clamp window are both duty-derived and were written only
from `applySamplingClock()` -- at the install, a pass short of the duty. With no
stale pulse to stand in, the install wrote 0 and nothing replaced it.

**On the leading-edge reference `SP_CS_HS_SP` IS the capture counter's origin**,
so 0 moves it by a whole pulse. Measured at 320x256@50 on composite into 1080p:
`SP_CS_HS_SP` 0 against the 148 the pulse asks for, **231 output columns of
black at the near edge** and the far border off the emitted frame, where the
same state reads 24 columns with the register placed.

Both now have one placement, driven from the divider installed and the pulse
measured, which `sourceMeasured()` runs on the pass the duty arrives.

**An arm in `sourceMoved()` is the wrong answer and was tried.** It is asked
only on a detection pass, and arming it calls `inputTimingsChanged()` -- which
discards the measurement, so a "the duty arrived" arm there forgets the duty it
fired for and loops.
