# The composite shear is the coast, and the sign was backwards

On `vga` at 320x256@50 on composite sync the picture shears a few times a
minute: a burst of mis-measured sync pulses over about 35 ms, the sync
processor's count collapsing, then the emitted frame displaced progressively
down the frame for about ten frames, then recovery. **The vertical interval
coast decides the rate**, over a 57-fold range, and the engine's own pair sits
well inside it.

## Measured

11.49 scored hours, 862 blocks of 48 s, cells visited in a randomised order,
both the coast pair and the sampling phase held in the engine so no solve could
take them back. Baseline is the engine's own pair.

| coast | bursts | collapses | sheared frames | scored | collapse ratio | sheared ratio |
|---|---|---|---|---|---|---|
| **0/0** | **0** | 6 | 9 | 228.8 min | **0.031** [0.01, 0.08] | **0.017** [0.01, 0.04] |
| 7/2, the engine's | 83 | 194 | 534 | 230.4 min | -- | -- |
| 7/6, long post | 4742 | 1261 | 2702 | 230.4 min | 6.50 [5.47, 7.73] | 5.06 [4.55, 5.63] |

Exact conditional p of 1.3e-49 on the collapse and 2.4e-144 on the sheared
frame. **No coast at all abolishes the pulse burst -- none in 229 minutes --
and cuts the sheared frame sixtyfold**, where the long post coast makes the
burst 57 times more frequent.

**AND NO COAST IS A PICTURE, NOT A DEAD FRAME.** Rows 3..1078 and columns
6..1919 lit, mean luma and row profile indistinguishable from the engine's pair
at a row-profile correlation of 1.0000, `IF_VB_ST` 543 either side. The
instrument is repeatable to 0.005 grey levels, shown by a pattern change moving
the same figure from 79.3 to 138.2 and back. So the vertical displacement an
unserrated source is recorded as taking at pre coast 0 does not happen on this
source.

## Why the lock duty acquitted it

The coast was scored on `STATUS_MISC_PLLAD_LOCK`'s duty, where turning it off
reads 77.1% against the engine's 74.4% -- a gain of 2.7 points against a
23-point gap to separate sync, which read as the coast being at most a tenth of
the instability with its sign the wrong way round. **On the fault itself the
sign is the other way and the size is sixtyfold.**
[the-composite-shear-rides-a-half-locked-adc-pll.md](the-composite-shear-rides-a-half-locked-adc-pll.md)
establishes on the Wii that the lock duty does not track the shear at all --
84.7% duty and 0 shears in 53999 frames -- so the metric that acquitted the
coast is one the same evidence disqualifies.

## What the sampling phase does

A real effect, far smaller than the claim it replaces: phase 13 against 16, at
equal exposure over the same 862 blocks, gives a collapse ratio of **1.139**
[1.01, 1.28] at p = 0.013 and a sheared-frame ratio of **1.479** at
p = 2.5e-28. So 16 is measurably the better phase and the margin is tens of
percent, not the difference between a fault and none.

Exploratory, from the same run: the phase separates the arms only where the
coast is on -- 0.03 against 0.03 collapses a minute at 0/0, 1.02 against 0.66
at 7/2, 5.70 against 5.24 at 7/6.

## Why a comparison on this fault needs this much arithmetic

**THE RATE WANDERS ON ITS OWN BY THIRTY TIMES.** Adjacent 600 s legs with
nothing written have measured 0 and 6 bursts, and a 2700 s run three minutes
after a restart measured 32 bursts per 900 s against 4 an hour later. It is not
uptime: the first 600 s leg after a restart measured 0 and the next measured 6.

Three consequences, each of which has produced a published candidate that did
not survive:

- **A leg's absolute count means nothing without its neighbours.** Two legs an
  hour apart compare patches of the wander.
- **A fixed sweep order aliases the wander onto the sweep**, so whatever was
  set during a busy patch correlates. Randomise the order.
- **An A-B-A controls a monotonic trend and does not rescue small counts.** The
  phase was named the lever on legs scoring 1, 0 and 1 bursts, and refuted on
  legs scoring 6, 5 and 4; neither can separate a register from Poisson noise,
  and the real effect is 1.1 to 1.5 times.

A register held by a poke is the other half. The search runs on every solve, a
solve is armed by the count moving, and the count moving is the fault -- so a
poked phase is taken back part way through a leg, and more often in the arm
that disagrees with the engine. `Adc::overridePhaseSyncProcessor()` and
`SyncProcessor::overrideCoast()` are what make a leg measure the register.

## It is the PRE coast

The run above moved the pair together, so it attributes nothing between the
lines. Crossing them does: 180 blocks over 2.39 scored hours, the same
randomised blocks with the phase held at 16, `PATTERN CARD`.

