# A composite-sync source in pass-through measures correctly and re-arms its own solve

The source is read right and solved right, over and over. What keeps the panel
dark is the IDLE pass: the count it reads swings between the source's own and
multiples of it, every swing says the source moved, and the arm takes the output
away and starts the measurement again.

Measured on `vga` at 800x600@60 composite into 1920x1080, `scale-rgbhv = 0`, the
arrangement re-probed with `/sc?~` so `SP_SOG_MODE` is 1. Console captured over
one 42 s window, timestamped.

| | |
|---|---|
| complete solves | **15**, one every 2.8 s |
| arms | **15** -- 13 on the count, one on an unsettled count, one on the rate |
| `measured:` lines reading the correct 627/628 | **50 of 89** |
| distinct counts the idle pass read | **34**, from 253 to 1880 |
| counts the solve measured | 622 to 630 |
| source keys recalled | six, `622@60.32/1205` to `630@60.32/1205` |

A solve completes in full: `measured: 627 lines x 60.31 Hz -> line rate 37879`,
`duty: 173 pulse / 1436 divider, htotal 1436` with the clock latched, the key,
the display clock, the sampling phase and `rate match: the HD bypass channel
carries the video`. The next poll then reads 997 lines against a solved 628 and
arms `TIMINGS change (count)` -- all three inside one poll, with
`STATUS_SYNC_PROC_HTOTAL` 2038 equal to `PLLAD_MD` throughout.

**So the measurement is not what is wrong, and neither is the solve.** Every
candidate downstream of the count inherits a configuration that was correct
when it was written and is then discarded.

## The counts are multiples, so vertical pulses are being missed

623, 1246 and 1870 are the source's count and twice and three times it. A
separator that misses the vertical pulse leaves the counter running into the
next field, which is what a count of 1251 against a solved 627 is. The values
between -- 888, 975, 1333, 1691 -- are a vertical found somewhere inside a
field rather than at its edge.

**`countIsSource()` cannot reject any of them.** Any count from 200 to 1300 is a
source, so 1246 is plausible, and the idle run finds it steady: the arm fires on
evidence it is right to act on.

## The formatter measures the frame where the sync processor cannot

`Tv5725::SamplingLog` at 25 ms, the count sampled from inside `loop()` on each
sync type, same source and mode:

| | separate | composite |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | **627 in 443 of 443** | 76 distinct values, 0 to 1937 |
| within 3 lines of the mode | 443 of 443 | **115 of 250, 46%** |
| `VPERIOD_IF` | debris; `STATUS_IF_VT_OK` 0, so `verticalPeriod()` reads 0 | **1255 half-lines in 116 of 250** |
| `STATUS_MISC_PLLAD_LOCK` | 443 of 443 | 119 of 250 |
| `STATUS_SYNC_PROC_HSACT` | 1 throughout | 1 throughout |

1255 half-lines is 627.5 lines, which is the source's own frame. **So on the
composite path the formatter is the half that reads the source and the sync
processor is the half that does not**, and on separate sync it is the other way
round -- the separator has nothing to extract there, so the formatter completes
no vertical measurement at all.

**The two are good at different moments**, which is what makes the pair worth
more than either: both good in 44 of 250, only the count in 71, only the frame
in 86, neither in 49. So four fifths of samples carry a usable reading of the
frame where the count alone carries under half.

**And no steadiness run can reject the wrong count.** A wrong value repeats for
up to **eight consecutive samples** -- 451, 688 and 1230 each held eight -- and
the idle run settles after four. Through the longest of those runs the formatter
went on reading 1255.

That is what `SourceMeasurement::countContradicted()` asks, through the
reconciliation the engine already had: `reconciledFrame()` returns 0 for a pair
that cannot be one frame, and a count the formatter contradicts is dropped
rather than judged.

Measured either side of it on the same source and mode, `present` over a 120 s
window: **1 and 2 polls of 60 before, 6 of 60 after** on the default build, and
11 of 60 on a `GBS_SAMPLING_LOG=1` build, which is not the same build and so not
the figure to quote. **The count arms still fire** -- 27 of them in 125 s --
where both halves missed the SAME vertical pulse, which they do because both
derive from the same separated vertical, so they agree on a doubled frame and
the corroboration passes it.

**One run of five caught a non-flat frame**, at mean luma 175 with 35 black
columns at the left and 27 rows at the bottom, where every run before the change
was flat at luma 0.00. A later run of 150 s with 12 present polls caught none.
So this is one observation and the picture is not reliable; a clip rather than a
still is what would settle it. It costs
the working sources nothing -- the Wii at 480p on `ypbpr`, which runs sync on
green and so has a live formatter frame, reads present 44 of 45 polls at mean
luma 201, the RiscPC on separate sync 33 of 35 at 82, and four source mode
changes are followed in 1.83 to 2.03 s.

## A count that is a multiple of the solve is refused by arithmetic

The 27 arms the corroboration passes are the ones where **both halves lost the
same vertical pulse**. They derive from the same separated vertical, so the
formatter's frame doubles with the count and the pair reconciles -- 1245 half
lines against a count of 622 is one frame, and there is nothing in the pair that
says it is two.

