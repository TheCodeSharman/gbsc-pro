# The vertical capture origin follows the sync type

**THE TITLE IS THE REFUTED MODEL.** Both legs of it are withdrawn below and
neither is a displacement: the composite one was the short count, and the
sync-on-green one is a set of `IF_VB_ST` values composite sync will not take.
Nothing here licenses a vertical origin derived from the sync arrangement, and
the deleted `FrameLagUnits` must not be reinstated. The two sections after the
banners are the current reading; the rest is kept for its measurements.

**THE COUPLING IS WITHDRAWN ON COMPOSITE SYNC.** The title's claim held on two
arrangements when it was written and holds on one now: the composite leg was a
consequence of the short count, and restoring the count closed it.
`SourceMeasurement::reconciledFrame()` adds back the vertical sync the composite
counter loses, and the vertical placement is byte-identical across the two
arrangements as a result.

Measured on the RISC PC on `vga` at 800x600@60, `SYNC 0` against `SYNC 1`, one
cable and one raster:

| | `IF_VB_SP` | `IF_VB_ST` | `STATUS_SYNC_PROC_VTOTAL` | picture |
|---|---|---|---|---|
| separate (`SYNC 0`) | 21 | 625 | 627 | clean, full screen |
| composite (`SYNC 1`) | **21** | **625** | 623 | clean, full screen |

The count still reads four lines short on composite — that is the mode's own
vertical sync width and it is unchanged — but the placement no longer follows it.
The whole placement chain is identical across the two arrangements apart from the
horizontal capture pair, which is a separate error and is measured in
[the-composite-capture-window-sits-between-two-wrong-values.md](the-composite-capture-window-sits-between-two-wrong-values.md).

**AND THE SYNC-ON-GREEN LEG IS WITHDRAWN TOO: THERE IS NO DISPLACEMENT AT ALL.**
The seven units were never an origin error. `IF_VB_ST` takes a small set of
values the part will not accept on composite sync, the Wii's framing lands on
one of them, and 21/505 worked only by stepping off it. The rest of this page
predates that and is kept for the measurements in it; the section below is the
current reading.

## The window's END is the whole of it, and only certain values fail

The two edges are separable, and only one of them matters. Measured on the Wii
at 480p on `ypbpr`, one variable at a time, the other left where the engine put
it:

| `IF_VB_SP` | `IF_VB_ST` | picture |
|---|---|---|
| 21 | 505 | clean |
| 21 | **512** | wraps and rolls |
| **28** | 505 | clean |
| **28** | **513** | clean, full screen |

So the start being seven units later than the working record is inert, and the
engine's own start with the end moved by ONE unit is correct. A seven-unit
origin correction is not what this source wants and must not be reinstated.

`IF_VB_ST` is `ov + ev + 2` -- 30 + 480 + 2 = 512 on the Wii, 23 + 600 + 2 = 625
at 800x600@60, 33 + 480 + 2 = 515 at 640x480@60, each read back against the
register. The framing decides which value the solve lands on, and nothing in the
chain knows some of them are unusable.

## The unusable values follow composite sync

Each value written five times on the Wii and three on the RISC PC, leaving the
register and returning between trials, scored on the input formatter's vertical
reaching the pin:

| source | lines | `SP_SOG_MODE` | 511 | **512** | 513 | 514 | **515** | 516 |
|---|---|---|---|---|---|---|---|---|
| Wii `ypbpr` 480p, sync on green | 525 | 1 | 0/5 | **5/5 dead** | 0/5 | 0/5 | **4/5 dead** | 0/5 |
| RISC PC `vga` 640x480@60, `SYNC 0` | 525 | 0 | 0/3 | **0/3** | 0/3 | 0/3 | **0/3** | 0/3 |
| RISC PC `vga` 640x480@60, `SYNC 1` | 525 | 1 | 0/3 | **2/3 dead** | 0/3 | 0/3 | 1, 6, 2 counts | 0/3 |

