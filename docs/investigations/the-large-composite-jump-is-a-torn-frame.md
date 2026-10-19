# The large composite jump is a torn frame, and the separator triggers it

`vga` on composite sync at 320x256@50 into 1080p emits, a few times every ten
minutes, a frame `picture_stability.py` reports as the picture displaced by 100
to 250 rows. **The picture is not displaced. The frame is TORN**, and the
instrument reports a correlation lag because a lag is all it can report.

What causes the tear is the sync processor's line count restarting mid-frame,
which is the separator. The frame time lock is flat through every one.

## What the frame contains

Read down one, from `tools/gbsc-pro-hwtest/jump_frames.py`, which waits for a
jump and writes out the frame it lands on together with the frame before it:
the tail of one capture, a band of black, then the head of the next capture,
with the picture leaning progressively sideways through both halves. The frame
immediately before is a whole, square, flush card.

| frame | longest black run |
|---|---|
| the frame before, every event | 3 rows, which is the top margin |
| a torn frame | **234 rows**, at rows 294..528 |
| a second | **233 rows**, at rows 641..874 |
| a third | 120 rows, at rows 0..120, the band running off the top |

The bench mode sends 312 lines of which 256 are active, so its vertical
blanking is 56 lines, and at the 4.19 output rows a source line this framing
scales to, 56 lines is **234.6 output rows**. The band is the interval.

**That identifies the mechanism rather than describing it.** The playback read
is on the far side of the capture write, so it crosses the part of the input
frame during which the write produces nothing, and the part emits black for
exactly as long as the write idles. A band of any other height refutes it.

**It also rules out the capture dongle**, which is a sink with a latch of its
own. A dongle straddling two HDMI frames delivers the tail of one and the head
of the next with NOTHING between them, blanking never reaching it. The band is
in the emitted frame.

**The offset WALKS through an event.** One carries -132, -130, -128 and -126
over successive frames, which is the read sliding past the write once the two
have been displaced, rather than a picture placed somewhere else and put back.

## The count collapses at the tear, and nowhere else

`Tv5725::SamplingLog` at 25 ms held against the capture on one clock, 600 s,
22618 samples:

| `STATUS_SYNC_PROC_VTOTAL` | samples |
|---|---|
| 308 | 21865 |
| 309 | 741 |
| 310 | 11 |
| **181, 106, 181, 115** | **one each** |

**All four readings below 300 fall within 0.25 s of a tear, and none falls
anywhere else.** The seven tears cover 3.5 s of the 600, so a collapse that was
ambient would land in those windows 0.58% of the time and this one lands there
four times out of four.

**That is what rules out the innocent reading** that a sample landed mid-count.
It is ruled out twice over: collapsed readings are not ambient, and at 3.3 ms
the collapsed value holds across three to six consecutive samples before
returning to 308, which a counter being read mid-sweep cannot do.

`VPERIOD_IF` reads 623 in every sample of the run, the collapsed ones included.
The field period does not move; only the count restarts. So the separator
asserts vertical sync part way down the frame, the capture write restarts on
it, and the read is left on the wrong side of the write.

## The frame time lock is not involved

Across every tear in two runs of 600 s the lock reports `in 50080 mHz,
out 50080 -> 50080 mHz` with the display clock between 107944888 and 107944920
and a phase error near 12270 -- steady to eight parts in 10^8 either side of a
tear, with no correction and no excursion.

So the rates are not drifting apart. The crossing is where a separator event
becomes visible, not a thing the lock allowed to happen.
`the-frame-time-lock-saturates.md` has the excursion that does exist and is a
different fault.

## The post coast is not the cause

`SP_POST_COAST` 2 against 4, unfrozen through `/coast` so the pair survives the
re-solve the route asks for, 300 s a leg, alternating:

| post coast | displaced of ~18003 | jumps/s | worst |
|---|---|---|---|
| 2 | 17, 12, 17 | 0.02, 0.01, 0.02 | 84, -134, 100 |
| 4 | 33, 9 | 0.06, 0.01 | -216, -216 |

The spread within one value covers the difference between the two, so the
length of the hold separates nothing. The hold is settled on the roll it does
move, in
`the-composite-roll-is-lost-horizontal-pulses-in-the-doubled-scan.md`.

## The collapse is a latched short count, and a 25 ms sample misses two thirds

`Tv5725::SamplingLog` emits a row only where a watched reading moved since the
last one (`/samplinglog?changes=1`), which takes a 900 s run from 270000 lines
to 10000 and lets the sample interval fall to what the loop achieves rather
than what the console will carry: **301 samples a second, one every 3.3 ms,**
against 138 delivered at a row a sample. `tools/gbsc-pro-hwtest/sync_dropouts.py`
is the reader, and the device reports its own sample count on the `done` line,
so a dropped row cannot be mistaken for a sample never taken.

