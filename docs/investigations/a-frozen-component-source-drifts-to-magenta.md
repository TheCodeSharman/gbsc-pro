# A frozen component source drifts to magenta, so no colour reading survives a freeze

With automation frozen on `ypbpr` and **nothing written**, the emitted frame's
colour runs away: R and B climb together while G falls, from `R-G +17.5` to
`+114` over 46 seconds. It does not settle inside a minute, and a walk that
reads a register a second or two after writing it measures this instead of the
register.

Measured on the Wii in 576i at 1920x1080, scored as the per-channel mean of the
lit pixels of the emitted frame off the USB capture.

## The runaway, with nothing written

Frozen on the solve, clamp held at `SP_CS_CLP_ST` 162, one reading every 3.5 s:

| elapsed | R | G | B | R-G | B-G |
|---|---|---|---|---|---|
| 0.3 s | 164.4 | 146.9 | 162.6 | +17.5 | +15.8 |
| 7.3 s | 186.9 | 140.4 | 187.8 | +46.5 | +47.4 |
| 14.3 s | 204.1 | 132.9 | 207.0 | +71.2 | +74.1 |
| 24.8 s | 222.0 | 122.2 | 226.1 | +99.8 | +103.9 |
| 45.8 s | 224.0 | 109.9 | 227.0 | **+114.1** | **+117.2** |

R and B saturate near 225 and G keeps falling, which is a black reference
walking away on the two colour-difference channels.

## What it invalidates

**The clamp position does not detectably decide the colour, and the walk that
said it did was reading the clock.** Two walks of `SP_CS_CLP_ST` both climbed
monotonically in walk ORDER rather than in clamp value -- 0 read `R-G +2.8`
early in one run and `+128.5` late in another -- and a third run with the
shipped value repeated between every treatment shows the control moving as far
as the treatments do:

| treatment | R-G | B-G |
|---|---|---|
| 162, control | +112.8 | +115.7 |
| 0 | +128.5 | +132.1 |
| **162, control** | **+136.2** | **+140.2** |
| 40 | +141.1 | +145.0 |
| **162, control** | **+144.3** | **+148.5** |
| 240 | +145.6 | +149.7 |
| 0 | +146.1 | +150.2 |

42 seconds of settling per value is not enough to spend the history, so every
reading is the trajectory and none of them is the treatment. A clamp over
picture remains a live mechanism for a cast -- `the-clamp-had-two-owners-and-two-units.md`
-- but this experiment does not test it, and nothing here supports or refutes
it.

## Reading the colour of a component source

- **Never from a frozen state.** The engine's own passes are what hold the
  reference still; stopping them is what starts the runaway.
- **A repeated control is not optional.** The confound is in the elapsed time,
  so a treatment compared only against its neighbours cannot see it. The
  control has to be the shipped value, written between every treatment, and
  read the same way.
- **Saturation is not a way out.** It had not arrived after 42 s at one value,
  and the asymptote is the fully magenta frame rather than a stable reading.

A difference between two states photographed close together, with the same
state shot twice as the control, is the shape that survives -- the same rule
`framing-is-anchored-to-a-measured-pulse.md` already states for brightness.