Separate sync reads a clean ten transitions at every value; composite sync kills
512, cripples 515, and makes the counts wander. **The 525-line frame is not the
cause** -- the same raster on separate sync has no bad value anywhere in the
range -- and neither is the component input, sync on green being `SP_SOG_MODE` 1
like `SYNC 1`. A wider sweep at step 1 over 420..524 on the Wii found exactly
these two.

**It is not a band and not an edge.** 513 and 514 sit between the two failures
and are clean in every trial, so a window edge crossing the vertical sync
interval does not describe it. 512 is not a poisoned value in itself either: it
is fine on composite sync at 800x600, where the frame is 628.

**The rule is not known.** What is established is which arrangement carries it,
that it is per value rather than per region, and that it is reproducible in both
directions.

### The coast window moves the set, which is the lead worth following

The coast pair is what composite sync puts around the vertical interval, and the
unusable values move when it moves. Measured on the RISC PC at 640x480@60,
`SYNC 1`, automation frozen so the engine cannot restore the pair, each value
written three times and scored on the worst reading (eighteen transitions at
`ms=150` is healthy):

| `SP_PRE_COAST`/`SP_POST_COAST` | 511 | 512 | 513 | 514 | 515 | 516 |
|---|---|---|---|---|---|---|
| **7/3**, what the engine writes | 16 | **0** | 12 | 10 | 4 | 14 |
| 0/0 | 14 | 10 | 7 | 12 | 17 | 8 |
| 4/4 | 13 | 10 | **0** | 14 | 10 | **0** |

At 0/0 nothing is dead; at 4/4 the dead values are 513 and 516 rather than 512
and 515. **So the value is not the property -- the coast is in the mechanism.**

**FREEZE FIRST, OR THE EXPERIMENT MEASURES NOTHING.** `/freeze?on=1`. Run
without it, every row reads back 7/3 whatever was written, because the engine
re-applies the pair within a pass -- and the readings then differ anyway, from
ordinary flakiness, which reads exactly like the coast having an effect.

The Wii answers the same way, A/B/A/B, three trials a cell, scored the same:

| coast | 512 | 513 | 515 |
|---|---|---|---|
| 7/3 | 0, 0, 0 | 17, 18, 18 | 0, 0, 2 |
| 0/0 | 18, 18, 16 | 18, 18, 18 | 16, 18, 18 |
| 7/3 | 0, 18, 0 | 18, 18, 18 | 0, 2, 2 |
| 0/0 | 18, 17, 18 | 18, 18, 18 | 18, 18, 18 |

So 0/0 clears both unusable values outright on both composite sources.

### And coasting 0/0 is REFUTED anyway, because the coast holds the count

**DO NOT MAKE THE COAST FOLLOW `serrated`.** It was built and flashed --
`applyForSyncType()` and `applySeparationThresholds()` taking the fact
`SyncProcessor::prepare()` is already handed -- and the Wii at 480p came up with
no picture at all. The count stops being steady:

```
source absent: 526 lines ... source acquired: 524 lines ... 525 lines
recovery: lift SOG floor at pass 2
scan: progressive, count 526, no settled count
scan: interlaced, count 525
deinterlacer: motion adapt engaged
```

At 7/3 the count is 524 in every sample and `VPERIOD_IF` agrees at 524. At 0/0
the count dithers 524/525/526, the scan decision follows it, the deinterlacer
engages on a progressive source, and `VPERIOD_IF` reads 995. Every register
either side reads healthy -- acquired, `PLLAD_MD` 1446 against
`STATUS_SYNC_PROC_HTOTAL` 1446, the frame time lock armed and steering.

**The coast is load-bearing on an unserrated composite source too**, so the
naming is not the whole story and neither is the NOR argument below: whatever it
does for the count, it does without serrations to skip.

**THE FREEZE IS WHAT HID THIS.** `/freeze?on=1` is required to hold the pair
against the engine, and it also stops the engine re-solving -- so the coast
sweep measures the blanking signal with the consequence for the count frozen
out. A coast reading taken frozen says nothing about what the engine will do
with the count afterwards. Sweep frozen to find the signal, then flash and watch
the console unfrozen before believing it.

