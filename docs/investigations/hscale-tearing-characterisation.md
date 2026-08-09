# The HSCALE tearing, characterised

## FOUND: `PB_FETCH_NUM`, 2026-08-09

**`PB_FETCH_NUM` 256 -> 200 clears the fault completely.** Photographed at
`zh` 250, 28 units inside the 66-unit torn band:
`snapshots/pb-fetch-num-200-clean-and-correct-2026-08-09.jpeg`, registers in
`snapshots/pb-fetch-num-200-no-corruption-2026-08-09.json`.

`PB_FETCH_NUM` is how much the playback stage pulls from SDRAM per memory
request. It sets the burst length, which sets the request rate — and the
interference is a **beat between the capture write bursts and the playback
read bursts on the memory bus.** Change the burst size and the beat changes.

That retro-explains every surviving observation, including the two that killed
the hypotheses before it:

| observation | under the arbitration beat |
|---|---|
| `VDS_HSYNC_RST` rolls the pattern but not the picture | the output line length sets the playback request rate; detune it and the beat drifts while the picture stays locked |
| `PB_CAP_OFFSET` does nothing to the pattern | a phase offset, not a rate. A beat's rate does not care where the pointers start |
| capture width, HSCALE and output hsync all flip it | each changes one of the two request rates |
| no single quantity predicts it | it is the ratio of two rates, not either one |
| worst at line start, decaying across | the burst pattern realigns once the line is running |
| irregular bands, 1 to 66 units wide | where the beat lands inside the visible region |
| magenta the source does not contain | data fetched from the wrong address |

### The `PB_FETCH_NUM` = 100 photo is a TRAP, and the test card caught it

The first value tried was 100, and it looked clean —
`snapshots/pb-fetch-num-100-clears-the-interference-2026-08-09.jpeg`. It was
not. `TestPat.bas` names its own blocks by count — *1 red bottom, 2 green
left, 3 blue top, 4 yellow right*, one circle, one centred `BAND n` — and that
photo shows **four** blue blocks, **four** circles and **four** `BAND 8`s. The
line was repeating. A repeated fragment is clean because it is not the
picture.

At 200 the counts are 3/2/4/1 with one circle and one `BAND 8`: the correct
image *and* no interference. **Without a test card that carries its own key,
100 would have been recorded as the fix.**

### `PB_FETCH_NUM` is inherited, never computed

`grep` the tree: **the firmware never writes `PB_FETCH_NUM` anywhere.** It
comes from the preset table and nothing has ever questioned it. 256 is an
inherited constant, in a register the geometry engine does not own — precisely
the class CLAUDE.md's "compute the geometry, never inherit it" rule exists for.

The firmware does know the two playback registers are related:

```c
// gbs-control.ino:4136
if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4) {
    GBS::PB_CAP_OFFSET::write(GBS::PB_FETCH_NUM::read() + 4);
}
```

Gated on standards 3/4, which this source is not, so that invariant has never
run here. The unit is currently fetch 200, offset 254; the firmware's own rule
would make it 204.

### The working configuration, 2026-08-09

`snapshots/pb-tuned-working-2026-08-09.json`. Four registers off their
inherited values, no crop:

```
PB_FETCH_NUM    200      was 256   the fix -- playback burst length
PB_CAP_OFFSET   250      was 256   NOT critical: clean anywhere 190..256
VDS_HS_ST  12 .. VDS_HS_SP 100     was 8..56.  Pulse width 88, back porch 24
VDS_VS_ST       30                 the per-frame phase
```

**`VDS_HS_SP` must stay below `VDS_DIS_HB_SP`, and the ceiling MOVES.** Past it
the hsync pulse overlaps the display window and is *blanking the left edge of
the picture* — the artefact vanishes because the pixels holding it are no
longer shown, and it does not look like a crop, it looks like the tuning got
better.

The ceiling is not a constant: `VDS_DIS_HB_SP` is computed by the geometry
engine and moved from 104 to 124 during this evening's tuning. So the rule is
`VDS_HS_SP < VDS_DIS_HB_SP`, never a remembered number — **any pad press can
slide the window and turn a legitimate pulse setting into a left-edge crop with
nothing having been touched.** Settled at `HS_SP` 100 against a window at 124,
a back porch of 24.

There is a second version of the same trap that no register reveals: with a
thin back porch **the display itself** may clip the left edge, hiding the
artefact just as effectively. `TestPat.bas` settles both cases in one glance —
its green 1-pixel frame sits on the outermost pixels, and *"a missing side
means that edge is being clipped"*. Green line present down the left means
nothing is being hidden and the improvement is real.

A thin back porch is also tight for the MS9288A, which samples the analog
output and re-locks to it — the wedge-with-perfect-registers failure mode.

**None of this survives a preset load.** The firmware writes none of these four
except `PB_CAP_OFFSET`, and that only for `videoStandardInput` 3/4.

### AND YET IT HAS TO BE A CONSTANT: the register cannot be rewritten live

Settled 2026-08-09 by building it. `Geometry::write()` recomputed
`PB_FETCH_NUM` per framing and **the picture flickered on every pad press** --
the fix has to be a known-good value held constant. Rewriting that register
reprograms the playback FIFO while the picture is being
read out of it, and that is visible however good the new value is.

Two versions were withdrawn before the constant:

