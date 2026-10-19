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

## Where the count restarts is two bands

Pooled over 32 events on this source, the value the count restarts at:

| 106 | 107 | 115 | 120 | 132 | 170 | 180 | 181 | 270 |
|---|---|---|---|---|---|---|---|---|
| 4 | 1 | 3 | 7 | 1 | 2 | 5 | 8 | 1 |

**Two bands, 106..121 and 170..181**, with single events at 132 and 270
outside them. Read as four events it looks scattered and read as twenty it
looks like two tight clusters; neither survives. What the bands are is open,
and nothing about a window edge is implied by either.

**A THIRD BAND EXISTS AND BELONGS TO A DIFFERENT FAULT.** A restart at 249..299
is the vertical detected inside the source's 53 blanking lines, which begin at
line 256, and it is 1 event of 43 on the engine's own configuration against 17
of 23 on the chip's default edge reference. **So a count of readings under 300
pools two faults**, and a rate compared between two states has to be split at
240 first.

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

**The lead is 14 ms, pooled over 25 events from eight captures** -- 23 of them
between 9 and 19 ms with a standard deviation of 3.45, and two at 34 -- against
a 19.97 ms field. So the dropout falls inside the field before the count
restarts, and does so at a tighter spacing than a field.

## Refuted: the dropout falls in the vertical interval

The restart value says how far into the field the count restarted, `restart /
308` of 19.97 ms, so subtracting it from the lead gives where the dropout fell
relative to that field's real vertical. That phase concentrates -- 16 of 25
between 1 and 6 ms before the vertical, where a uniform dropout would put 6.3
there -- and the two restart bands then read as a dropout in or just before the
53 blanking lines.

**It does not survive the comparison that matters.** Over the 23 events under
25 ms the phase has a standard deviation of **3.17 ms against the lead's
3.45**, so it is no tighter an account than the lead alone, and the
concentration is the lead's own spread arriving through the arithmetic. The
lead grows with the restart position by **+1.8 ms where a fixed phase predicts
+3.8**, which is half. So the data cannot say whether the dropout is anchored to
the field's vertical or to the restart, and **a mechanism resting on the dropout
falling in the vertical interval has no support**, the two restart bands
notwithstanding.

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

## The edge reference adds a SECOND fault and does not touch this one

Reverting the pair that closed the roll -- `SP_CS_P_SWAP` and
`SP_HS2PLL_INV_REG`, which reference the LEADING edge of a negative-going
composite sync -- to the chip's default multiplies the count collapse about
tenfold. **It is the wrong tenfold.** Split by where the count restarts:

| state | restart < 240 | restart >= 240 |
|---|---|---|
| the engine's own pair, 6538 s | 42, **5.8 per 900 s** | 1, 0.1 per 900 s |
| the chip's default, 958 s | 6, **5.6 per 900 s** | 17, **16.0 per 900 s** |

**The population this investigation is about is unchanged**, 5.8 against 5.6
per 900 s across 42 events and 6, while a population that barely exists on the
leading edge rises by a factor of 160. So the edge reference creates a
different event rather than amplifying this one, and the eleven-fold figure a
pooled count gives is the sum of two faults.

**THE TWO POPULATIONS ARE THE BANDS, AND THEY HAVE DIFFERENT PRECURSORS.** The
source's 53 blanking lines begin at line 256, so a restart at 249..299 is the
vertical detected inside the interval -- a few lines early -- where the bands
at 106..121 and 170..181 are a vertical arriving 130 to 200 lines early, mid
picture. The dropout separates them too: pooled over both states, **8 of 16
events below 240 are led by a pulse dropout and 1 of 18 at or above it**.

So the leading edge earns its place twice over, and the trailing edge is not an
assay for this fault. **What it is an assay for is the vertical-interval
event**, which on the leading edge is too rare to study and is not what tears
the picture here.

**A RATE IS PER POPULATION, NOT PER EVENT CLASS, AND THAT IS HOW THIS WAS
MIS-READ.** `collapse_legs.py` counts every reading under 300 as one fault,
which is the right default for finding a lever and the wrong one for believing
it: the restart value was printed beside the count throughout and the pooled
figure was quoted anyway. **Split by restart band before comparing two states.**

