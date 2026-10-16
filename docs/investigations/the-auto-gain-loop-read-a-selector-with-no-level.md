# The auto-gain loop read a selector with no level on it

`ADC_RGCTRL`/`GGCTRL`/`BGCTRL` are the converter's analog gain, larger meaning
less gain. The colour space seeds them -- 0x33 component, 0x7B RGB -- and the
auto-gain loop is what is supposed to refine them. It never did: the seeds stood
unchanged for the life of a boot, clipping the top of the range on both colour
spaces.

**The loop ran. Every gate passed. It was reading a signal that carries no
level, and a field declaration is what put it there.**

## The gates are not the fault, and a marker proves it

The call site wrote `DEC_TEST_SEL`, a field in `s5_1F` that the datasheet gives
four bits -- bit 0 the measurement path's enable, the top three one of eight
test logics. Whatever its width, writing it leaves a signature in those bits.

So park a different logic selection there over HTTP and read the byte back: it
is overwritten within one read. The gate chain runs continuously, and every
hypothesis about `acquiredPasses()`, `clampPlaced()` or the SOG-bad interrupt is
answered at once, with no firmware build.

## A field declared one bit wider selected a different signal

| | upstream `1.3` | the fork |
|---|---|---|
| `DEC_TEST_ENABLE` | `s5_1F[3]`, commented "fake name" | `s5_1F[3]` |
| `DEC_TEST_SEL` | `s5_1F[6:4]`, three bits | `s5_1F[6:3]`, **four** |

The datasheet's own description is the four-bit one, so widening it reads as a
correction. It is not: the two names then OVERLAP, and every call site that asks
for a logic by number writes the enable along with it. `DEC_TEST_SEL::write(1)`
stops meaning logic 1 and starts meaning logic 0 with the path switched on, and
`write(2)` means logic 1 with the path switched OFF.

Measured at `TEST_BUS_SEL` 0x0b on a clipping picture, `s0_2F` over 13 samples:

```
logic 0   0 32 64 96 128 160 192 224        a counter; the same at every gain
logic 1   1 127 128 129 130 226 255         a level, and it follows the gain
```

So the loop read a counter. Two call sites were hit:

- **auto gain**, which asked for logic 1 and got logic 0;
- **the ADC offset calibration**, which asks for 1, 2 and 3 to walk green, red
  and blue -- and whose `2` cleared the enable, so the red channel's search read
  a dead bus and kept whatever offset it started with.

Both are declarations away from working, and no check could see it: every name
resolved, every read-back agreed. **Ask in bits, not in names.**

The two fields are split again, `[6:4]` and `[3]`, each named for one of the two
roles the datasheet gives them. That is granularity rather than an alias --
the same licence `SDRAM_RESET_SIGNAL` has.

## The pad does not matter

Upstream also writes `PAD_BOUT_EN` 0 before reading and puts it back after,
where `TestBus::select()` drives it. That divergence is inert: at logic 1 on a
clipping picture, 0x7f appears in 3 of 20 samples with the pad off and 3 of 19
with it on.

## The levels are on three other selectors, and that is what the walk reads

Even on upstream's own path the trigger is weak. `s0_2F` at logic 1 carries a
seven-bit level under a flag in bit 7, and `== 0x7f` only matches when the flag
happens to be clear: at the shipped `RgbGain`, with the picture measurably
clipping, 18 samples contained no 0x7f at all.

So the walk reads elsewhere. RD-5725-1.1 tabulates no values for
`TEST_BUS_SEL`, so the selector carrying a channel's level is found rather than
read. Sweeping all 32 at a clipping gain and again at minimum gain leaves three
that move with it: **14, 15 and 16**, ten bits each, no flag.

Raising one channel's gain at a time on an RGB source names them:

| gain hot on | sel 14 | sel 15 | sel 16 |
|---|---|---|---|
| R | **0x3f9** | 0x36a | 0x398 |
| G | 0x380 | **0x3fb** | 0x379 |
| B | 0x38d | 0x3aa | **0x3fd** |

So 14, 15 and 16 are R, G and B. On a component source all three follow the
green gain alone, because Y is on green and the matrix puts it on all three.

The tap is downstream of the video processor's luma offset: `VDS_Y_OFST` 0x60
pushes all three to full scale. It is the emitted level, not the converter's own
sample, which is what makes it the right thing to judge -- and what means a
colour balance pushed to an extreme is in the measurement.

## Saturation is a band, and the band has to clear two artefacts

**The value a channel stops at is per channel and below full scale.** Measured
on the RGB input: 0x3fc, 0x3fd and 0x3fc, against 0x3ff on the component one. An
equality against the top of the range is therefore a condition that never holds
on one of the two inputs. `AutoGain::Saturated` is 0x3F8, and the gap below it is
wide -- at the knee the same three channels peak at 0x3e4, 0x3d3 and 0x3e2.