1. **A line**, 204..256 across capture 737..1185. It put 236 at capture 1009,
   which **wrapped the picture** -- the start of the frame reappearing down the
   right-hand side. That is a second constraint nobody had modelled: playback
   must fetch enough per line to *cover* it, a FLOOR, and the floor rises with
   the capture width because a wider capture means more write requests
   competing for the bus. 256 at that same width is clean, so the floor near
   1009 units sits between 236 and 256 and the line walked under it.
2. **A step** between the two measured values. No wrap, still flickered,
   because the flicker is the *write*, not the value.

So the section above is right that no constant is clean at every zoom, and it
is still a constant, because the alternative is worse. The engine now writes it
only when it differs from what the register holds -- a repair after a preset
load, silent on every ordinary press.

**What would unlock a per-framing value is a vblank-synchronised write**, not a
better formula. Until the firmware can defer a register write to the vertical
blanking interval, anything that varies with the framing flickers.

`Tv5725::Memory` holds **204**, and which number that is has changed twice —
see the section below, because the difference between 200 and 204 is the
difference between a measurement and a leftover.

### The shipped constant is 204, not 200

The glitch clears as soon as `PB_FETCH_NUM` reaches 204. Read back off the unit
the same evening: `s4 r0x39 = 0xcc`.

**204 is the measurement. 200 never was.** 204 is the fetch that was clean at
capture 737 while 256 tore — the lower end of the withdrawn line above, and the
only value at that end anybody has ever put into the register by hand. When the
line was withdrawn, its lower *clamp* was kept as the constant, and the clamp
was 200 — a number produced by arithmetic that had already been deleted. It sat
in `Memory.h` for a day looking exactly as measured as 204 does.

Nothing about the 200 photographs is retracted: they are of 200, they show the
correct block counts, and the shredding at HSCALE 381 was seen at 200. The
correction is which number the firmware writes, and the rule is the general one
— **when the measurement and the model disagree, ship the measurement.**

The offset stays at 250: measured clean anywhere across 190..256 *against a
fetch of 204*, which is the pair those two halves were measured as.

**And it is now under an acceptance test rather than a comment.**
`test_a_zoom_press_leaves_the_playback_burst_at_the_measured_value` presses the
zoom pad and reads the register back;
`test_a_zoom_press_repairs_a_playback_burst_a_preset_left_behind` seeds 256/260
first — exactly what a preset load leaves, exactly the state every torn photo
here was taken in — and requires one press to clear it. The first test alone
would pass against a register nobody wrote, which is the fault that survived
three sessions.

### NO SINGLE CONSTANT CAN BE CLEAN AT EVERY ZOOM

After tuning, the glitch returns on a few creeps of zoom, and that is structural
rather than a tuning failure. The write rate tracks the
capture width, which sweeps 70% across the zoom range; the read rate is fixed.
So the beat phase sweeps with the zoom, and any fixed `PB_FETCH_NUM` clears a
range of framings and fails somewhere else.

**So the fix is a computed value, not a better constant.** `PB_FETCH_NUM`
should be derived from the capture width in the geometry engine, alongside
everything else that is computed rather than inherited — which is exactly the
rule this register has been violating, and why 256 was wrong: it was right for
whatever mode its preset table came from.

**And that changes the experiment.** Sweeping the zoom at a fixed fetch
measures one slice of a two-dimensional space. What the engine needs is the
other axis:

> at each of five or six capture widths across the range, sweep
> `PB_FETCH_NUM` and record the window of values that are clean

Six columns rather than one row, 60-80 presses. Clean windows marching
linearly with capture width gives the firmware a formula; modular gives it a
modulus. Either produces a fix rather than a pass/fail. A variant of
`sweep_zoom.py` that steps `PB_FETCH_NUM` instead of the framing, with the same
one-key verdict, is the tool for it and is not yet written.

### What it IS, mechanically

The TV5725 scales through SDRAM, and two independent periodic request streams
share that bus:

- **Capture** writes incoming samples. Rate ∝ capture width × input line rate.
  The input line rate is fixed (VTOTAL 311 at 50 Hz, ≈15.6 kHz), so this tracks
  the capture width — the quantity that swings 70% across the sweep.
- **Playback** reads pixels for the output in bursts of `PB_FETCH_NUM` words.
  Rate ∝ produced width × output line rate, both essentially fixed. Across all
  493 readings `produced` moves only 1232→1244 px, **1%**, because the engine
  holds the picture the same size on screen while you zoom.

Two periodic streams into one arbiter is a **beat**. The phase advances a fixed
amount per line, and where it lands decides whether a playback burst is held
behind a capture burst. Held too long, the output FIFO underruns and the
display takes whatever is in it — not noise, a *specific wrong address*.

Everything falls out of that:

- **worst at line start** — the playback FIFO is emptiest just after the line
  boundary, so it has the least slack; once running it has depth and absorbs
  the jitter, hence the decay across the line
- **vertical envelope, nulls, sign reversal** — the phase advances slightly per
  line, so severity varies down the frame and crosses through zero
- **static** — input and output are phase-locked, so the beat frequency is
  exactly zero: a standing wave. Detune `VDS_HSYNC_RST` and it rolls.
- **magenta the source lacks** — a wrong address, not a degraded signal

