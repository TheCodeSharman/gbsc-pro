# The vertical capture origin follows the sync type

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

**Sync on green still wants the placement seven units earlier**, and that row
stands unexplained. The rest of this page is about that arrangement.

The vertical capture window is placed seven counter units late on sync on green:
the picture tears and wraps. Separate sync is correct, and it is the only
arrangement the placement was measured on.

`VideoSourceLine::frame()` builds the vertical line with no sync interval at all
-- no `syncUnits`, no head blanking -- while the horizontal line is built by
`forDuty()` from the *measured* hsync pulse. The vertical axis therefore has no
representation of where the input formatter's line counter zeroes relative to the
source's vertical sync, and that origin is not the same on every sync type.

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

Same window height, both edges seven units later, horizontal untouched. So
sync-on-green wants the placement seven units earlier than separate sync does,
which is the whole of what the deleted constant was worth on that source.

## Why a register dump cannot see it

`/geometry` reports the **framing** -- where the user put the picture as a
proportion of the source -- and the framing is identical either side of the
change: `ov` 30, `ev` 480, `ch` 1449, `cv` 525 on both. The displacement lands
only in `IF_VB_ST`/`IF_VB_SP`, which `/geometry` does not carry. Every other
register in the solve is byte-identical, `PLLAD_MD` 1448 against
`STATUS_SYNC_PROC_HTOTAL` 1448 and `HPERIOD_IF` 214 among them.

The picture is the only instrument, and it needs a settle: for about a minute
after the source is acquired the output is blank -- white on one build, black on
another -- and a photograph taken when `state` first reads `acquired` shows
nothing wrong on a build that is badly broken. Judging a frame taken at the
moment of lock produced false verdicts in both directions.

## What a bisect costs here, and the oracle it needs

Acquisition on `ypbpr` is itself unreliable across a wide span of history --
three consecutive input bounces on one build gave a grey field, then no signal,
then a solve stuck on the previous input's geometry. **Acquisition rate is not a
usable oracle.** Picture quality after a settle is: every build tested was
either clean on every acquire or torn on every acquire, with no build sitting
between.

## The magnitudes are not one number

Sync on green wants the placement seven units earlier than separate sync.
Composite sync counted a shorter frame, 623 against 627, and that was the whole
of its displacement -- so the two were never the same correction, and restoring
the count fixed one of them and left the other exactly where it was.

**The quantity that fixed composite is not the quantity sync on green needs.**
The Wii's sync on green is serrated, so the counter loses nothing: `VPERIOD_IF`
524 equals `STATUS_SYNC_PROC_VTOTAL` 524 and the reconciliation correctly yields
0. There is no shortfall on that source to add back.

**And seven is not established as the right magnitude.** A wrap is a binary
test: writing 21/505 by hand, vertical only with `IF_HB_SP2` untouched, cleans
the picture completely, and writing 28/512 wraps it. That says 28 is wrong and 21
works, and nothing at all about 20 or 22. There is one Wii mode and one sync
arrangement behind it, so there is no A/B. Switching the Wii to 480i and 576i
with 480p as the control in the same sitting is what would give one.

**The source's vertical sync width is not the quantity**, which is the rule a
placement would naturally be written against and which the section below
refutes: the displacement matches neither the vsync width nor either porch on
any mode measured.

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

A constant applied at four call sites, compensating for an error owned by a
different class, is not something any of those call sites can be read against.
The origin belongs to whatever determines the sync arrangement, derived once from
the measurement and handed to `CaptureWindow` the way the hsync pulse already is
-- `docs/sync-type-selection.md` is where that choice is made.

Restoring the constant would restore the picture and reinstate exactly the
arrangement that hid the fault.

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
so this measurement says nothing about the seven units the Wii wants, which is
the one displacement still open.

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
