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

`solve()` gives a unit back where the width comes out even, so every solve lands
on an odd one. Back rather than forward: opening the window past where the write
ends shows memory the playback stage walks and nothing wrote, where closing it one
short blanks a pixel that was written.

It costs one output pixel and it is a **bias, not a cure** — why an even width
shears is not known. Anything that later explains the mechanism should be
expected to replace it rather than build on it.

Horizontal only. `VDS_VB_SP` has never been crept, so the vertical axis is
unmeasured rather than known to be unaffected.

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