**Not the memory bus running out of margin, and not a routing fault.** It tears
at capture 722..787 and is clean at 855 and above — *narrow* capture means
*fewer* writes, so the failures are at LOWER bus load, not higher. Add the
perfect determinism (`end-1` register-identical to `onset-1`, `zh` 220 vs 221
flipping on one unit and repeating), and the wide slack in the fix
(`PB_CAP_OFFSET` free across 190..256), and it behaves like arithmetic rather
than like marginal signal integrity. **A marginal signal gives noise; this is a
standing wave, and standing waves come from arithmetic.**

That may also retire the older reading of the left-edge bands — CLAUDE.md's
*"banded non-monotonic thresholds look like marginal signal integrity, in which
case the numbers are facts about this board"*. An arbitration beat produces
banded non-monotonic thresholds deterministically. If that is what they were,
the numbers are facts about the **preset tables**, not the board, and the fix
generalises rather than being unit-specific.

### The left residual has a SECOND handle, and it is a phase not a duration

`PB_FETCH_NUM` 200 left roughly 4 px of residual at the extreme left — the
worst case, first burst of the line, FIFO emptiest. **`VDS_HS_SP` = 56 clears
it**, with `VDS_HS_ST` 12: pulse width 44, back porch 48.

The tempting model is "back porch is the FIFO's prefill time, longer is
better". **It is wrong, and CLAUDE.md already contains the refutation:**
left-hand corruption once cleared by moving the pulse to **62..77**, a back
porch of 28 — *shorter*. Tonight 35 → 48 cleared it. Opposite directions, both
clearing.

So the hsync pulse position is a **phase** into the same arbitration — when the
playback line reset falls relative to the capture request stream. A phase
wraps, so several values work and there is no "more is better"; a prefill
budget would be monotonic. That is also the first mechanism ever offered for
the non-monotonic left-edge banding, which nothing has modelled.

**No crop is needed.** Blanking the first 4 px was considered and is not
required, which is the better outcome: those pixels are real picture.

### What 200 did not settle

Clearing came at **one** framing. The previously-torn band was `zh` 222..287, 66
units wide, and the fault has always come in bands, so a value that clears one
band may only have moved the beat. Sweeping it is how that gets answered:

```sh
python3 tools/gbsc-pro-hwtest/sweep_zoom.py --host <ip> \
    --step 1 --start 222 --prefix fetch200
```

66 presses over ground that was 66/66 torn. That sweep is what produced *no
single constant can be clean at every zoom* above, and it is why 204 ships rather
than 200.

Still open: what the right value **is** as a function of the mode, rather than by
hand. 204 works here; nothing yet says why, or what it should be for another
source.

---


Started 2026-08-09. **This is the durable record so the sweep does not have to
be done again.** `VDS_HSCALE` is swept on the OSD and the picture judged by eye;
every boundary called is recorded with the full register state beside it, and the
photograph or video is stored next to the row. Raw data:

```
tools/gbsc-pro-hwtest/snapshots/hscale-characterisation.jsonl
tools/gbsc-pro-hwtest/snapshots/hscale-*.jpeg  .mov
python3 tools/gbsc-pro-hwtest/characterise.py --table
```

## What is being characterised

A picture fault that appears at some scales and not others. It has been in three
handovers as "the interference patterns at some HSCALEs" and has never been
described precisely enough to test anything against, which is why the previous
attempt at a rule — the headroom rule — was retracted rather than refined.

**It is not the zigzag.** That was solved on 2026-08-09 (`IF_HB_ST` holding 1347
on a 1277 unit line) and it was one pixel, strictly alternating line to line, and
uniform down the frame. This is many pixels, ragged, and has structure down the
frame. Merging the two is a mistake that has already been made once; see
CLAUDE.md, "The zigzag is NOT HSCALE-banded, and that is measured."

## Method, and why each part of it is there

- **The verdict comes from the screen.** Nothing scores the picture
  automatically. The faults are things you look at, and a checker that guessed
  would be inventing the data the exercise exists to collect.
- **Snapshot at the moment of the call, before any analysis.** `peak-1` was
  snapshotted several minutes after the video was taken, which is how a row ends
  up describing a state the unit had already left. Snapshot first, read after.
- **Twenty-seven registers per row, not one.** An OSD sweep is *not* a
  single-variable sweep: a pad press recomputes every window, so the capture,
  the memory window and the display aperture all move along with the scale.
  "The glitch starts at HSCALE 795" is not a finding about HSCALE until the
  table can show which of them the fault actually follows.
- **Each row records whether the picture was wider than the display window.**
  That is what retracted the headroom rule: its readings were taken with 24 to
  163 px of picture outside the aperture, where the tearing it was meant to
  measure could not be seen. A reading through an edge nobody can see is not
  noisy, it is worthless.
- **One garbled INTERVAL is the unit of observation, not one bad scale.**
  A pass continues until the tearing completely stops, and scales where it
  almost cleared along the way do not end the interval. Near-misses inside an
  interval are recorded as `almost` rather
  than ending it — they may be the band structure the old headroom note saw, and
  discarding them would mean sweeping again to get them back.

## The shape of the fault

From `onset-1`, and unchanged in character at `peak-1`:

- **Horizontal displacement only.** Every vertical edge is combed; horizontal
  edges are clean. No vertical component at all.
- **The LEFT ~2/3 of the width**, not the whole width.
- **The boundary is a DIAGONAL.** Onset at the top of the frame, the affected
  region widens going down, a CLEAR band about 2/3 of the way down, then it
  starts again just before the bottom with the slope apparently reversed.
  Roughly 1.5 repeats down the frame.
