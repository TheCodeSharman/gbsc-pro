# The SOG reference is inverted for pass-through

A sync-on-green source passed straight through loses the left of its line. On
the Wii at 480p into 1920x1080 the source's right border arrives as ~50 emitted
columns, the top as ~3 rows and the bottom as ~40, and **the left border does
not arrive at all** -- the transmitted window sits about one border width right
of where it would have to be to carry the whole line. Correlating a pass-through
frame against a scaled one of the same source puts the deficit at 56 of 1920
columns, absent at the left and none at the right.

`SP_SOG_P_INV` 1 recovers the whole line, with the clamp moved to follow it:
left 0, right 0, a 1920-wide picture, mean luma 189 against 183 un-inverted.

## It is a compensating correction, not a correct polarity

Un-inverted, the sync processor's low time **is** the source's pulse: 152 of
2038 ADC samples in pass-through and 91 of 1212 scaling, both 7.5%, against the
mode's own 62 of 858 which is 7.2%. Inverted it reads the complement. The chip
agrees -- `SP_SOG_P_ATO`, its own SOG polarity auto-correct, declines to invert
this source, and walking it 0/1 moves nothing: left 0 and right 55/56 either
way, `HLOW_LEN` 152 either way, `SP_SOG_P_INV` left at 0.

So inverting makes the processor count from the other edge of the pulse, which
displaces its origin by one pulse width and cancels a displacement of about that
size arising elsewhere. **Where that displacement comes from is open.**

## What it is not

- **Not the channel's emitted sync width.** The channel emits a 124-sample pulse
  where the source's is 152, which looked like the cause. Holding the pulse's
  start at 40 and walking its span 80, 124, 152, 200, 300 -- spanning both sides
  of the source's own -- leaves the left edge at column 0 at every value and
  moves the right bar by at most 2 columns. An earlier walk of `HD_HS_ST`/`SP`
  held the span at 124 throughout and moved only the position, which is why the
  width looked untried rather than inert.
- **Not the sink.** The television plugged directly into the board shows the
  same missing left border, so two sinks sharing no hardware agree.
- **Not the clamp.** Inverted, the clamp frames the picture anywhere from 60 to
  120 and the window is 1920 wide at every one of them. The clamp sets the black
  level and never moved the transmitted window.
- **Not the encoder hunting for the end of black.** With `HD_MATRIX_BYPS` 1 the
  source's border is magenta on the wire at G exactly 0, and the window still
  does not reach it.

## The clamp has to move with it, and the distance is derivable

`SP_CS_CLP_ST` counts the 27 MHz reference, which is the line `coastLineFor()`
already states, so the offset is the source's pulse expressed in those counts --
`syncDuty x 27e6 / lineRateHz`, which is 64 on this source. The un-inverted
start is 19, so the window lands at 83.

Walked at a 4-count width, with a `PAD_SYNC_OUT_ENZ` re-lock at each step:

| start | picture | mean luma | |
|---|---|---|---|
| 40 | 1920x1077 | 219.6 | first ~120 columns a grey band at level 100, bottom border gone |
| 56 | 1920x1079 | 220.1 | whole frame lifted, no borders at all |
| **60..120** | **1920x1028** | **189..195** | **bottom border back at 51, profile normal** |
| 160 | 1920x1027 | 131.2 | clamping on sync, frame crushed |
| 240 | 1917x1027 | 134.4 | crushed, and the right edge starts to go |

So 19 + 64 sits near the middle of the good band where the bare 64 sits one step
inside its lower edge. Below the band the window lands on picture, the black
level lifts and back porch comes through as a visible grey band; above it the
window clamps on the sync tip and crushes the frame.

**A black-margin detector cannot see the lower failure.** It scores the
120-column level-97 band as picture and reports no bars, and at a start of 56 it
reports a full 1920x1079 frame. The column profile and the bottom border are
what separate them.

## The sync type cannot be the condition

`SP_SOG_MODE` is raised by `applySeparation()`'s csync branch, so it means "the
sync separator is in the path" and is 1 for **composite sync on a dedicated pin
as well as for sync on green**. Measured on the bench:

| source | `SP_SOG_MODE` | `ADC_SOGEN` |
|---|---|---|
| Wii `ypbpr`, sync on green | 1 | 1 |
| RISC PC `vga`, `SYNC 0` separate | 0 | 1 |
| RISC PC `vga`, `SYNC 1` composite | **1** | 1 |

`ADC_SOGEN` is 1 on every one of them, so neither field nor the pair separates
the case that wants inverting. A component source carries its sync on the SOG/Y
pin by construction, which is what `HdBypass::enterFor()`'s existing `component
&& csync` says -- and it excludes composite-sync VGA without having to test it.

**RGsB also puts sync on green and is not covered.** Nothing in the chip layer
distinguishes it from RGBs -- both are `component` false, `csync` true -- and
there is no RGsB source on the bench to measure. `VideoSourceSelection` holds
the fact per input and is where the condition would come from if one appears.

## And the scaling path must not inherit it

Nothing on the way out of pass-through calls `applyTrailingEdgeReference()`, so the
inversion would survive into a route whose capture origin consumes it.
`SP_RT_HS_SP` is derived from the pulse `hsyncPulseSamples()` folds, so it lands
in the same place inverted or not -- measured 1187 against the 1186 that
`1212 - 89 + 63` predicts -- while the retimed edge has moved by a whole pulse.

Measured on the Wii at 480p scaling into 1920x1080: inverted, the picture moves
from a left margin of 38 to 209 of 1920 and clips at the right, 1833 columns
down to 1709. It is the same round-trip hazard `SP_SOG_P_ATO` had, which cost
310 columns -- see
`the-sog-polarity-auto-correct-displaced-pass-through.md`.

So `applySeparation()` clears it, and `SyncProcessor` holds whether it is in
force rather than letting two sites decide the condition separately: the clamp
counts from the same reference, and a second condition is one that can drift
from the write.
