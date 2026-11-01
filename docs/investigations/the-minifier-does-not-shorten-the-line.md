# The minifier does not shorten the line, on either scan mode

`Tv5725::InputScale` writes the input formatter's scaling-down DDA, and the
engine shortens the line counter to match the ratio it asked for. **The block
does not shorten the line.** It perturbs the sample phase and leaves the
picture where it was, at the size it was, so the shortened counter is what
destroys the frame.

This holds on a line-doubled source as well as an undoubled one. The earlier
reading that it engages on a doubled line and not on an undoubled one is
superseded: the doubled source's broken frame is a DUPLICATE, which every
measure that asks whether a frame moves calls clean.

Measured on `vga` into 1080p on the scaling path, PM5544 with `ANIM OFF`,
driven through `/inputscale` so the capture window, both scales, the stride,
the fetch and the counter all follow the ratio. Judged with
`tools/gbsc-pro-hwtest/picture_match.py`, whose `corr` is the frame's match
against a reference captured from the same state at unity.

## Every ratio breaks the picture, on both scan modes

**1280x720@60, undoubled, `PLLAD_MD` 1444:**

| rate | ratio | counter | corr | scale |
|---|---|---|---|---|
| 0 | 1.000 | 1444 | **1.000** | 1.000 |
| 100 | 0.976 | 1409 | 0.218 | 1.000 |
| 205 | 0.952 | 1375 | 0.246 | 1.000 |
| 455 | 0.900 | 1299 | 0.277 | 0.555 |
| 1023 | 0.800 | 1155 | 0.272 | 1.140 |
| 1365 | 0.750 | 1083 | 0.563 | 1.810 |
| 0 | 1.000 | 1444 | **1.000** | 1.000 |

**320x256@50, line doubled, `PLLAD_MD` 2200:**

| rate | ratio | counter | corr | scale |
|---|---|---|---|---|
| 0 | 1.000 | 1100 | **1.000** | 1.000 |
| 100 | 0.976 | 1073 | 0.853 | 1.000 |
| 205 | 0.952 | 1047 | 0.827 | 1.000 |
| 455 | 0.900 | 990 | 0.871 | 0.995 |
| 1023 | 0.800 | 880 | 0.745 | 0.995 |
| 1365 | 0.750 | 825 | 0.811 | 1.000 |
| 0 | 1.000 | 1100 | **1.000** | 1.000 |

Unity taken at both ends of each run restores the frame exactly, so the control
is clean and the figures between them are the ratio rather than drift. **A 2.4%
compression is enough**, which is the first thing that says the ratio is not
merely landing slightly wrong.

**The doubled source scores higher on a frame that is just as broken.** Its
failure duplicates real content across the line, which correlates with the
card; the undoubled source's shreds into noise, which does not. That difference
in APPEARANCE is the whole of the asymmetry that was previously attributed to
the block.

## The rate registers alone leave the picture untouched

From a solved unity state at 720p, writing the eight segments and the shared
low nibble for a ratio of 0.75 and **leaving the counter where the unity solve
put it**:

| state | corr | scale | detail |
|---|---|---|---|
| solved, unity | 1.000 | 1.000 | 10.92 |
| rate 1365 alone, counter 1444 | **0.938** | **1.000** | 10.86 |
| back to unity | 1.000 | 1.000 | 10.92 |

The card is in the same place at the same size, with the grid, the circle, the
cross and the text unmoved; the solid colour blocks are striped. A block
compressing the line by a quarter cannot leave `scale` at 1.000, and near-Nyquist
detail does not fall either, so the DDA is disturbing sample phase rather than
removing samples.

## The counter 1650 reading is a duplicate

The figure that supported the doubled source working — at `PLLAD_MD` 2200 and a
ratio of 0.75 the roll stops at a counter of 1650, which is `PLLAD_MD x ratio`
— reproduces, and the frame it names is not a picture. With the counter walked
by hand at that ratio:

