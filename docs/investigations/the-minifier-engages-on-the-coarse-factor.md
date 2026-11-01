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

## The FIFO does not need the line-doubled routing

Searched where the block works rather than where it does not. From the
compressing state on the 312-line source -- FIFO in, coarse factor 1, DDA 4095,
VDS at 1:1 -- each register was flipped to the value the undoubled branch would
write:

| flipped to the undoubled value | corr | green |
|---|---|---|
| none, the working state | 1.000 | no |
| `IF_LD_SEL_PROV` 1 | 0.927 | no |
| `IF_PRGRSV_CNTRL` 1 | 1.000 | no |
| `IF_SEL_WEN` 1 | 1.000 | no |
| `IF_HS_SEL_LPF` 0 | 0.995 | no |
| `IF_HBIN_SP` 2, `IF_HBIN_ST` 0 | 0.921 | no |
| `IF_HSYNC_RST` 2200, the full line | 0.963 | no |
| **all six at once** | **0.862** | **no** |

**Not one of them breaks it**, and with all six flipped the card is still
compressed into a column with the rest of the line empty. So the FIFO wants
none of the line-doubled routing, and the green an undoubled source gives is
not the routing.

## Still open: the FIFO cannot be brought up by writing registers

At 1280x720@60 and at 640x480@60 -- both left undoubled by the engine -- forcing
`IF_LD_RAM_BYPS` 0 gives a solid green frame, zeros reaching the frame buffer.
Nothing moves it:

- the counter at `PLLAD_MD` or at `PLLAD_MD / 2`
- the coarse factor at 0, 1 or 2, and any DDA increment
- the whole line-doubled routing written beside it
- the divider held at 1024, 1444 or 2047
- `SFTRST_FIFO_RSTZ`, `SFTRST_IF_RSTZ`, `SFTRST_DEC_RSTZ`, `SFTRST_MEM_RSTZ`
  and `SFTRST_MODE_RSTZ` each pulsed afterwards

**What separates the working case from every failing one is who configured the
FIFO.** It works on the source the engine SOLVED with the FIFO in circuit, and
keeps working through six register flips afterwards; it is green on every source
where the solve bypassed it and the register was written by hand later. That is
the standing rule about hand-set subsets of an interdependent solve, so the next
move is a change to `applyScan()` rather than another poke.

## What the half octave would buy, and why the divider has to move with it

The coarse factor at 1 halves the kept count, so the capture can be sampled at
twice the divider the raster would otherwise allow. At 1280x720@60, line rate
45013 Hz:

| divider | kept units | ADC rate at 1x | samples per source pixel |
|---|---|---|---|
| 1444, what the solve installs | 1444 | 65 MHz | 0.875 |
| 2890 with the half octave | 1445 | 130 MHz | **1.75** |
| 3599 with the half octave and DDA 0.80 | 1445 | 162 MHz | **2.18** |

All three put the same 1445 units in front of the VDS, so the raster is served
either way and the difference is entirely sampling density.

**`SamplingClock::recommendedDivider()` bounds the divider at
`InputFormatter::LineCounterMax * samplesPerUnit` with `samplesPerUnit` 1 for an
undoubled line, so `/sampleclock?hold=` cannot exceed 2047 there** -- measured,
2890 and 3400 both land on 2047. The half-octave route therefore needs that term
to follow the minifier before it can be tried at all, which is the same one
number `docs/known-issues.md` already identifies.

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
