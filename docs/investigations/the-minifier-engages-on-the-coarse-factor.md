# The minifier's write enable is `IF_HS_DEC_FACTOR`, and the FIFO consumes it

The input formatter's scaling-down DDA shortens the line. **Two things have to
be true at once, and an undoubled source gets neither:**

1. **`IF_HS_DEC_FACTOR` must be non-zero.** A DDA rate written beside a zero
   coarse factor does nothing whatever. RD-5725-1.1 says so in the field's own
   description -- *if the scaling ratio is less than one half, use it and DDA to
   generate the we and phase* -- and the write enable is what that sentence is
   about.
2. **The line-double FIFO must be in circuit**, `IF_LD_RAM_BYPS` 0.
   `IF_SEL_HSCALE` 1 sends the scaling-down data **and its write enable to the
   line double**, so the FIFO is the only consumer the part offers. It is the
   only RAM in the horizontal path, and thinning a stream needs a buffer:
   writing sparsely and reading back contiguously is what turns a write enable
   into a shorter line. Doubling is a second use of the same RAM, not its
   purpose here.

`InputFormatter::applyScan()` writes `IF_HS_DEC_FACTOR` 0 and `IF_LD_RAM_BYPS` 1
for every undoubled source, so on such a source the block is disabled and its
consumer is out of circuit.

## The measurement

RiscPC on `vga` at 320x256@50, which the engine line-doubles, so the FIFO is
already in circuit. `PLLAD_MD` 2200, the counter held at 1100 throughout, and
the VDS taken to 1:1 (`VDS_HSCALE` 1023) so one captured unit is one output
pixel and a shorter line shows as a narrower picture. Judged with
`../../tools/gbsc-pro-hwtest/picture_match.py` over a scale search reaching
down to 0.10.

| DDA increment | `IF_HS_DEC_FACTOR` | DDA ratio | scale | corr |
|---|---|---|---|---|
| 0 | 1 | 1.000 | 1.000 | 1.000 |
| 1365 | 1 | 0.750 | **0.476** | 0.615 |
| 2047 | 1 | 0.667 | **0.408** | 0.606 |
| 4095 | 1 | 0.500 | **0.332** | 0.569 |
| 4095 | **0** | 0.500 | **1.000** | 0.995 |
| 0 | 2 | 1.000 | 1.000 | 0.665 |
| 4095 | 2 | 0.500 | 0.712 | 0.628 |
| 0 | 1 | 1.000 | 1.000 | 1.000 |

Unity taken at both ends returns 1.000 exactly. **The fifth row is the finding**:
the same DDA increment that compresses the picture to a third of its width with
the coarse factor at 1 does nothing at all with it at 0.

At ratio 0.50 the whole card -- circle, colour bars, grating, greyscale and both
captions -- is legible inside a narrow column with the rest of the line empty.
**The correlation is low because the frame is a compressed picture rather than a
scaled copy of the reference**, so the reported `scale` names the compression
only approximately; what it establishes is the direction and that it tracks the
ratio, not an exact factor.

## Why every earlier reading said the block did nothing

All of them were taken with `IF_HS_DEC_FACTOR` at 0, with the FIFO bypassed, or
both:

- The ratio sweeps driven through `/inputscale` on an undoubled source run with
  the coarse factor at 0 throughout, because `applyScan()` writes it from
  `lineDoubled`.
- The clean "does the picture narrow" test was run only with the FIFO bypassed.
- The green frame an undoubled source gives with the FIFO in circuit was taken
  with the coarse factor at 0, so the FIFO was in circuit receiving no write
  enable at all. It is not evidence about the FIFO's read side.

## What the engine does wrong

Three things, and they compound:

- `IF_HS_DEC_FACTOR` is written from `lineDoubled` rather than from the ratio
  being asked for. It has two owners and this is what the collision costs.
- `IF_LD_RAM_BYPS` is written from `lineDoubled` too, so the one consumer of the
  write enable is bypassed exactly when a ratio is wanted.
- `InputFormatter::lineCounterFor()` shortens the counter by the ratio. Whether
  that is right is **unestablished**: with the block disabled it was simply
  wrong, and now that the block engages the counter has not been swept against a
  working compression.

## Still open: an undoubled source emits green with the FIFO in circuit

At 1280x720@60 with the FIFO in circuit, the coarse factor at 1 and a DDA rate,
the output is a solid green frame -- zeros reaching the frame buffer -- whatever
the rate, and whether the rest of the routing is left undoubled or set to the
whole line-doubled branch (`IF_LD_SEL_PROV` 0, `IF_PRGRSV_CNTRL` 0,
`IF_SEL_WEN` 0, `IF_HS_SEL_LPF` 1, `IF_HBIN_SP`/`ST` 160/96).

So the mechanism is proven on a doubled line and not yet reached on an undoubled
one, which is the case the divider ceiling binds hardest.
`the-scaling-path-samples-below-one-to-one.md` is what that ceiling costs.

## Reading these states

A still cannot tell the failures apart and each has been misread as another.

- A **shear** is a constant slip per row, stable frame to frame, so every
  frame-to-frame measure reports zero on it.
- A counter that divides the real line exactly gives a stable **duplicate**, not
  a roll. It holds perfectly still and its repeated content correlates with the
  card, so neighbouring-row correlation and displacement both call it clean. A
  counter of 1650 at `PLLAD_MD` 2200 and ratio 0.75 was recorded as the roll
  stopping and is one of these.
- Mean luma separates none of them.

`picture_match.py` is what does separate them, by asking whether the frame is
still the reference and at what scale, and **its scale search has to reach below
the compression being looked for** -- a floor of 0.55 clamped the readings above
and made a working compression look like none.

**And a routing register toggled on an already-broken frame proves nothing**:
every reading of `IF_SEL_WEN` and `IF_LD_RAM_BYPS` taken at a shortened counter
was uninformative for that reason. Make the change from a clean solved state
with the counter left alone.

`../scaling-down-path.md` is the path and its registers.