### What is left

The trade is real and neither end of it is usable:

| coast | the count | the blanking signal |
|---|---|---|
| 7/3 | steady 524, `VPERIOD_IF` agrees | dead at `IF_VB_ST` 512 and 515 |
| 0/0 | dithers 524..526, scan flips | live at every value in 511..516 |

So a fix has to keep the coast and keep `IF_VB_ST` off the values that fail
under it. Two shapes, neither tried:

- **A coast that does both.** 12/12 read healthy on the RISC PC frozen; whether
  it holds the count is unmeasured, and the engine writes one pair so this is a
  flash per value rather than a sweep.
- **Check the signal in the solve.** The engine already has the instrument --
  `TestBus::selectInputVsync()` and `debugPinPulseEdges()` are what
  `FrameSync::bothVsyncPeriodsReadable()` uses -- so the capture window's
  vertical pair can be verified after it is written and nudged where the
  vertical does not come back. That needs no rule for the bad set, which is
  what makes it worth more than a nudge by a constant.

### Why the coast is a candidate at all

[the-risc-pc-composite-sync-is-not-serrated.md](the-risc-pc-composite-sync-is-not-serrated.md)
measures the bench source's composite sync as VIDC20's **NOR** form, whose
vertical interval is one flat level -- the horizontal edges inside it are absent
rather than attenuated. There are no serrations to coast through, so a seven-line
pre-coast on that source discards seven lines of good horizontal sync just before
the vertical interval instead of protecting anything.

The arithmetic puts that where the failures are. With the counter zeroing on the
vertical sync's trailing edge, `IF_VB_SP` 31..33 matches DMT 640x480@60's
33-line back porch, active ends near count 513, and a seven-line pre-coast
covers roughly counts 516..522 -- adjacent to the values that fail.

**And `serrated` reaches none of the three writers.** `SyncProcessor::prepare()`
is handed it, `applyPulseIgnore(csync, serrated)` takes it, and the coast pair is
written from `csync` alone by `applyForSyncType()`, `applySeparationThresholds()`
and `widenCoast()` -- under constants named `SerratedPreCoastLines` and
`SerratedPostCoastLines`. `SyncProcessor.h` states the pair is
`applyForSyncType()`'s alone and records what a second writer cost, so the
single-owner rule it sets out is already broken by two more.

## The failure is silent everywhere except the test bus

At an unusable value the vertical blanking is never asserted. The input
formatter's vertical carries **0 transitions at 0.00% duty** where the blanking
fraction predicts 6.3%, and that prediction matches the measurement to about
0.3% at every value that works -- 7.86% against 7.63% at 505, 55.6% against
55.2% at 256.

What that costs is a second fault on top of the wrap. `TestBus::selectInputVsync()`
selects exactly this signal, so `FrameSync::vsyncEdges()` cannot read an input
period, `init()` never arms, the Si5351 is never steered, and the output
free-runs -- 1601 x 1124 at the unsteered 108.0206 MHz is 60.027 Hz against the
source's 59.940, which laps the frame every 11.5 s. **The picture rolls as well
as wrapping, and the roll is downstream of the blanking rather than a fault of
its own.**

Nothing else sees it. `STATUS_IF_VT_OK` reads 1, `VPERIOD_IF` reads a correct
524, `STATUS_SYNC_PROC_VTOTAL` reads 524, and a raw dump of s1_18..s1_23 either
side of the write shows only the two bytes of the field moving. `/framesync`'s
`ready` is no use as an oracle either: it latches on first arming and stays true
across a value that has since killed the signal.

**The transition count IS the oracle**, which supersedes needing a photograph
and a settle for this fault:

```sh
curl 'http://<ip>/testbus?ms=150&if=3'     # tb,0 is the input formatter's vertical
```

Sixty transitions in 500 ms is healthy, zero is the fault, and `tb,2` -- the
VDS's output vsync, swept in the same pass -- is the control that says the pin
and the pad are working. `sweep_vb_st.py` drives it.