- **The OSD overlay stays sharp** while the picture tears, in the same frame.
  It is still torn after the menu times out, so the OSD is not involved — it is
  a clean reference, and it puts the fault UPSTREAM of OSD insertion.
- **Magenta appears at the right edge, where the source has yellow and green.**
  Colour that is not in the source means data read from the wrong place, not a
  signal degraded in flight.

**A diagonal in a raster means a constant slip per line.** Noise does not draw
straight lines. Something drifts by a fixed amount every line and the point
where it goes wrong walks across the picture, drawing the boundary.

## The envelope's phase DOES move with HSCALE

This section previously said the opposite, on the strength of the shape looking
identical between HSCALE 619 and 652. That was over-claimed twice over: a few
percent shift in where the null sits is not something the eye catches, and two
readings 5% apart cannot bound a slope. It is now positively contradicted.

At the second garbled section, HSCALE 686, the affected piece is the one that
was clear in the previous stretch. **The null and the peak have swapped.** The band that was clean two thirds down through section 1 is the
worst part of section 2. So the vertical envelope's phase advances as HSCALE
does, and the "sections" are simply where its peaks land inside the visible
frame.

## The fault is CONTINUOUS, and only its visibility is banded

The other half of the same message, and it reframes three handovers' worth of
notes: *"there was a minimal amount of tearing the entire way to onset 2."*

`clear-1` was called clear at the time and is corrected to `almost` in the data.
**There is no clean scale between the two sections.** The fault is present
throughout at some amplitude; what is banded is whether you can see it.

That kills the picture of "bands of corruption in a sea of good" that the
headroom note and the left-edge corruption note both assume. It is one
continuously present error whose amplitude is modulated, and the bands are
contours of visibility, not of existence.

## The fault has two independent factors

That refutation is worth more than the hypothesis was, because it separates the
fault into two things that can be chased apart:

  * **A VERTICAL ENVELOPE, fixed.** Onset at the top, a zero crossing about two
    thirds down, restarting near the bottom, roughly 1.5 repeats per frame.
    Independent of HSCALE across the whole interval 619..652.
  * **A HORIZONTAL AMPLITUDE, set by HSCALE.** How many pixels each line is
    displaced by. This is what carries the envelope above or below visibility,
    and it is why the fault looks banded in scale at all.

The "widening wedge" is then the envelope growing past the threshold where the
eye catches it, not a boundary walking sideways. The apparent slope is an
amplitude contour.

**The vertical geometry never changed during the sweep** -- VSCALE 483, capture
48..576 half-lines, 1119 output lines, identical in all three readings -- because
the OSD scale pad moved the horizontal only. So the envelope's independence from
HSCALE is established; what sets it is not.

The decisive experiment is now cheap and needs no code: **change the VERTICAL
zoom only and watch the gap.**

  * If the gap MOVES up or down the frame, the envelope is set by the vertical
    geometry -- a frame buffer read pointer lapping the write pointer, or a
    vertical resampling phase. Both live in segment 4, the memory FIFOs, not in
    the input formatter's blanking sets.
  * If the gap DOES NOT move, the envelope is tied to the rasters or the frame
    rates rather than to any scaling, and the search moves to what is fixed
    about the frame.

## The amplitude is largest at the START of each line

`almost-1` is one press above `end-1` -- **one press is one HSCALE unit**, 652 to
653, so every boundary here is resolved to a single unit. The transition is
razor sharp: clearly glitching at 652, barely visible at 653, on a change in
produced width of 0.4 px.

What matters is HOW it degraded. It did not shrink uniformly: still glitching
but barely visible, a few random points on the very left edge. Against the
full-blown case covering the left two thirds, that puts the peak of
the displacement at the START of the line, decaying across it.

**That is a line-start underrun.** The read side begins emitting a line slightly
before enough data has been written, the first pixels come from wherever the
pointer actually is, and the error clears as the write pulls ahead. It accounts
for every observation in this document:

  * left-biased and decaying rightward -- the underrun resolving across the line
  * a fixed vertical envelope with zero crossings -- the phase governing how
    early the read starts, drifting down the frame and resetting each frame
  * static -- the rates are locked, so that phase pattern repeats identically
  * magenta where the source has none -- data read from the wrong addresses
    while the underrun lasts
  * razor sharp in HSCALE -- the read rate decides whether the read ever gets
    ahead of the write at all

The pointer idea survives the refutation above, relocated: not a race within the
line buffer's geometry, but a read that STARTS too early relative to the write.

**The experiment that tests it costs one pad press: pan the picture sideways.**
If the corruption stays glued to the PICTURE's left edge as it moves, it is
line-relative and this holds. If it stays at the SCREEN's left edge while the
picture slides past it, it is raster-relative and this is wrong.

## SETTLED: the pattern is static

Watched on the screen, 2026-08-09: **the pattern is static.**

That halves the hypothesis space, and it removes the more alarming half.

**The input and output clocks are NOT beating.** A free-running rate difference
would make the boundary crawl, frame after frame, and it does not move at all.
So FrameSync being broken on this unit -- `no INPUT vsync`, `runFrequency()`
returning early, the Si5351 never steered -- is a real defect and is **not this
defect**. Worth stating plainly because the two would have been easy to conflate
and the fix for one would have done nothing for the other.

What static leaves is much more specific. The pattern is regenerated
*identically* every frame, so whatever drifts:

