# The clamp window is placed in the wrong units

**REFUTED. The register counts the chip's 27 MHz reference, not ADC samples, and
its counter's origin is about 12 ADC samples after the sync pulse ends rather
than at the pulse's leading edge.** Both corrections land on this page's
conclusion, and together they say the csync arm was writing the right place:

- `HPERIOD_IF` 214 is not a quantity 5.25x too small for the register. It is the
  quarter-count of the 27 MHz line -- the same quantity
  `27e6 / (4 x lineRate) - 1` derives -- and the csync fraction was fitted
  against it. There is no unit mismatch to correct, and the factor of 5.25 is
  just the ratio of two line lengths each read with its own fraction.
- A window at 20..39 is 28..55 ADC samples past that origin, which is the back
  porch. It only reads as "wholly inside the pulse" if count 20 is taken for ADC
  sample 20 against a pulse of 0..84.

So `ChannelComponentClampOffset`, the `0x60` below, is not a hand-fitted patch
over a unit error. What is left of the fault is a different one, measured later
and on a different path: two callers chose the line the fractions were applied
to and the register held whichever wrote last.

[`the-clamp-had-two-owners-and-two-units.md`](the-clamp-had-two-owners-and-two-units.md)
has the walk that settles the units, and
[`the-clamp-cannot-move-into-the-sync-pulse.md`](the-clamp-cannot-move-into-the-sync-pulse.md)
is the colour measurement that settled the origin's direction first.

**What made it convincing** is the arithmetic below reproducing both register
values exactly, to the unit, from a reading taken off the chip. A fraction
fitted against a line length reproduces the register whatever the register
counts, so agreement there is not evidence about the unit at either end.

## The original reading

`SP_CS_CLP_ST` and `SP_CS_CLP_SP` say where in the line the black level is
sampled. They land inside the hsync pulse rather than the back porch, on both
sync paths, and the arithmetic says why.

## The measurement

Wii at 480p on `ypbpr`, passed through, sync on green:

```
line          1124 ADC samples    (STATUS_SYNC_PROC_HTOTAL, = PLLAD_MD)
hsync pulse   0 .. 84             (STATUS_SYNC_PROC_HLOW_LEN, 7.5% of the line)
clamp window  20 .. 39
```

The window is wholly inside the pulse. `acquireClampWindow()`'s csync arm
reproduces both numbers exactly from `HPERIOD_IF`, which reads 214 for this
mode:

    start = 1 + 214 x 0.089 = 20
    stop  = 2 + 214 x 0.174 = 39

## The units do not match the register

`clampLineLength()` measures the line in `HPERIOD_IF` on the csync path and in
`STATUS_SYNC_PROC_HTOTAL` on the separate path, and the fractions are fitted per
path to suit. **The register counts ADC samples on both**, and `HPERIOD_IF`
counts periods of the chip's 27 MHz: 214 against 1124 here, a factor of 5.25. So
the csync arm writes a window about five times too early, which is inside the
pulse for any source whose sync is under a fifth of the line.

That was read as settling a question `docs/scaler-geometry-model.md` then
carried as open -- whether these two are in IF units or ADC samples. They are
neither, and the header above says what killed the reading.

## What it would have cost, had it been real

**On sync on green the green channel carries the sync**, so a clamp inside the
pulse references G against the sync tip while Pb and Pr reference their true
blanking level. The channels then sit at different offsets, which is a colour
cast, and the channel that is off is green. Observed as a slight green in the
grey of the Wii menu.

On a separate-sync RGB source nothing carries sync on a colour channel, so the
same misplacement costs a black level taken from inside the pulse on all three
equally -- a lift rather than a cast, which is far harder to see and is why this
has survived.

## The offset read as a patch over it

    if (inputIsYpBpR && isHdBypassChannel() && sourceLowLineRate())
        offset = 0x60;

Ninety-six counts later, applied to a component source on the bypass channel at
a low line rate -- `ChannelComponentClampOffset` now. It reads as the correction
the unit conversion should have made, hand-fitted for the one case someone
looked at and withheld from 480p because the gate asks the line rate. With the
units settled there is no conversion for it to stand in for, and what it clears
is a longer sync tip against a 15 kHz line.

## What would replace the fractions

**The idea survives the refutation and the unit in it does not.** Deriving the
window from the measured pulse rather than from a fitted fraction still removes
a constant per path, and it is still the move
[`../video-source-acquisition.md`](../video-source-acquisition.md) makes
everywhere else -- but `STATUS_SYNC_PROC_HLOW_LEN` is in ADC samples and the
register is not, so a width taken from it has to be converted by the line rate
and the divider before it is written.

What blocks it is the counter's origin rather than the arithmetic: the fractions
are of a line the origin is not the start of, and one source cannot place that
origin. A composite source at a second line rate is what would.