`VideoSourceLine::frame()` builds the vertical line with no sync interval at all
-- no `syncUnits`, no head blanking -- while the horizontal line is built by
`forDuty()` from the *measured* hsync pulse. That asymmetry is real and is
described below; it is no longer offered as the explanation for anything.

## What it replaced

`VideoSourceLine::FrameLagUnits` held **-7** counter units and was applied in four
places across two classes: `videoAt()`, `fractionAt()`, `firstCapture()` and
`lastReachable()`. It was removed on the premise that what it compensated for --
both vertical placements counting the vsync pulse as leading blanking while the
counter zeroes on the pulse's trailing edge -- had been fixed at its source.

That premise is true for separate sync. It is a statement about how vertical sync
is *extracted*, and composite sync and sync-on-green extract it differently, so
the correction lands in the wrong place on both.

## The measurement

One input, one cable, one raster, `vga` at 800x600, the sync type the only
variable -- the RISC PC sets it from CMOS, so `SYNC 0` and `SYNC 1` are one
command apart. **This is the reading the count restoration superseded**, kept
because it is what the displacement looked like before the cause was found:

| sync type | `IF_VB_SP` | `IF_VB_ST` | `STATUS_SYNC_PROC_VTOTAL` | picture |
|---|---|---|---|---|
| separate (`SYNC 0`) | 21 | 625 | 627 | clean, full screen |
| composite (`SYNC 1`) | 36 | 622 | 623 | top of the card absent, content pushed down and right |

The frame count itself differs, 623 against 627, so the vertical measurement was
not merely displaced on composite sync -- the counter was counting a different
frame, and that is the whole of what the composite displacement was. Reading 36
as an origin error rather than as a denominator error is what cost the sessions
this page records.

On the Wii at 480p on `ypbpr`, sync on green, the same solve either side of the
removal:

| | `IF_VB_SP` | `IF_VB_ST` | horizontal | picture |
|---|---|---|---|---|
| with the lag | 21 | 505 | 99 / 1315 | clean, full screen |
| without it | **28** | **512** | 99 / 1315 | torn, wrapping at a moving seam |

Same window height, both edges seven units later, horizontal untouched. **The
reading is sound and the conclusion drawn from it is not**: moving both edges
together cannot say which one carries the fault, and separating them puts all of
it on the end. The lag was worth nothing on that source beyond stepping the end
off 512.

## Why a register dump cannot see it

`/geometry` reports the **framing** -- where the user put the picture as a
proportion of the source -- and the framing is identical either side of the
change: `ov` 30, `ev` 480, `ch` 1449, `cv` 525 on both. The displacement lands
only in `IF_VB_ST`/`IF_VB_SP`, which `/geometry` does not carry. Every other
register in the solve is byte-identical, `PLLAD_MD` 1448 against
`STATUS_SYNC_PROC_HTOTAL` 1448 and `HPERIOD_IF` 214 among them.

The picture needs a settle: for about a minute after the source is acquired the
output is blank -- white on one build, black on another -- and a photograph taken
when `state` first reads `acquired` shows nothing wrong on a build that is badly
broken. Judging a frame taken at the moment of lock produced false verdicts in
both directions. **The picture is no longer the only instrument**, and the test
bus above needs neither a settle nor a camera.

## What a bisect costs here, and the oracle it needs

Acquisition on `ypbpr` is itself unreliable across a wide span of history --
three consecutive input bounces on one build gave a grey field, then no signal,
then a solve stuck on the previous input's geometry. **Acquisition rate is not a
usable oracle.** Picture quality after a settle is: every build tested was
either clean on every acquire or torn on every acquire, with no build sitting
between.

## The magnitudes are not one number, and one of them is not a magnitude

Composite sync counted a shorter frame, 623 against 627, and that was the whole
of its displacement. **The Wii's is not a displacement at all** -- the section
at the head of this page separates the two window edges and puts everything on
the end landing on a value composite sync will not take. So there were never two
corrections to reconcile; there was one correction and one unusable register
value.