- **accumulates per line**, which is what draws the diagonal, and
- **is reset every frame**, which is what makes it repeat identically.

That is a within-frame accumulator, and it is deterministic arithmetic rather
than an analogue or timing margin. Marginal signal integrity -- the reading
CLAUDE.md currently records for the banded thresholds -- does not survive this:
noise is not identical frame after frame.

Candidate registers that set a per-line phase, now recorded in every row:

  * `IF_INI_ST`  -- the only field the RD describes this way: "for the internal
    line_counter, the detail pixel's shift that the line_counter count compare
    to the horizontal sync"
  * `IF_LD_ST`   -- the line double's write reset generation start position
  * `MADPT_SEL_PHASE_INI` -- the motion-adaptive block's initial phase

## Readings

Eleven rows, 2026-08-09. Vertical geometry constant throughout: VSCALE 483,
capture 48..576 half-lines, output raster 1445 x 1126.

| label | verdict | HSCALE | capture | width | produced px | photo |
|---|---|---|---|---|---|---|
| `onset-1` | onset | 652 | 289..1078 | 789 | 1239.17 | hscale-onset-1.jpeg |
| `peak-1` | peak | 619 | 310..1056 | 746 | 1234.09 | hscale-peak-1.mov |
| `end-1` | end | 652 | 289..1078 | 789 | 1239.17 | hscale-end-1.jpeg |
| `almost-1` | almost | 653 | 288..1078 | 790 | 1238.84 | hscale-almost-1.jpeg |
| `clear-1` | almost | 665 | 281..1086 | 805 | 1239.58 | hscale-clear-1.jpeg |
| `onset-2` | onset | 686 | 267..1099 | 832 | 1241.94 | hscale-onset-2.jpeg |
| `almost-2` | almost | 690 | 264..1102 | 838 | 1243.64 | - |
| `onset-3` | onset | 691 | 264..1103 | 839 | 1243.32 | hscale-onset-3.jpeg |
| `almost-3` | almost | 692 | 263..1103 | 840 | 1243.01 | - |
| `onset-4` | onset | 693 | 263..1104 | 841 | 1242.69 | hscale-onset-4.jpeg |
| `onset-5` | onset | 694 | 262..1104 | 842 | 1242.37 | hscale-onset-5.jpeg |

`end-1` duplicates `onset-1` exactly -- the same boundary reached from the other
direction, which is a free repeatability result: no hysteresis, the fault is a
deterministic function of the state.

`clear-1` was called clear at the time and corrected to `almost` afterwards.
`peak-1`'s registers were snapshotted late and are provisional.

**The sampling is the dataset's main weakness.** The fault alternates on roughly
one HSCALE unit and most gaps between rows are five to thirty units, so it is
aliased by construction. Only 690..694 is contiguous. Every pattern found and
then refuted came from stitching across those gaps.

## The dense sweep: one key a step

`sweep_zoom.py` exists because of the paragraph above it. Recording a row by
hand is a press on the OSD pad and then a command line typed with the picture
still on screen, and at that cost a sweep gets sampled where somebody thought
something was happening -- which is precisely the sampling that aliased the
first dataset.

```sh
python3 tools/gbsc-pro-hwtest/sweep_zoom.py --host <ip>
```

It steps the framing one input unit, waits for the geometry to settle, records
the same thirty registers `characterise.py` records into the same index, and
takes one keypress: **ENTER for clear, `t` for torn.** An almost-clear counts as
clear, so there is no third judgement to make.
`b` steps back and *measures again*, `n` types a note, `q` restores the framing
the sweep started from.

**The swept variable is the framing, not `VDS_HSCALE`.** The engine takes a zoom
in input units and solves every window from it; HSCALE is an output, and it does
not move on every press -- `dense-01` widened the capture 842 -> 843 with HSCALE
unchanged at 694. Sweeping HSCALE directly is a different experiment, and one
the firmware undoes at the next solve.

Two things it reports that a hand-typed row could not:

- **A press the unit refused.** At the framing clamp the geometry stops moving
  and every further press produces an identical window. Recorded silently, that
  is a run of rows saying the fault is scale-independent.
- **Settled, not slept.** Two consecutive agreeing reads of the *solver's*
  registers. Segment 0's live measurements blip, so settling on the whole
  snapshot would wait the full timeout at every step and then call a clamped
  framing moved.

`--axis v` sweeps the vertical zoom instead, which is the other experiment
listed below.

## THE DENSE RUN: 493 framings, and what it settles

2026-08-09, late. `sweep_zoom.py`, one press a unit, nothing skipped. 342
readings widening from `zh` 165 out to the framing clamp, then 156 narrowing
from `zh` 173 to 311. Capture width is exactly `1009 - zh` throughout, so every
earlier reading converts and sits in the same axis.

### SETTLED: the fault is not a function of VDS_HSCALE

```
zh 220   HSCALE 652   capture width 789   TORN
zh 221   HSCALE 652   capture width 788   clean
```

One register value, both verdicts, adjacent presses. `VDS_HSCALE` is the only
quantity in the whole table that carries both, and it takes exactly one pair to
end it. Sweeping HSCALE by hand *does* tear and clear the picture, repeatedly
and not in dispute, so **both** the scale and the
capture flip the fault, and therefore **neither of them is the variable.**

This is what the aliased dataset could never have shown. The pair is one unit
apart; every earlier gap was five to thirty.