Read at that interval the collapsed value is **held across three to six
consecutive samples, 10 to 20 ms, then returns to 308** -- one field. So the
register is reporting a frame that ended early rather than a counter sampled
mid-count, and one collapsed reading is a measurement.

**The rate is one collapse every 90 s**, 10 events in 900 s and 6 in 600 s. The
25 ms runs read 4 across 1200 s because they were missing most of them:
decimating one 301/s run in software, its six collapses read as six at 22 ms,
**two at 43 ms and one at 58 ms**.

| interval | of the 6 events |
|---|---|
| 14.5 ms | 6 |
| 21.7 ms | 6 |
| 29.0 ms | 5 |
| 43.5 ms | 2 |
| 57.9 ms | 1 |

**So the faster sample does not make the fault countable.** One collapse every
90 s is the same order as the 3 and 7 tears a 600 s run carries, which is what
a tear per collapse predicts. A leg scored on collapses still costs 30 minutes
for one point.

## Where the count restarts is two places, not scattered

Pooled over 20 events on this one state, the value the count restarts at:

| 106 | 115 | 120 | 180 | 181 |
|---|---|---|---|---|
| 2 | 2 | 6 | 4 | 6 |

**Two clusters, 106..120 and 180..181**, and nothing between or outside them.
The earlier reading of four values as scattered through the frame was four
events.

## A pulse dropout leads the collapse, and never follows it

`STATUS_SYNC_PROC_HLOW_LEN` reads 153 of a 2200-sample line on this source,
which is its 6.95% sync width. In one sample either side of a collapse it reads
**near zero** -- the separator emitting no pulse at all rather than a narrow
one. Over 900 s, 10 collapses and 28 readings under 30:

| band of `HLOW_LEN` | events | leading a collapse by <= 20 ms | following | ambient |
|---|---|---|---|---|
| 0..10 | 10 | **5 of 10** | 0 | 0.02% |
| 0..30 | 28 | **6 of 10** | 0 | 0.06% |
| 30..60 | 57 | 0 | 0 | 0.13% |
| 60..100 | 76 | 2 | 0 | 0.17% |
| 100..150 | 275 | 0 | 0 | 0.61% |
| 2660..2713 | 670 | 0 | 0 | 1.49% |

**The asymmetry is the argument.** A dropout covers 0.06% of the run and leads
six collapses of ten; none follows one, so it is not a consequence of the tear.
The lead times are **9, 13, 13, 16, 17 and 17 ms** against a 19.97 ms field, so
the dropout falls inside the field before the count restarts.

**It is the pulse GOING, not narrowing.** The band 30..60 holds 57 events and
leads nothing, and 100..150 holds 275 and leads nothing -- which also rules out
`SP_H_PULSE_IGNOR`, 61 on this state, as the boundary being crossed: the
readings that lead a collapse are a tenth of it.

The broad readings are a separate population -- 2660, 2661 and 2713 against a
2200-sample line, 670 of them -- and they lead nothing either.

## The chip's own interrupt latches do not see it

`STATUS_0F` reads 0 in all 270837 samples of the 900 s run, collapses and
dropouts included. Every source is unmasked (`INT_ENABLE0..7` all 1), and
nothing in the firmware acknowledges bits 4 to 7 on a settled source -- so
`STATUS_INT_INP_HSYNC`, `_VSYNC` and `_CSYNC` would hold a 1 once set and never
do. The dropout is below the interrupt block's own notion of a change, so there
is no lossless hardware count to score a walk on.

## What is open

**What drops the pulse.** The separator emitting nothing for a line is the
measured precursor; what makes it do so is not established. The slicing level
is the untried candidate -- `ADC_SOGCTRL` holds 12, and a slicer cutting near
the signal's edge drops pulses and inserts them, which is what a reading of
zero beside a steady 153 looks like. `SP_H_PULSE_IGNOR` is ruled out above.

**Why a dropped pulse restarts the VERTICAL count.** `VPERIOD_IF` reads 623 in
every sample of every run, so the input formatter's vertical measurement is
untouched while the sync processor's count restarts: the two vertical
extractors disagree across the event. Nothing else in the row moves --
`STATUS_SYNC_PROC_HTOTAL` 2200, `HLOW_LEN` back at 153, `HSACT` 1, the IF
status bits 67 -- so the line and the field period are both intact at the
moment the count restarts.

**And four collapses in ten carry no caught dropout**, while five dropouts in
ten carry no collapse. A dropout is held for one sample only, so neither is a
measurement of necessity or of sufficiency -- both sides are partially
detected, and closing that needs a count the sampling cannot miss.

`SP_DLT_REG` and the vertical-detection thresholds (`SP_SDCS_VSST_REG_L`,
`SP_VSYNC_TGL_THD`, `SP_V_PRD_EQ_THD`, `SP_VT_DLT_REG`) are still unwalked
against this fault, and at one collapse every 90 s a leg scored on collapses
buys one point per half hour.