**The quantity that fixed composite reaches nothing here.** The Wii's sync on
green is serrated, so the counter loses nothing: `VPERIOD_IF` 524 equals
`STATUS_SYNC_PROC_VTOTAL` 524 and the reconciliation correctly yields 0. There
is no shortfall on that source to add back, and none is wanted.

**The A/B this page said did not exist is now on the bench.** DMT 640x480@60 is
525 total lines at 59.94 Hz, the Wii's vertical raster exactly, and the RISC PC
carries it -- `MODE X640 Y480 C256 F60`, either sync type, one command. That is
what established the fault follows composite sync rather than the mode or the
input, and it is how any further claim about this should be checked.

**The source's vertical sync width is not the quantity** either, which is the
rule a placement would naturally be written against: it matches neither the
vsync width nor either porch on any mode measured.

## The vertical axis consults no polarity, where the horizontal does

This is the gap the origin work has to close, and it is a property of the code
rather than of a source.

`forDuty()` takes the horizontal origin end from the **measured** pulse, so the
horizontal axis adapts to whichever edge the arrangement presents.
`VideoSourceLine::frame(units)` asserts a trailing-edge origin unconditionally
and reads no polarity at all — `STATUS_SYNC_PROC_VSPOL` reaches nothing. So the
two axes are built on different rules, and the vertical one cannot express a
source whose counter zeroes on the other edge.

**`STATUS_SYNC_PROC_VSPOL` does not vary with the sync type on the RISC PC**, so
it is not the discriminator the asymmetry might suggest: measured 1 on both
`SYNC 0` and `SYNC 1` at 800x600@60, while `STATUS_SYNC_PROC_HSPOL` goes 1 to 0
across the same change. The bit is still worth reading — it agrees with the
standard on each mode, unlike `STATUS_SYNC_PROC_VSACT` — but a vertical origin
keyed on it would read the same on both arrangements here and could not be
tested from this end.

## The shape the fix has to take

**Not an origin.** Deriving a vertical origin from the sync arrangement and
handing it to `CaptureWindow` builds a mechanism for a displacement that the
edge-separation measurement says is not there, and it would leave the solve free
to land on an unusable value from some other framing.

What the solve needs is for `IF_VB_ST` not to take a value the part refuses on
composite sync. Until the rule behind those values is known that is a list and
not a derivation, so it is worth knowing first what the list is: whether it
moves with `IF_VB_SP`, with the frame, or with the coast window.

Restoring the deleted constant would restore the picture and reinstate exactly
the arrangement that hid the fault -- and on this reading it would fix it by
coincidence, which is worse.

[the-capture-lag-was-the-retiming-bypassed.md](the-capture-lag-was-the-retiming-bypassed.md)
is the horizontal constant that turned out to be one misconfigured bit;
[the-vertical-capture-window-is-placed-late.md](the-vertical-capture-window-is-placed-late.md)
is where -7 came from and how a one-source-line feature made it measurable;
[framing-is-anchored-to-a-measured-pulse.md](framing-is-anchored-to-a-measured-pulse.md)
is the horizontal axis doing what the vertical one does not.

## The origin is measurable, and the test bus already carries it

The input formatter's vertical signal -- `IF_TEST_SEL` 3 driven onto
`TEST_BUS_SEL` 0, which is what `TestBus::selectInputVsync()` selects -- is a
vertical-rate pulse whose high time is the source's **vertical blanking
interval**. It is the only vertical-rate signal of the sixteen: every other
`IF_TEST_SEL` value reads line-rate, faster, or static.

Measured at `ms=100`, normalised per pulse, three repeats:

| sync | mode | lines per pulse | the mode's blanking |
|---|---|---|---|
| separate | 640x480@60 | 41.3  41.4  41.3 | 45 |
| separate | 800x600@60 | 26.1  26.2  26.3 | 28 |
| separate | 1024x768@60 | 34.1  34.1  34.3 | 38 |
| composite | 800x600@60 | 43.0  47.9  43.0 | 28 |

