# The coast cannot be lengthened on sync on green

The Wii at 480p on `ypbpr` reads 521 lines where the raster is 525, and the
sync-on-green separator losing about three hsync pulses inside the 6-line
vertical sync is the arithmetic that fits. `SP_POST_COAST` is 6 and the
datasheet defines it as the line number the coast drops at, so the coast ends at
the vsync boundary with no margin for the separator to free-run through it —
which makes a longer coast the obvious repair.

**It is not available. Every value away from the shipped pair degrades or
destroys the lock.**

## The points

`/coast?pre=&post=` holds a pair against the engine's own constants and survives
a solve. Each row is a 25 s `Tv5725::SamplingLog` capture at 25 ms, scored with
`hlow` inside 130..190 as the liveness guard.

| coast pre/post | liveness | vt 521 | vt 524 | `hlow` 163..166 |
|---|---|---|---|---|
| 7/6, the shipped pair | 99.4% | 76.3% | 19.1% | 20.9% |
| 7/6 through the override | 98.5% | 75.7% | 17.4% | 19.3% |
| 48/6 | 2.8% | — | — | — |
| 7/24 | 23.1% | — | — | — |

**THE NULL ROW IS WHAT MAKES THE OTHER TWO READABLE.** Writing the shipped
values through the override leaves the source indistinguishable from the default
— every column within a point and a half — so the route is harmless and the
deaths belong to the VALUES. Without that row a coast sweep looks like a route
that cannot be swept, which is what "one experiment per boot" was standing in
for.

`/coast?clear` does not recover a lock lost this way; only `/restart` does.

## What this refutes

**Deriving the post coast from the source's vertical blanking.** 480p is 9 lines
front porch, 6 vsync and 30 back porch, so a post coast landing after vsync and
before active video wants 12..35. 24 is inside that band and takes the liveness
to 23.1%.

**That a longer pre coast alone is what made 48/48 read 95% correct counts.**
48/6 is the worst row measured, at 2.8%.

**That the spurious counts near 450 belong to a coast change.** 451 and 455 are
present at the DEFAULT coast, 4.3% of 666 samples, with 433, 452 and 492 beside
them — so an earlier reading of them as added by a 9/30 point is void, and so is
the half of that point's refutation which rested on them.

## What the near-450 counts cost, which is less than it looks

They are a single-sample population and the presence gate takes one sample, so
each one drops presence for the four polls the run needs to re-earn — 48 drops
in 25 s, 15% of the time reading absent. `OutputBlankPasses` is 15 passes and
`UnsettledArmPasses` is 16, so none of them reaches an output blank or an arm.

**The consequence is that a single `/geometry` poll reads `state: absent` on a
healthy source**, which is a reading to distrust rather than a fault to chase.
The sampling log is what answers the question, because it samples from inside
`loop()`.

## What is still open

The three missing pulses. The coast is the only knob measured to move the count
— 48/48 reads 95% correct — and it is not reachable without losing the lock at
either half of that pair on its own.
