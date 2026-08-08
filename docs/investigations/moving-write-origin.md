# The write origin moves, which is why `produced` looked lossy

**Status:** settled. The model is in
[`scaler-geometry-model.md`](../scaler-geometry-model.md); this page is why the
obvious alternative is wrong, so it does not get proposed again.

## What the code does

`produced = capture x 1024 / scale` — a plain multiply, both axes, no loss term
at either end.

The write start is **not** a constant. It is
`VDS_?B_SP + START_CONST + START_PER_MAG x magnification`, so it moves as the
picture is zoomed: 55 + 25m horizontally, 0.2 + 0.8m vertically. Every window is
recomputed from the capture and the raster on every change, rather than adjusted
from the previous framing.

## Why not inherit the previous framing

Because the origin moves. Holding a corner constant while the scale changed put
41 px of the previous frame down the left of the screen.

`scale_step()` takes no `scale` argument, and a test asserts that the parameter
does not exist — its existence was the bug.

## What going back looks like

This is worth knowing because the wrong answer was convincing, not silly.

Measure `produced` from a corner you assume is constant, and a fixed length
appears to *vary* with magnification. The deficit changes sign — short near 1:1,
long when zoomed in — which is exactly what a loss term looks like:

```
capture 798, HSCALE 1023 (x1.001)  formula  798.8   measured 785   -14
capture 400, HSCALE  512 (x2.000)  formula  800.0   measured 811   +11
capture 200, HSCALE  320 (x3.200)  formula  640.0   measured 680   +40
```

A two-term fit matched all eleven bench readings to within **0.43 px**, and was
still wrong. It gave `capture_offset = -24.67` and
`output_loss = 38.27`, and **`-24.67` is `START_PER_MAG_H` to two decimal
places** — the origin's own magnification term, split in two and attributed to
the far end of the pipeline.

## The part that generalises

**A good fit is not evidence that the quantities are what you think they are.**
Residuals could not have caught this, because nothing was wrong with the
arithmetic — the error was in what was being measured. Only measuring the near
edge settled it.

None of the eleven readings were bad. They are still acceptance tests; what
changed is what they are readings *of*.