### There is a THIRD input, and it is on the output side

Changing `VDS_HS_ST` and `VDS_HS_SP` affects it too, 2026-08-09.
The output hsync pulse position and width flip the fault as
well — and CLAUDE.md already carries half of this from a different symptom:
left-hand corruption that survived everything else cleared by moving the pulse
from 10 to 62..77.

So three quantities flip it — the capture width, `VDS_HSCALE`, and the output
hsync pulse — and none of them determines it. **That is the shape of a phase
relationship, not of a threshold.** All three change the timing between a read
pointer and a write pointer; none of them *is* that timing, which is why every
attempt to find the fault as a function of one of them has failed, and why no
modulus of any of them partitions the data.

`VDS_HS_ST` / `VDS_HS_SP` are now in `characterise.py`'s FIELDS. They were not
for the first 493 rows, which is the same aliasing as the first dataset on a
different axis.

**But the output hsync does NOT clear it at `zh` 250**, tested the same
evening: with the picture parked 28 units inside the 66-unit torn band, no
value of the pulse helped. That is the identical shape to the HSCALE result of
earlier the same day, where `VDS_HSCALE` was swept by hand across the corrupted
state and no value cleared it — and it means the same thing here:
**a quantity that flips the fault in some states cannot necessarily clear it in
any given one.** Deep inside a band, neither output-side knob reaches it; the
capture width still does, since `zh` 221 and `zh` 288 are both clean.

So "three quantities flip it" is about the *relationship*, not about three
available fixes. Do not read either negative as "that register is irrelevant",
and do not re-run either sweep expecting a clean setting to fall out.

### Tried at `zh` 250, frozen, and did NOT clear it

The state: capture 304..1063 (width 759), `VDS_HSCALE` 629, 28 units inside the
66-unit torn band — the deepest torn territory measured, and deliberately so.
A register that cannot reach the fault *here* may still reach it at a band
edge, so these are negatives about this state, not about the register.

| register | swept to | effect |
|---|---|---|
| `IF_LD_RAM_BYPS` | 1 | destroys the picture entirely — see below |
| `VDS_HS_ST` / `VDS_HS_SP` | by hand | none |
| `SP_H_CST_SP` | by hand | none |
| `MADPT_MI_1BIT_BYPS` | s2, motion index | none |
| `MADPT_MI_1BIT_FRAME2_EN` | s2, motion index | none |
| `MADPT_HTAP_BYPS` | s2, motion index H filter | none |
| `MAPDT_VT_SEL_PRGV` | s2, motion-adaptive VT filter | none |
| `MADPT_BIT_STILL_EN` | s2, pixel-base still detect | none — **predicted** |
| `MADPT_Y_WOUT_BYPS` | s2, Y noise-reduction | none — **predicted** |
| **`MADPT_Y_DELAY`** | **s2, Y pipeline delay** | **moves the glitches** |
| **`MADPT_Y_DELAY_UV_DELAY`** | **s2, both delay nibbles** | **moves them right** |
| **`MADPT_UV_DELAY`** | **s2, UV pipeline delay** | **mild, on the reds only** |

### SEGMENT 2 IS EXHAUSTED, and here is the rule that says so

Seven negatives and three positives, and they are all explained by one thing —
**a segment-2 field is live only if its sub-block's gate is open, and the delay
pipes are the only things in there with no gate at all:**

| sub-block | its gate | state |
|---|---|---|
| motion-adaptive | `MAPDT_VT_SEL_PRGV` 1 = *select original data* | computes, result **discarded** |
| diagonal bob | `DEINT_00` 0xFF | all bypassed |
| noise reduction | `MADPT_Y_NRD_ENABLE` 0, `MADPT_Y_WOUT_BYPS` 1 | off |
| V-IIR, vertical scale | `MADPT_VIIR_BYPS` 1, `Y_VSCALE_BYPS` 1, `UV_VSCALE_BYPS` 1 | bypassed |
| scaling-down line buffer | `MADPT_PD_RAM_BYPS` 1 | bypassed |
| **Y/UV delay pipes** | **none — unconditional** | **in the path** |
| UV delay line buffer | `MADPT_UVDLY_PD_BYPS` **0** | in the path |

This is better than the "motion-adaptive is dead" it replaces, because it
explains the awkward cases: `MADPT_Y_MI_DET_BYPS`, `MADPT_MI_1BIT_BYPS`,
`MADPT_HTAP_BYPS` and `MADPT_VTAP2_BYPS` all have their own bypass bits
**clear**, so the motion block is computing perfectly well. Its answer is
simply thrown away by `MAPDT_VT_SEL_PRGV`.

**Exactly three untried segment-2 fields could still do anything:**
`MADPT_UVDLY_PD_SP`, `MADPT_UVDLY_PD_ST` (both 4) and `MADPT_MI_DELAY`. The
first two are in the **UV** path, so tonight's luma result rules them out as
the cause; the third is a delay with a motion name and the rule says it does
nothing, which makes it the cheap test that would break the rule.

So stop sweeping segment 2. **The remaining search space is segment 1** — the
input formatter, where the live line-double FIFO is — **and segment 4**, the
main memory write and read FIFOs.

`SP_H_CST_SP` is a **sync processor** position. The sync processor measures the
incoming line; it does not move data through the scaling pipeline, so it has no
way to reach a read/write phase. CLAUDE.md records it at 1667, one of the three
values that proved the sync processor counts in ADC samples rather than IF
units.