Two things read full scale without the picture clipping.

**A read taken straight after the selection comes back with every bit set.** It
masks to the top of the range and counts as a clipped sample, at any gain, a
fixed couple of times a pass. A millisecond's wait after `selectLevel()` removes
it -- the same wait `SyncOnGreen`'s separator reader already takes. An HTTP probe
never sees this, because every read it takes is a quarter of a second after the
selection, which is why the bench reading and the firmware's disagreed.

**The rest is a few samples a pass on whichever channel's turn they fall in.**
Measured past the knee: 2 to 3 of a pass's 72 samples, against 7 to 25 at every
gain below it. So the decision counts the whole pass rather than any one
channel, and `OverToStep` is 8 -- about a twelfth of the samples -- with a gap
either side of it.

**A pass that misses costs nothing and a pass that fires wrongly costs a step
nothing takes back**, which is what sets the bias. A single pass over the
threshold still crept the gain past the knee, a step a minute, on a picture that
had stopped clipping; it has to hold across two passes before the gain moves.

## The walk only backs off, and the seed is what makes that enough

A level below the band says nothing about how much headroom is left, so a source
showing a dark scene reads as weak and a loop that raised the gain on that
reading would clip the moment the scene brightened.

The seed is what makes a one-way walk sufficient: it is hot enough that a
standard source clips, and the walk takes it back until nothing does. A source
hotter or weaker than nominal lands where its own white does. That is also why
the seed has to reach the held gain rather than the register alone -- a gain
converged on one colour space and carried into another leaves a source wanting
MORE gain with no way back to it, so `ColourSpace` seeds through
`Adc::holdGain()`.

## What settles it is evidence, and settling is a long interval rather than a stop

A pass with nothing bright on it reports only that nothing bright was on screen.
That is not the same as a gain that fits, and a walk that settled on a run of
them would hold the seed and clip everything brighter that followed. So the
source's level counts as found only after `EvidenceRun` passes that BOTH reached
`EvidenceLevel` and asked for no step. A dark picture never settles the walk.

Settled, it looks every `SettledMs` rather than stopping. Stopping is the
tempting shape -- the gain is a fact about the source, like its line count, and
`VideoPath::armSolveForSource()` already re-determines it on every arrival and
every mode change. But the fact was established against whatever was on screen
at the time, and content brighter than that has to be answered. A long interval
gives both: measured, a hand-written gain of 64 under a settled walk is
corrected to 211 within 15 s and then held.

**A pass is not free**, which is why the interval matters at all. Three channels
of 24 reads and a settling wait each is tens of milliseconds, and a walk looking
at its stepping rate for ever spends that out of every loop: the frame time lock
reported `not armed: something keeps disturbing the lock` on a locked source
against a build that did.

## Headroom, and what it costs

The walk holds the picture's top `Headroom` below where a channel stops, so
content a little brighter than anything it has seen has somewhere to go.

Measured on `vga` at 320x256@50 with PM5544, whose greyscale staircase draws six
known levels 51 apart:

| gain | steps | top gap |
|---|---|---|
| 123, the shipped `RgbGain` | 25, 60, 111, 164, **236** | the top two have merged |
| 199, the knee itself | 24, 49, 89, 132, 177, 226 | 49 |
| 211, the knee plus headroom | 23, 47, 86, 128, 172, 219 | 47 |

At the seed only five plateaus survive: 204 and 255 land on each other. The
margin costs 7 of 255 at the top, under 3% of the range, and two of the six
gaps by a count each.

## What the console says

```
auto gain: peaks 1020/1021/1020 over 3/9/8, gain 123 -> 127
auto gain: settled at 211
```

The three peaks, the three counts of samples over the level, and the step; then
the value the walk ended on. A walk that is working shows a run of steps after a
source arrives and then one settled line. A walk reaching 255 is the settle
interval or the two-pass confirmation failing, a walk that steps for ever is
finding real clipping it cannot correct, and a walk showing nothing at all is
the gate -- which the marker above answers in one read.

## Not established

**What the all-bits read is.** It follows the selection rather than the picture,
and a millisecond's wait removes it; whether that is the mux settling or the
bus's own update is not known, and nothing downstream depends on which.

**What bit 7 of the logic-1 byte carries.** It gates half the samples at that
selector, which is why the walk reads 14, 15 and 16 instead.

**Why the two colour spaces want such different gains.** Component Y and RGB are
both nominally 0.7 Vpp, so a walk settling near 87 on one input against the
210s on the other says the two paths do not attenuate alike. The board is the
candidate and nothing here measures it.
