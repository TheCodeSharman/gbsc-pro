# The zoom shear follows the produced width's parity

Zooming horizontally used to shear the picture on about half the steps: step in,
it shears; step again, it clears; step again, it shears. `Axis::solve()` now
biases the memory window to an odd width and the artefact does not appear.

## What the quantity is

`Axis::solve()` writes the memory window as

    VDS_HB_ST = floor(VDS_HB_SP + originOffset + produced)
    memory    = (VDS_HB_SP, VDS_HB_ST)

so its width is `floor(originOffset + produced)` and **`VDS_HB_SP` cancels**.
Checked against every engine-solved mark on record it holds in 131 of 134, the
three exceptions off by one, which is the rounding tolerance of the fitted origin
constants.

So the parity that reaches the picture is the **produced picture's width in
output pixels**. An even one shears, an odd one is clean. The memory window is
where it becomes visible, not where it comes from — which is why stepping the
zoom alternates arbitrarily, `produced` moving continuously while the floor of it
flips.

**That identity no longer holds exactly, and the rule is the register.** The far
edge now stops one capture unit short of the write end for the interpolator's
reach, so the width is `floor(originOffset + produced - magnification)`. Which of
the two the part responds to is not separable from the marks: `produced` cannot
be moved without moving the capture or the scale, and walking the scale to reach
a parity moves it by tens of counts before the floor flips, which is picture size
paid for a parity. So the bias stays on the register the solve can set.

## How it was separated from the register

Two earlier rules fitted well and were wrong, each refuted by the dataset it was
not fitted to:

| rule | jog, one solve | sweep, whole solves |
|---|---|---|
| `VDS_HB_SP` odd shears | 24/24 | 17/34, chance |
| `VDS_HB_SP + floor(produced)` even shears | 34/35 | 77/104 |

Anything of the form `VDS_HB_SP + floor(origin + ...)` fits better still, 93/104,
and is **degenerate**: `origin` already contains `VDS_HB_SP`, so the two cancel
mod 2 and the rule reduces to a function of the zoom with no register dependence
at all, which the jog disproves directly.

What separated them is that the width and the register can be moved
independently. `creep_memory_width.py` does both, and the two rules predict
opposite outcomes on each:

| motion | marks | `VDS_HB_SP` | width | measured |
|---|---|---|---|---|
| far edge alone | 23 | **fixed at 33** | every integer 1810..1825 | alternates clean/shear, 23 of 23 |
| both edges about the centre | 8 | **walks 34..40** | parity held | verdict never changes |

The width rule called **38 of 38** across that session, each mark predicted before
it was taken. On the marks it was originally fitted to it calls 189 of 189 of
those the eye judged consistently — the seven exceptions there are register states
that were marked both ways on separate visits, 8 of 21 revisited states having
been, so the artefact is marginal near a boundary and a single mark carries less
than it appears to.

## What scoring it wrong looks like

Scored across the whole dataset the rule reads 218 of 255 and appears to be a
candidate with 36 misses hiding a second variable. Thirty-one of those sit below
the near edge's clamp, where the picture is broken whatever the width — 51 marks,
49 sheared. Splitting there is what turns the rule from a candidate into a
finding, and `shear.clamped()` is the split.

## The fix, and what it is not

`solve()` biases the width by a unit where it comes out even, so every solve
lands on an odd one. It is a **bias, not a cure** — why an even width shears is
not known. Anything that later explains the mechanism should be expected to
replace it rather than build on it.

**The unit goes FORWARD, and only the memory window takes it.** The bias used to
step back, because opening the window past where the write ends shows memory the
playback stage walks and nothing wrote. Two things have changed since:

- the far edge now closes one capture unit short of the write end, for the
  interpolator's reach, so there is a reserve to step into that stepping back
  does not use;
- stepping back blanks a written pixel, which is a black column down the right
  where the picture should reach the edge of the screen.

So the memory window takes the unit and the **aperture does not follow it**.
Moving `VDS_DIS_HB_ST` with `VDS_HB_ST` puts the last shown column one past the
interpolator's reach, which is a column of junk down the right-hand edge —
measured on the bench the moment the forward bias was flashed. Blanking that
column costs no picture, because it was never captured.

The two windows therefore differ at the far end by the bias, with the MEMORY one
wider. That is the safe direction: the fetch covers every column the aperture
shows. The reverse would show a column the fetch never filled.

Horizontal only. `VDS_VB_SP` has never been crept, so the vertical axis is
unmeasured rather than known to be unaffected.

**And the width is not the only parity that reaches the picture.** The output
raster total carries one of its own, measured with the capture, the scale and
both windows held:
[`the-raster-total-decides-which-samples-play-out.md`](the-raster-total-decides-which-samples-play-out.md).
A state can satisfy this rule and still be corrupt through that one, which is
what made a bisect necessary to tell them apart.

## What the bench says after it

Flashed to the unit, RiscPC at 320x256@50 on `vga`:

- 120 consecutive zoom solves across `VDS_HSCALE` 441..563, **no even width**.
- Six consecutive zoom steps photographed, all clean, where the same steps
  previously alternated. The markers are the test card's curved edges — the gold
  segment's curve and the curve before the finest grating — the frequency wedge's
  regularity, and the text labels, which carry vertical black bars when it shears.

The marks are under `tools/gbsc-pro-hwtest/sessions/`, each carrying the sixteen
registers it was taken at and the prediction made before it.

## Measuring this again

The artefact is visible in a photograph, but only against a control. A camera
capture of an unchanging screen differs from the next by about 2.2 grey levels,
and a written state differs from an unwritten one by the same, so that figure is
the camera's noise floor and not a picture that moves. Averaging several aligned
frames per state is what makes a marginal width callable.

Two automated metrics were tried and both failed. Sub-pixel row-to-row
displacement tracks MAGNIFICATION rather than the artefact — a clean zoomed frame
scores 0.30-0.37 against 0.06-0.08 clean at the default framing, and 0.31 for a
grossly sheared one. A spectral test on the same sequence gives no ordering at
all. The eye is the instrument, and `creep_memory_width.py` is built around that.