### RETRACTED, within the hour: "segment 2 is inert"

The section below was written after four segment-2 registers did nothing, and
it concluded that the remaining fifty could be skipped. **`MADPT_Y_DELAY` then
moved the glitches.** The generalisation was wrong and the reasoning behind it
is worth keeping only as a warning: four negatives in a segment do not make the
segment dead, because a segment is not a functional unit. Segment 2 holds the
motion-adaptive deinterlacer *and* the Y/UV delay lines, and only the first of
those is switched off.

Read the section below as "the motion-adaptive block is off", which it is and
which is measured. Do not read it as "segment 2 does nothing".

### POSITIVE: `MADPT_Y_DELAY` moves the glitches

The first register anyone has found that reaches the fault without destroying
the picture. RD-5725, S2_17:

```
MADPT_Y_DELAY   bits 3-0   Y  data delay pipes, value + 1, range 1..16
MADPT_UV_DELAY  bits 7-4   UV data delay pipes, value + 1, range 1..16
```

Both currently **0**, i.e. one pipe each. These are plain pipeline delays, not
motion-adaptive functions, which is why they are live while the rest of the
block is not — and the firmware writes this byte in the *progressive* branch
(`MADPT_Y_DELAY_UV_DELAY = 1`, `gbs-control.ino:3878`), so it is in the path
for this source by design.

**This is the mechanism, not a symptom of it.** A delay line is a horizontal
shift; if shifting Y moves the glitches, the glitches are tied to where the Y
data sits in the pipeline.

**And it may explain the magenta.** §4.1 records *"magenta appears where the
source has none"* — magenta is red plus blue without green, which is what a
chroma/luma misalignment looks like. `Y_DELAY` and `UV_DELAY` are literally the
two halves of that alignment, they share one byte, and both are at 1.

`MADPT_Y_DELAY_UV_DELAY` moves it **to the right** as well, which is consistent
rather than additional: that name is the whole-byte convenience field, so it
writes both nibbles at once. It does not separate Y from UV.

Open, and cheap:

- **Does any `MADPT_Y_DELAY` value CLEAR it, or only move it?** Sixteen values,
  one register. Moving-but-never-clearing and clearing-at-one-value are
  different faults with different fixes, and this is the question that matters
  most.
- **`MADPT_UV_DELAY` ALONE: answered.** *"A mild effect on the reds."* Chroma
  shifts, the glitches do not. So **the corruption travels with the LUMA
  data**, and the magenta is a consequence rather than the fault — chroma
  arriving correctly against luma that did not. That is the discriminating
  result, and it points at the Y path.

- **THE CONFOUND, and it decides whether any of this is real.** A Y delay
  shifts the whole luma image right; if the glitches move right with it, that
  is the register doing its ordinary job and says nothing about the fault. The
  question is whether the glitch moves **relative to the picture content**.
  Pick a sharp vertical edge, note where it sits, step the delay, and see
  whether the edge and the glitch move by the same amount. Together means no
  information. Differently means the glitch has a fixed position in the
  *pipeline* rather than in the *image*, which localises it to a stage — and
  that would be the most useful thing found all evening.

  This was not checked before the delay was swept. It should have been.
- **Does the glitch move one pixel per pipe?** That calibrates the units and
  says whether the offset is a whole number of pipes.
- **Does the amount it moves depend on the framing?** If one pipe shifts the
  glitch further at `zh` 250 than at a band edge, the delay is being scaled,
  which ties it to the magnification that the sweep says matters.

### MEASURED: the motion-adaptive block is off on this unit

The other three are all in segment 2, and none of them changed the picture —
including `MAPDT_VT_SEL_PRGV`, which reads like a progressive/interlace switch
and is the one that reads as though it should do something.

They did nothing because **the block they feed is off**: `DEINT_00` 0xFF (every
diagonal-bob bypass bit set), `MAPDT_VT_SEL_PRGV` 1, `WFF_ENABLE`/`RFF_ENABLE`
0 — character for character what `disableMotionAdaptDeinterlace()` writes.

That was an inference from register values an hour earlier. It is now a
measurement: **four independent segment-2 registers, no effect on the picture.**

**So skip the rest of segment 2.** Every `MADPT_*` and `DIAG_BOB_*` field is
downstream of the same disabled block — around fifty registers that need not be
swept one at a time. Switching the block back on is a different experiment, and
a much larger one.

The parts of the path where a change can still reach the picture:

- **segment 1**, the input formatter, including the line-double FIFO that *is*
  running
- **segment 4**, the main memory write and read FIFOs
- **segment 3**, the VDS — but downstream of the fetch, and the corruption is
  *fetched* wrong (magenta the source does not contain), not displayed wrong

### So the deinterlacer hypothesis is dead

It was a good hypothesis and it is worth stating plainly that it is finished,
because "maybe it's the deinterlacer" is the kind of idea that returns. Four
segment-2 registers do nothing, the block's own enables are all off, and the
only stage in that family still running is the input formatter's line-double
FIFO — which is in segment 1, and which destroys the picture when removed.

### SETTLED: no recorded quantity separates torn from clean

80 torn, 413 clean, 493 distinct framings. Every quantity in the row overlaps:

