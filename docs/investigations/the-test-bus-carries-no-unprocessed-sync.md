# The test bus carries no unprocessed sync, so the vertical interval cannot be timed off it

The coast lengths are chosen per sync type and the sync type is a stand-in for
whether the vertical interval is serrated, which no register reports. The
optimum is one value wide -- `SP_POST_COAST` 4 is the only value the bench's two
csync sources share -- so deriving it from a measurement rather than a constant
is worth a route.

`/edgetrace` is that attempt, by timing the gaps between successive pulses: a
serrated interval carries pulses at twice line rate, an unserrated one carries
none at all, so a run of consecutive periods away from the line period would
give the hold's width in lines. The instrument works. The signal does not
exist on the bus.

## What the instrument does

`TEST_BUS_SEL` drives one chosen block onto `DEBUG_IN_PIN`, and an
`ICACHE_RAM_ATTR` ISR records each rising edge's `ccount`, storing the gap to
the previous edge. Periods saturate at 0xffff rather than wrapping, so an
overflow reads as one. The buffer is heap for the duration of the call, so an
idle build pays nothing for it, and 700 periods covers a field at 31 kHz.

**EVERY EDGE RECORDED, NOT COUNTED.** `debugPinSpanTicks()` spans N periods and
divides, which is the whole point of it -- one pulse carries both ISRs' latency
and N divides that error by N -- and it therefore averages away exactly the
structure being asked for. `MeasurePeriod`'s measure ISR already fires on every
intermediate edge and discards the timestamp.

**A POLLED READER CANNOT STAND IN.** Tried first, because it needs no ISR: at
the 30 us periods the input formatter's horizontal runs at, a `digitalRead` poll
drops pulses, and a dropped pulse reads as a period of exactly twice the median
-- indistinguishable from a pulse the source did not send. The first traces were
littered with them.

## What is on the bus

`vga` on composite sync at 320x256@50 into 1080p, 25 ms windows, transitions and
duty from `/testbus`. A 15.6 kHz pulse train is ~780 transitions at a low duty.

Selector 10 is the sync processor's, and it is the only selector that answers to
`SP_TEST_MODULE`: 47, 7703 and 2 transitions for the three stages tried. All 32
stage and signal pairs on it:

| stage | what every signal reads |
|---|---|
| 4, `vs_act_det` | 0 transitions, stuck at a level |
| 5, `cs_sep` | signals 0 and 7 at ~7600 transitions and 50% duty; the rest stuck or single digits |
| 6, retiming | signals 0, 2, 3 at ~8000 and 50% duty; the rest stuck |
| 7, out proc | 2 transitions at 4% duty on every signal -- the field rate |

So the sync processor offers a ~150 kHz clock, a stuck level, or the vertical.
No stage carries a line-rate pulse train.

The input formatter's selectors, read on selector 0:

| `IF_TEST_SEL` | transitions | duty |
|---|---|---|
| 0 | 1074 | **16%** |
| 1, 2, 7 | 1320, 1362, 1192 | 80..82% |
| 3 | 2 | 14% -- the vertical `selectInputVsync()` already uses |
| 4 | 8096 | 50% |
| 5 | 96 | 50% |
| 6 | 1124 | **15%** |

`if=0` and `if=6` are the only sync-shaped trains, with 1, 2 and 7 their
inverse.

## Why `if=0` is not the source's sync

Traced on the same state: median period 30.5 us, which is **32.7 kHz, the
doubled line**. It is the formatter's horizontal, downstream of the line
doubler, not the sync the separator was handed.

And it does not mean the same thing on another source. The Wii at 480p on
`ypbpr`, same selector: median 5.19 us -- 193 kHz -- with periods from 2.9 to
98.7 us, and `if=6` returns no edges at all.

RD-5725-1.1 tabulates no values for `IF_TEST_SEL` or for the stage signal
selects, so there is nothing to narrow a search with: 32 selectors by 8 stages
by 8 signals, and the answer differs per source.

## The structural reason, which is the one that matters

Every signal the bus offers is **downstream of the sync processor the coast
configures**. Timing one to choose the coast asks the chip about a state the
coast already decided, which is the circularity
[`../sync-type-selection.md`](../sync-type-selection.md) records for the sync
type. The one signal that would break it -- the composite sync as it arrives at
the input pin -- is on no test bus and reaches no ESP GPIO.

## What to do instead

Hold the sync processor in a KNOWN state and compare two instruments, which is
the move `/refclock` already makes for the divider. At coast 0/0 the serrations
reach the counter: the Wii's `STATUS_SYNC_PROC_VTOTAL` collapses to 356 and
dithers across 8 to 14 values in a 14 s window, while `VPERIOD_IF` stays within
five counts of the truth, and the RISC PC reads 308 at every one of 121 coast
pairs. The disagreement is the serration, and
[`two-owners-of-the-coast-lengths-double-the-count.md`](two-owners-of-the-coast-lengths-double-the-count.md)
is what makes it sound: `VPERIOD_IF` is disturbed by a bad coast but never once
reported a doubled value in 363 samples, so it cannot make the error that
matters.

`SourceMeasurement::countIsSerrations()` was that comparison. It was deleted
once the doubling stopped being reachable at the engine's coast; at 0/0 it is
reachable deliberately.

`../known-issues.md` carries the three constraints that design has to meet --
the acquisition budget, the sync pad being away while the picture drops, and the
field rate being timed on the signal the post coast gates.