**AND A POKE THAT IS NOT READ BACK IS NOT A STATE.** Two legs of the first
repeat were silently controls: `/coast?clear` is queued for `loop()` and the
re-solve behind it runs `applyForSyncType()`, which rewrites the edge reference
and the pulse ignore, so a poke written before that landed was undone. The legs
read 1 and 0 collapses and said nothing. `collapse_legs.py` waits the coast
request out, then pokes, then verifies, and prints every watched field for
every leg.

## The slicing level is NOT the lever, and a block of legs is why it looked like one

`ADC_SOGCTRL` across nine legs on one source, every leg unfrozen with the level
read back and the source `acquired` with `cv` 624 throughout:

| level | how it was set | leg | collapses | per 900 s |
|---|---|---|---|---|
| 14 | poked | 900 s | 8 | 8.0 |
| 12, the engine's own | in force | 900 s | 10 | 10.0 |
| 12 | in force | 600 s | 6 | 9.0 |
| 12 | in force | 900 s | 3 | 3.0 |
| 10 | poked | 180 s | 0 | 0 |
| 8 | poked | 1080 s | 1 | 0.8 |
| 6 | poked | 180 s | 0 | 0 |
| 9 | in force | 900 s | 9 | 9.0 |
| 8 | poked | 900 s | 6 | 6.0 |

**The count runs 0 to 10 per 900 s with no relation to the level.** The same
value 8, set the same way on the same source, reads 1 in one 900 s leg and 6 in
another.

**WHAT MADE IT LOOK LIKE A LEVER WAS A BLOCK OF CONSECUTIVE LEGS.** The four
quiet legs at 6, 8 and 10 ran one after another and were scored against
controls taken before and after the block rather than between, which read as an
elevenfold reduction. Interleaving kills it: level 9 in force reads 9, and a
poked 8 immediately after reads 6.

**So one leg is not a measurement at this rate, and 900 s is not enough.** The
within-state spread covers the whole range any candidate could move, which is
the measured form of a 300 s leg reading 0, 8, 17 and 26 on one state. **A
pooled comparison of non-interleaved legs is worse than no comparison**,
because it produces a confident wrong answer.

**It also kills the one hypothesis that survived the level.** A poked level
leaves the engine's held level alone, so `putInForce()` never relatches the
sampling phases or the ADC PLL -- which would have separated the quiet poked
legs from the noisy in-force ones. The interleaved poked leg reads 6, so it
does not.

## Why the engine chooses the level it does

`SyncOnGreen::acquire()` walks the chosen level DOWN from the level the input
needs -- `DefaultLevel` 13, `ComponentLevel` 14 -- and stops at the first one
where the sync processor holds clean edges over a run, so **it stops at the
marginal level by construction** and the run it scores is milliseconds long.
That remains true of the walk and is no longer a candidate explanation for this
fault.

A margin of four steps below the first clean level was built and reverted. It
lands the bench source on 9 instead of 12, the Wii on `ypbpr` holds acquired at
every level down to 6 with `STATUS_SYNC_PROC_HTOTAL` matching `PLLAD_MD`, and
the roll reads 0 of 1202 frames displaced at both levels -- but the collapse
reads 9 per 900 s at 9, so the change bought nothing while altering acquisition
for every source.

## What is open

**WITHIN A SESSION THE RATE IS STEADY; BETWEEN SESSIONS IT MOVES.** Binned at
90 s over the six long captures of one session the collapse is
**under-dispersed -- 60 bins, mean 0.65, variance 0.46, index of dispersion
0.71** -- and no bin holds more than 2, so the events are if anything more
regular than Poisson and the 1-to-10 per 900 s range is the square root of a
rate near 6. Nor does the rate ramp after a re-solve: across 31 events the
quarters of a 120 s leg hold 11, 7, 7 and 6 against a flat 7.8, so a short leg
is not biased by the one that set it up.