| quantity | torn | clean |
|---|---|---|
| produced px | 1231.18..1244.37 | 1228.10..1264.25 |
| capture width | 722..854 | 698..1185 |
| HSCALE | 600..703 | 582..960 |
| memory window | 1433..1435 | 1433..1435 |
| display window | 1229..1242 | 1226..1262 |
| frac(produced) | 0.02..0.99 | 0.00..1.00 |

### SETTLED: the modular family is dead

Every modulus from 2 to 250, against capture width, HSCALE, capture start and
produced: **not one of the 996 partitions is consistent.** No residue class is
reliably torn or reliably clean.

That closes out an entire line of attack. "Periodic in HSCALE at ~34 units",
"distance from a whole pixel", "capture width parity" and "alternates on one
HSCALE unit" were four separate attempts at the same shape, each fitted to
eight or fewer rows. The shape does not exist.

### The band structure, measured

```
   TORN   zh 155..155    1 wide
   clean  zh 156..158    3
   TORN   zh 159..164    6      (165 not measured)
   TORN   zh 166..166    1      (167..172 measured only in the earlier session,
   TORN   zh 173..177    5       where 167/168/170 were onset and 169/171 almost
   clean  zh 178..219   42       -- so 159..177 is very likely one band of 19)
   TORN   zh 220..220    1
   clean  zh 221..221    1
   TORN   zh 222..287   66
   clean  zh 288..311   24
   clean  zh -188..154  343      the whole wide side, unbroken
```

Irregular. Bands of 1, 5, 6, 66 torn against 1, 3, 24, 42, 343 clean, in no
progression. And **the wide side is clean for 343 consecutive units** — the
fault has a hard upper edge at width 854/855 and nothing above it.

### The produced sawtooth

One press narrower normally *raises* produced by ~0.32 px, because HSCALE drops
a unit and more than compensates the unit lost from the capture. Every seventh
press or so HSCALE fails to tick and produced drops 1.571 px instead — one
input unit at this magnification. `zh` 220 -> 221 is one of those, which is why
that pair exists at all. Any future model has to live with the fact that the
swept quantity does not move monotonically.

## The deinterlacer is already off, and that is measured

Dumped 2026-08-09 while the fault was live, in case the tearing was a
misconfigured deinterlacer. It is not, because there is nothing running:

| | value | |
|---|---|---|
| `DEINT_00` (s2 0x00) | `0xFF` | every diagonal-bob bypass bit set |
| `MAPDT_VT_SEL_PRGV` | 1 | VT filter takes the original data |
| `WFF_ENABLE` / `RFF_ENABLE` | 0 / 0 | the motion path's FIFOs are not running |
| `SFTRST_DEINT_RSTZ` | 1 | out of reset, but bypassed |

That is character-for-character what `disableMotionAdaptDeinterlace()` writes.
Two fields dissent — `MADPT_Y_MI_DET_BYPS` 0 and `MADPT_Y_MI_OFFSET` 0 are the
*enable* values — which is a `preset-load-clobber.md` signature, but they feed
only the path that is off.

**What IS in the path is the input formatter's line-double FIFO, configured for
an interlaced source:**

```
IF_LD_RAM_BYPS   0   "0: select interlace line double data from FIFO"
IF_PRGRSV_CNTRL  0   "0: source is interlaced"
IF_LD_SEL_PROV   0   "0: select read reset timing of interlace data"
IF_LD_ST         5   line double WRITE reset generation start position
```

against `STATUS_IF_INP_INT` = 0, progressive. Those three have read 0/0/0 in
**every one of the 519 rows**, torn and clean alike. The firmware only writes
the honest values (`gbs-control.ino:3800-3801`) for `videoStandardInput`
3/4/8/9 and only when `!isCustomPreset`; Mode Detect currently reports nothing
at all (`STATUS_00` = 0x02 fails the `& 0x07 == 0x07` gate), so they were left
at the preset table's.

### Bypassing the line doubler destroys the picture

Tested frozen at `zh` 250, deep inside the 66-unit torn band:
`IF_LD_RAM_BYPS` 0 -> 1. The picture collapsed into horizontal bands of
magenta, white and black — the memory read completely out of step.
`snapshots/if-ld-ram-byps-1-destroys-picture-2026-08-09.jpeg`.

**That proves the FIFO is live and load-bearing, not that it is
misconfigured.** Deleting a stage the pipeline depends on breaks it however
well it was set up, so this was the wrong shape of experiment.

The test that probes the mechanism keeps the FIFO in the path and changes only
*when it resets*:

```sh
python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set IF_LD_SEL_PROV=1
```

**Not yet run.** If the tearing moves, the read-vs-write reset offset is the
mechanism; if the picture is unchanged, the hypothesis is dead and should be
recorded as dead. `IF_LD_ST` crept one unit off 5 is the same quantity from the
write side.

## Still to do

- **`IF_LD_SEL_PROV` = 1**, above. One field, frozen, reversible.
- **The vertical axis.** Every one of the 493 readings changed the horizontal
  zoom only. `sweep_zoom.py --axis v` is one flag and the null may move.
- **Pan the picture sideways.** Still not done, still one press: if the
  corruption tracks the picture's left edge it is line-relative, if it stays at
  the screen's left it is raster-relative.
- The 167..172 gap, to confirm 159..177 is one band rather than three.
- Raise the `picture_wider_than_display` threshold above the engine's standing
  2 px so the warning means something. Every row tonight flagged YES on a 2 px
  overrun, so the warning currently means nothing.