**It tracks the mode across three different blanking values, to ±0.2 lines**,
which is what says it is a measurement of the source rather than a constant.

**THE SAME RASTER READS 26 LINES ON SEPARATE SYNC AND 43 ON COMPOSITE.** Nothing
moved but the sync type -- one input, one cable, one mode.

**That seventeen-line difference is not the displacement**, which is the reading
this page carried and which the count restoration refuted: the composite
displacement was the short count, and with the count restored the vertical
placement is identical on both arrangements while this reading still differs by
seventeen. So the two are not the same quantity, and what the difference is
remains unexplained. It is still the signal that separates the arrangements, and
it is no longer a candidate for a correction term.

**It is not the window the engine wrote, played back.** Panning vertically
through the pads moved `IF_VB_SP` 21 -> 0 -> 25, a 21-line excursion, and the
measurement moved 24.4 -> 25.6 -> 26.1 -- under two lines. A single
`setfield.py` write of `IF_VB_SP` does zero the signal and it does not return on
restore, which reads as the same thing and is not: the write leaves the block
needing a reconfigure, and concluding "it follows our window" from it is wrong.

## What the measurement does not yet explain

**Sync on green looks ordinary on it.** The Wii at 480p reads ~42 against the
mode's 45, the same two-to-four line deficit every separate-sync mode shows --
so nothing here distinguishes the arrangement that carries the unusable values
from the one that does not. It is the high time that reads ordinary; whether the
signal is there **at all** is the discriminator, and that is the reading the
head of this page is built on.

**THE SIGNAL IS THE PORCHES, NOT THE WHOLE BLANKING.** There was never a
deficit; the comparison was against the wrong quantity. Measured off the CPU
cycle counter at 800x600@60 -- 24 samples, no failures, median **24.00** lines,
stdev 0.27 -- against `RetroScaler-Acorn.mdf`'s own
`v_timings:4,23,0,600,0,1`: back porch 23 plus front porch 1 is **24**, and the
4-line sync pulse is what the high time leaves out.

The MDF is the check that settles it, because it states what the machine
emits rather than what a standard says it should: totals and blanking agree with
DMT on all three modes, so only the quantity being measured was wrong.

**The polling reading is biased and must not be trusted to a line.** `/testbus`
counts samples across a fixed window and normalises by the pulses in it, so a
partial pulse at either edge inflates the answer: it gave 26.1 where the edge
measurement gives 24.00. It is fine for telling 26 from 43 -- which is what
separated the sync types -- and not for telling the porches from the blanking.

## The edge-timed instrument is not reliable yet

Timing the pulse off the cycle counter -- one rising edge, the falling edge, the
next rising edge -- is stable on ONE of three modes:

| mode | `STATUS_SYNC_PROC_VSPOL` | median lines | stdev |
|---|---|---|---|
| 800x600@60 | 1 | 24.00 | 0.46 |
| 640x480@60 | 0 | 93.81 | **42.5** |
| 1024x768@60 | 0 | 44.86 | **73.4** |

**It works on the positive-going mode and fails on both negative-going ones**,
with a spread larger than the quantity. The signal is not the problem -- the
polling reading is steady to ±0.2 lines on all three -- so it is the edge
capture. The chain detaches and re-attaches the interrupt from inside each ISR,
which is where an edge on the opposite phase is dropped or double-counted, and
taking the shorter of the two intervals cannot rescue a pair that was mistimed.

Until that is fixed the measurement is a bench instrument and not something the
engine can solve from.

**THE EDGE-TIMED INSTRUMENT IS GONE AND MUST NOT BE REBUILT.** It measured a
blanking-shaped quantity off `IF_TEST_SEL` 3, and an origin needs the *pulse*, so
it could not answer the question it was built for whatever the edge capture did.
`VPERIOD_IF` against `STATUS_SYNC_PROC_VTOTAL` gives the vertical sync directly,
is proven over twelve modes, and is what
`SourceMeasurement::reconciledFrame()` uses. `TestBus::selectInputVsync()`
remains and is FrameSync's, not this measurement's.

