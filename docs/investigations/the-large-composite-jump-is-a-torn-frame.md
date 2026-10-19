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

**That is what rules out the innocent reading.** `SP_VTOTAL` is a live counter,
so a sample taken mid-count returns any value between zero and the total, and a
collapsed reading beside a tear is worth nothing until collapsed readings are
shown not to be ambient. They are not.

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

## What is open

**Why the separator asserts vertical mid-frame.** The four collapses land at
181, 106, 181 and 115 of 308, which is scattered through the frame rather than
at one place, so nothing about a window edge is implied. `SP_DLT_REG` is the
threshold that decides how much broader than a line pulse something must be
before it reads as vertical, and it holds 192: a serrated source is miscounted
below 0x70 and the separator stops extracting vertical at all by 2048, so the
range between is unwalked against this fault.

**The rate is what makes the walk expensive.** Two 600 s runs carry 3 and 7
tears, and 300 s legs on one state have read 0, 8, 17 and 26 frames displaced,
so an A/B scored on tears needs legs no session can afford. The collapse is a
cheaper signature only if collapses also occur WITHOUT producing a visible
tear; at four in 600 s they do not yet, and whether that is because every
collapse tears or because the 25 ms sample misses most of them is untested.