**But the next session's controls read 1 event below 240 in 959 s where that
rate predicts 6.2**, which Poisson puts at p 0.014. So the rate between
sessions moves by about threefold and the within-session regularity licenses
nothing across one.

What follows is the leg arithmetic, and it holds only inside a session:

| effect sought | legs of 900 s per arm |
|---|---|
| threefold | one pair |
| twofold | three pairs |
| a state ten times worse | a single 120 s leg |

**So screen for a large effect in short legs, do not measure a small one, and
interleave** -- a rate read in one session against a control read in another
compares two sessions.

**A STATE WHERE THIS FAULT IS FREQUENT IS STILL NOT FOUND.** The trailing edge
looked like one and is not: split by restart band it leaves the sub-240
population at 5.6 per 900 s against 5.8, and all of its tenfold is a
vertical-interval event this investigation is not about. Starving the separator
is not it and the slicing level is not it either.

**What drops the pulse.** `STATUS_SYNC_PROC_HLOW_LEN` reading near zero leads
the collapse by a median 14 ms and never follows it, which is the one link
established. Dropouts run 19.5 to 67 per 900 s across the eight captures, so
the level does not move them either.

**THE DROPOUT IS THE CHEAPER INSTRUMENT AND IT IS STILL NOT A CHEAP ONE.** It
is five times more frequent than the collapse, which is what makes a short leg
worth taking at all -- but its own within-state spread is **28 against 63 per
900 s at one level**, two and a half times, where Poisson on either figure is
under eight. So it resolves a tenfold change and not a twofold one, and the
interleaving rule binds on it exactly as it does on the collapse.

**Why a dropped pulse restarts the VERTICAL count.** `VPERIOD_IF` reads 623 in
every sample of every run, so the input formatter's vertical measurement is
untouched while the sync processor's count restarts: the two vertical
extractors disagree across the event. Nothing else in the row moves --
`STATUS_SYNC_PROC_HTOTAL` 2200, `HLOW_LEN` back at 153, `HSACT` 1, the IF
status bits 67.

**The vertical-detection registers do not gate the count, and are retired.**
RD-5725-1.1 labels `SP_VSYNC_TGL_THD` (s5_2d), `SP_SYNC_WIDTH_DTHD` (s5_2e),
`SP_V_PRD_EQ_THD` (s5_2f), `SP_VT_DLT_REG` (s5_31), `SP_VSIN_INV_REG` (s5_32)
and `SP_V_TIMER_VAL` (s5_34) all **V active detect control**, mirroring an H
family -- `SP_SYNC_TGL_THD`, `SP_L_DLT_REG`, `SP_T_DLT_REG`, `SP_SYNC_PD_THD`,
`SP_PRD_EQ_THD`, `SP_H_TIMER_VAL` -- labelled H active detect control. What the
two families decide is `SYNC_PROC_STATUS_[1]` and `[3]`, the HS and VS active
bits; the V total the capture follows is a different field,
`SYNC_PROC_STATUS_[50:40]`, and nothing in the datasheet connects the family to
it.

**The bench settles which of the two readings holds.** On the csync path
`STATUS_SYNC_PROC_VSACT` reads 0 while `STATUS_SYNC_PROC_VTOTAL` reads 308 and
the picture is correct, so the block never declares V active on this path and
the counter follows the source regardless. `SP_VT_DLT_REG` 47 against a
vertical arriving 189 lines early is therefore no contradiction: the register
gates a declaration this path does not make.

**Neither link is measured as necessary or sufficient.** Roughly half the
collapses carry no caught dropout and half the dropouts carry no collapse, and
a dropout is held for one sample only, so both sides are partially detected. No
latched interrupt supplies a count the sampling cannot miss.

**What the edge reference leaves open is why either event survives it.** The
pair is already in force, so the lever is spent as a fix; what it buys is the
assay. The residual's restarts sit at 106..121 and 170..181 where the loud
state's sit in the vertical interval, so whether the two are one mechanism at
two depths or two mechanisms is not settled, and a candidate screened loud
needs confirming quiet.