| counter | corr | scale |
|---|---|---|
| 825 (what the solve writes) | 0.687 / 0.641 | 1.000 / 0.595 |
| 1100 (`PLLAD_MD / 2`) | 0.751 / 0.751 | 1.000 |
| 1650 (`PLLAD_MD x ratio`) | **0.647 / 0.648** | 0.680 |
| 2047 | 0.905 / 0.809 | 1.005 / 1.010 |

Two passes, so 1100 and 1650 repeat and 825 and 2047 do not. At 1650 the frame
carries the left third of the card twice down the screen with two thirds of the
line black and a green bar beside it. **It holds perfectly still**, which is why
it reads as the roll stopping.

**A hand-set counter walk is not a repeatable measurement here.** At 720p the
same counter gave opposite verdicts in two walks over the same range with
nothing else changed, so the state depends on how the walk arrived rather than
on the register. Nothing about the line length should rest on one.

## The line is not shortened while the FIFO is bypassed

The clearest form of the measurement takes the VDS to 1:1 -- `VDS_HSCALE` 1023,
the closest a ten-bit field reaches -- so one captured unit is one output pixel
and a compression shows as the card occupying less of the screen. At unity the
card fills the left 1445 px of 1920, with the playback's fetch past the picture
after it; a shortened line would move that boundary.

Asked for a half and for a quarter, with the counter left where the unity solve
put it:

| state | card width | card position |
|---|---|---|
| unity | full | unmoved |
| DDA 4095, ratio 0.50 | full | unmoved |
| `IF_HS_DEC_FACTOR` 1, the half octave | full | unmoved |
| `IF_HS_DEC_FACTOR` 2, the quarter octave | full | unmoved |

**A block compressing 4:1 cannot leave the card the same width and in the same
place.** Only the colour striping changes, which is the DDA disturbing sample
phase.

## The FIFO is the consumer, and on a progressive source it emits green

`IF_SEL_HSCALE` 1 sends the scaling-down data **and its write enable to the
line double**, so the line-double FIFO is the only thing the datasheet names
that could turn that write enable into a shorter line -- and an undoubled
source bypasses it, `IF_LD_RAM_BYPS` 1.

Put in circuit from a clean solved state at 720p, with either the half octave or
the DDA at 4095 beside it, **the output is a solid green frame**. That confirms
the earlier reading rather than correcting it.

So neither `IF_LD_RAM_BYPS` nor `IF_SEL_WEN` is refuted: a routing toggled on a
frame that is ALREADY destroyed cannot be told apart from one that does nothing,
and that is what every reading of those two at a shortened counter measured.
What is established is narrower and firmer -- **with the FIFO bypassed the DDA
does not shorten the line, and with it in circuit a progressive source produces
no picture at all.**

## What follows

`InputFormatter::lineCounterFor()` applies the ratio to the counter, and no
measurement supports its doing so. Nothing ships in that state —
`VideoPath::inputScale_` is unity unless the `/inputscale` debug route moves it
— so this is a bound on what the block can be asked for rather than a defect in
the engine.

**So the minifier does not lift the divider ceiling in any configuration
reached so far**, which is what it was wanted for: the chain it was to serve is
a divider above the output's ceiling, the DDA compressing the captured line back
to what the raster can hold, and the VDS magnifying that to fill the screen.
`the-scaling-path-samples-below-one-to-one.md` is what the ceiling costs, and
the three reachable oversampling states are unchanged by this.
`../scaling-down-path.md` is the path and its registers.

**The open question is how the line-double FIFO is run on a progressive
source**, not which bit enables the DDA. Its read side -- `IF_LD_SEL_PROV`,
`IF_LD_ST`, `IF_LD_WRST_SEL`, `IF_HBIN_SP`/`ST` and `IF_HSYNC_RST`, which
RD-5725-1.1 calls the total pixel number per line and ties to generating
progressive timing from interlace data -- has never had to be configured for a
source that is already progressive, because such a source has always bypassed
the FIFO. The green frame is that read side, and it is where the question now
lives. The eight segment boundaries are undocumented and unmeasured besides,
and a linear ratio across all eight is the only configuration anything here has
asked for.