**The sync processor's bus is not an alternative.** `SP_TEST_MODULE` 7 carries a
vertical pulse on sync on green and reads nothing at all on separate sync; 4 and
6 are line-rate or faster on both. No stage of it exposes the source's vertical
on separate sync, which is why the input formatter's signal is the one to build
on.

## Reading it costs one window, and the window has to be chosen

The count is `transitions / 2` complete pulses inside the sweep, so the high
time has to be divided by that before it means anything: at `ms=25` a 60 Hz
source gives one pulse or two depending on phase, and summing both reports the
blanking as double. `ms=100` gives six and is stable to a fifth of a line.

## The composite path has a second error: the frame it solves against is short

`STATUS_SYNC_PROC_VTOTAL` reads short on composite sync by the mode's own
vertical sync width -- already established on four modes, with the mechanism and
the signal form that causes it, in
[the-risc-pc-composite-sync-is-not-serrated.md](the-risc-pc-composite-sync-is-not-serrated.md).
1024x768@60 adds a fifth point and changes nothing: 805 separate, 799 composite,
against a 6-line pulse.

**The coast is not the correction, and reaching for it is the tempting move.**
It is 7 + 3 on this bench against a shortfall of 4, so adding it back
over-corrects by six. Coast stops the serrations disturbing the PLL; on a source
that has none it has no count to restore.

What matters here is that this is a SECOND error on the composite path rather
than the same one twice. The engine solves against a frame four lines shorter
than the source's, so a placement taken as a fraction of the frame has the wrong
denominator as well as the wrong origin -- which is why composite sync is
displaced further than sync on green, and why the two are not one correction.

**The reconstruction is bounded by the signal form.** `VTOTAL + vsyncWidth` is
the true frame only where the vertical interval carries no horizontal edges. A
serrated source keeps feeding the counter through it, so the shortfall is not
there to add back, and nothing here has been measured on one.

## The vertical polarity is measured, agrees with the standard, and is not needed

`STATUS_SYNC_PROC_VSPOL` reads 1 on the one DMT mode of the three whose vertical
sync is positive-going and 0 on both negative ones, so unlike
`STATUS_SYNC_PROC_VSACT` it is a status bit worth quoting.

**The measurement does not need it.** `IF_TEST_SEL` 3's high time is the blanking
on all three modes -- 3.84%, 7.86%, 4.24%, never the complement -- so the input
formatter's vertical is already polarity-normalised and the source's polarity
never reaches the reading. Taking the SHORTER of the high and low intervals
makes that structural rather than incidental, at no cost, which is what the
horizontal axis settled on for the same reason.
[the-duty-is-the-shorter-interval.md](the-duty-is-the-shorter-interval.md)

**`VPERIOD_IF` completes a measurement only on composite sync** --
`STATUS_IF_VT_OK` 0 with the register wandering 154..248 as debris on separate
sync, 1 with it steady on composite. And across two acquisitions of the same
composite mode it read 1255 and then 615, a factor of two apart.

**The factor of two is resolved rather than disqualifying, and this register is
now the cross-check.** `SourceMeasurement::reconciledFrame()` takes whichever of
one or two puts `VPERIOD_IF + 1` a non-negative distance of at most eight lines
above `STATUS_SYNC_PROC_VTOTAL + 1`, and that distance is the vertical sync the
composite counter lost. Eight because the widest vertical sync in the DMT set
the bench carries is seven. At 800x600@60 on `SYNC 1`: `VPERIOD_IF` 1255 taken
at two gives 628, against a counted 624, for a shortfall of **4** -- the mode's
own pulse width.

Two constraints come with it. The width is **held once two readings agree**
rather than recomputed, because `VPERIOD_IF` spans two registers and tears, and a
per-sample correction moves the count by the whole vertical sync whenever a
reading is refused -- which the presence poll reads as the source moving, giving
an endless acquire/absent loop. And it does not apply to an interlaced source and
does not claim to: there the counter holds a field where `VPERIOD_IF` holds a
frame, no factor reconciles them, and the raw count stands.