| coast | bursts | sheared frames | worst spread | collapses | scored |
|---|---|---|---|---|---|
| 0/0 | **0** | **0** | 0 | 24 | 36.0 min |
| 0/2 | **0** | 6 | 129 | **1650** | 35.2 min |
| 7/0 | 5 | 19 | 290 | 49 | 36.0 min |
| 7/2, the engine's | 7 | 22 | 237 | 36 | 36.0 min |

**The PRE lines carry the shear.** Sheared frames are 41 at pre 7 against 6 at
pre 0, a ratio of **6.76** [2.54, 18.00] at p = 1.8e-07, and the pulse burst is
**12 against 0** at p = 4.9e-04. **The POST lines do not**: within pre 7 they
give 22 against 19, p = 0.76.

That is what the register's own reasoning says. `SyncProcessor.h` states that
only a serrated source needs the pre coast; `preCoastLines()` returns
`CompositePreCoastLines` for every composite source with no serration
dependence, where `postCoastLines()` follows the measurement. An unserrated
source is handed a hold the header says it does not need.

## THE COUNT COLLAPSE IS NOT SPECIFIC TO THE SHEAR, AND 0/2 IS WHERE THAT SHOWS

At 0/2 the count leaves its band **1650 times**, 47 times the engine's rate,
with **no pulse burst at all and six sheared frames**. So the count dithers
violently while the picture does not shear, and a leg scored on the collapse
alone would call 0/2 the worst state measured when on the picture it is among
the best.

The three metrics agree across 0/0, 7/2 and 7/6, which is why the collapse was
taken as the primary endpoint; 0/2 is a cell that run never visited. **So an
endpoint that tracks the fault across three states is not thereby specific to
it**, and the attribution above rests on the burst and the sheared frame, which
were ranked second and third. It wants confirming on its own run rather than
reading as closed.

**AND pre 0 IS ONLY SAFE WITH post 0.** The same pair of cells differ by a
factor of 69 on the count dither -- 1650 against 24 -- with nothing else moved.
Whatever the post lines do to an unheld count, they do it only when the count
is unheld.

## The sheared frame count is content dependent

The overnight ran on `PATTERN PM5544` at a mean luma of 139 and the attribution
run on `PATTERN CARD` at 80. The shear is read as the spread across bands of
one frame, which a detailed picture carries more strongly than a plain one, so
**sheared-frame counts do not compare between the two runs.** The burst and the
collapse are register reads and do compare. Judge a shear rate within one run
and one pattern.

## The fix landed, and it is validated on the source that shears

The pair follows a measurement now rather than being 7 for every composite
source. Scored on the resulting firmware with `shear_factorial.py`, 48.0 scored
minutes per arm interleaved on the minute, the engine choosing the pair rather
than an override holding it:

| | 0/0 | 7/2 | ratio |
|---|---|---|---|
| bursts | **0** | **17** | -- |
| blocks carrying a burst | 0 / 60 | 10 / 60 | Fisher p = 0.00065 |
| count collapses | 30 | 77 | 2.57 [1.68, 3.91] |
| sheared frames | 13 / 172870 | 35 / 172870 | 2.69 [1.42, 5.09] |

Witness registers read back correct in 60 of 60 blocks on both arms. The
emitted frame at 0/0 is byte-comparable to the 7/2 it replaces -- black margins
7/1/3/1 on both, mean luma 78.26 against 78.24, the card's border on all four
edges -- so the picture pays nothing for it.

All three arrangements hold with a freshly probed verdict: RISC PC composite
0/0 with count 308, RISC PC separate untouched at 0/0 with count 311, Wii 576i
7/2 with count 310, Wii 480i 7/2 with the count alternating 259/260.

## Open

- **WHICH SOURCES MAY TAKE 0/0 IS NOT SETTLED, AND THE GATE AS LANDED IS
  WRONG.** It keys on `Tv5725::EqualisationMeasurement`, which sees only
  half-line pulses and is blind to serration -- and the Wii at 480p carries no
  equalisation while failing at 0/0 outright. It is not reaching that state
  today only because the probe reads across a divider change there and returns
  a false answer that happens to be the safe one.
  [`the-equalisation-probe-cannot-see-serration.md`](the-equalisation-probe-cannot-see-serration.md)
  has the measurements and the gate that matches every state measured good.
- **The mechanism.** The shear is the vertical sync being detected about 32
  lines late, and nothing measured says why holding the PLL across the
  preceding seven lines makes that more likely.
  **What is now ruled out is PLL starvation**: with the pair at 0/0 on the RISC
  PC, disabling the coast generator outright moves the count not at all and
  `STATUS_SYNC_PROC_HTOTAL` holds the divider through every combination, so
  nothing was starving.
  [`what-the-coast-pair-actually-holds.md`](what-the-coast-pair-actually-holds.md).
- **What the post lines do to an unheld count**, which is the 69-fold
  difference between 0/0 and 0/2.