What separates them is the line period. **Doubling the count at an unchanged
field rate doubles the LINE rate**, so a source that really moved there moves
the period with it: 480i to 480p does, and so does the bench's 320x256@50 to
800x600@60 -- 311 lines at 15.6 kHz to 628 at 37.9. A count that is a whole
multiple of the solved one while `HPERIOD_IF` has not moved cannot be a source
that moved. `HPERIOD_IF` as a change detector is the one role it is trusted in,
and this is that role.

The tolerance is 20 per thousand, because the multiple is of the count the pulse
was lost from rather than of the restored one: 1246 and 1870 sit 8 and 11 short
of exact multiples of a solved 627, 6.4 and 5.8 per thousand. The nearest
reading it has to refuse is 1333, 63 per thousand out, and one multiple is a
whole frame from the next.

**It narrows the count arm rather than dropping the pass.** The arithmetic has
no second instrument behind it, so it must not be the last word: `rateMoved()`
still runs, and its recheck measures the field rate a different way and arms on
that. A source whose count really doubled at an unchanged line period -- 60 Hz
to 30 at one line rate -- is therefore followed late rather than never.

The positive answer is its own reading: `linePeriodHolds()` rather than
`!hasLineRateMoved()`, because the second is also false for a register that
would not hold still, and refusing a count on the strength of a register that
said nothing is refusing it for no reason.

## Refuted: the field-rate measurement reads a line-derived signal

`known-issues.md` recorded the field rate reading 3937.58 Hz against a line rate
of 31500 -- the line rate over eight -- and called it a line-derived signal
reaching a measurement that wants vertical sync. **That reading was taken with
the sync type stale**, the separate-sync configuration in force on a composite
source. With the arrangement right the same signal is a clean field-rate pulse.

`/testbus?ms=25` on each sync type, one cable, one mode, only the RISC PC's
`SYNC` moving. `if=3` is the input formatter's vertical, which
`TestBus::selectInputVsync()` is, and `sel=0` is its bus:

| probe | separate | composite |
|---|---|---|
| `if=3` (IF vertical) | **4 transitions, duty 0.2%** | **4 transitions, duty 0.2%** |
| `if=0` | 2499, 25.9% | 3066, 36.6% |
| `if=1` | 2062, 27.2% | 1691, 11.8% |
| `if=2` | 2444, 24.2% | 3517, 41.6% |
| `sp=4` `vs_act_det` | 115, 49.7% | **0, duty 100%** |
| `sp=6` retiming | 1082, 6.1% | 1500, 49.8% |
| `sp=7` out proc | 0, 0.0% | 2, 1.9% |

Four transitions in 25 ms is two field-rate pulses, and 0.2% duty is a narrow
one. The two sync types are indistinguishable there.

**`vs_act_det` is the one stage that stops carrying anything**: it toggles 115
times on separate sync and sits high for the whole window on composite, which is
`STATUS_SYNC_PROC_VSACT` reading 0 -- the normal composite state rather than a
fault, and now with the stage behind it measured.

## The field rate cannot be read while the clock is a reference one

The IF derives its vertical through the line the clock in force implies, so a
field rate timed off it is meaningless at a reference divider sized for another
line rate. Measured with the separator level searched across the whole field at
that point, three readings a level: **18, 32, 60, 681 and 2367 Hz at levels that
read a steady 60.31 Hz once the source's own divider was latched**. 18.5 Hz
recurs exactly, being the sampler's own timeout reported as a rate.

That bounds what any search run before the source is measured may judge on. The
count answers there; the field rate does not.

## The level's effect on the count is not reproducible across acquisitions

Two consecutive acquisitions of the same source in the same mode, the search
reading 24 counts a level from inside `loop()`:

| level | one acquisition | the next |
|---|---|---|
| 13 | 0..624 | 0..1820 |
| 17 | 624..1690 | 1691..1691 |
| **18** | **624..624** | **1691..1691** |
| 22 | -- | 1246..1246 |

So a level that holds the true count in one acquisition holds 1691 in the next,
and a search stopping at the first steady count accepted **1246 -- the count
doubled**. This is the surface
`two-owners-of-the-coast-lengths-double-the-count.md` records for the coast
pair, and unlike `SP_H_PULSE_IGNOR`, whose value repeats, nothing can be chosen
for the level on one run's evidence.

## Traps

**A level held from outside cannot be judged, because the polling it takes
starves the loop.** The recovery ladder rewrites `ADC_SOGCTRL` every 500 ms, so
holding one against it means writing at 4 Hz, and watching the count means
reading at 2 Hz beside it. Measured: **30 console lines in 338 s against 1070 in
80 s** on the same unit, `arms=0 solves=0` across three 75 s windows where the
engine otherwise solves every 2.8 s. Every reading taken that way describes a
starved engine. Walk the level from inside `loop()` instead.

**`/testbus` output is lost by a parser that anchors on the start of the line.**
The console interleaves `#8` status frames into the text stream, so a `tb,`
line arrives as `#8  #8  tb,header,...`. Searching for `tb,` anywhere in the
line reads 476 rows where a `startswith` reads none -- which looks exactly like
a route that did not run.
