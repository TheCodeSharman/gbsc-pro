# One source takes many keys, and its stored shape goes with them

A framing and a shape tuned on one source come back on some boots and not
others, with nothing touched between them. Two independent causes, both
measured, both now closed.

The bench state throughout: the RISC PC on `vga`, 311 lines, settled field rate
50.474 Hz, `/framing.txt` holding three records for that one source, one of
which carries a 4:3 shape.

    311@50.45/732++ = 2588 6412 1090 8269 13333
    310@50.00/741?? = 1971 7956 1479 8215 0
    311@50.61/732++ = 2698 6249 1154 8205 0

## The shape never left the file

`loadFramingTable()` parses the file into a scratch `FramingTable` so a read
failing part way cannot leave the live table holding half a file, then copied
the entries across one at a time -- naming the key and the framing, and not the
shape, which took `Aspect()`'s default of Fill. **Every stored shape was
discarded on every boot, for every source.**

The console says it in one line. The record above holds 13333 and the recall
reports:

    source key: 311@50.45/732++, framing recalled, shape 0

with `/geometry` answering `"aspect":0` beside a framing that is the stored
record's to the unit. The text codec round-trips the shape and is tested doing
so, so nothing was lost in the file or the parse.

`FramingTable::adopt()` crosses that gap now, so the copy has one owner rather
than a list of fields written out at the call site. The table's own tests had no
shape coverage at all, which is what let it through.

## The boot's first solve takes a key the source does not have

Every later acquisition of this source reads 50.47. The first solve of a boot
does not. Measured across six boots, untouched between them:

| boot | key the first solve took | shape it recalled |
|---|---|---|
| 1 | 311@50.61 | 0 |
| 2 | 311@50.45 | 13333 |
| 3 | 311@50.53 | 0 |
| 4 | 311@50.45 | 13333 |
| 5 | 311@50.45 | 13333 |
| 6 | 311@50.45 | 13333 |

So the shape appears on the boots whose key happens to land on the record that
carries it. 50.45 against 50.61 is 3.2 per thousand, outside the 3
`SourceIdentityPerThousand` allows, so those are two keys and the one source
accumulated a record under each.

### The readings ladder, and agreement picks a rung

The whole of one boot's first solve, from the console:

    sampling: 311 lines x 50.45 Hz -> line rate 15740
    sampling: 311 lines x 50.53 Hz -> line rate 15765
    sampling: 311 lines x 50.45 Hz -> line rate 15740
    sampling: 311 lines x 50.53 Hz -> line rate 15765
    sampling: 311 lines x 50.60 Hz -> line rate 15788
    sampling: 311 lines x 50.60 Hz -> line rate 15788
    sampling: rate 15788 doubled 1 -> divider 2200
    source key: 311@50.61/732++, framing recalled, shape 0

The rungs sit about 80 mHz apart, which is half a source line at 311 lines and
50.47 Hz. `rateSettled()` promotes the first consecutive PAIR agreeing within
`RateAgreementPerThousand`, which is 1 -- and one rung is 1.6 per thousand, so
adjacent rungs never agree and an agreement means two samples landed on the SAME
rung. **Which rung that is, is luck**, and it becomes the source's identity and
the raster's rate.

**A MEDIAN REJECTS AN OUTLIER AND NOT A SPREAD.** `medianOfThree()` returns the
middle sample whatever the three are -- no agreement is required and nothing is
ever rejected -- so three samples on three rungs yield a rung. It is sized
against the signature `single-sample-rate-jitter.md` measures on a SETTLED
source, one sample in eighty displaced by a whole line, and it does kill those:
two boot logs here carried a 51.35 and a 51.61 that never reached a reading.
Against a spread it passes the ladder straight through, which the console shows
directly -- every `sampling:` line above is ALREADY a median of three.

**The two signatures are different and must not be merged.** After the boot, the
same measurement on the same source reads 50.47 with occasional single
excursions of one whole line:

    sampling: 311 lines x 50.47 Hz -> line rate 15748
    sampling: 311 lines x 50.47 Hz -> line rate 15748
    sampling: 311 lines x 50.63 Hz -> line rate 15798
    sampling: 311 lines x 50.47 Hz -> line rate 15748
    sampling: 311 lines x 50.47 Hz -> line rate 15748

Half-line rungs spanning several values during the first solve; whole-line
single excursions afterwards. **What makes the first solve different is not
established.** The filesystem is ruled out on timing -- it is mounted at 1.73 s
and the framing table loaded at 1.78 s, five seconds before the first reading --
and detection's own 2.57 s of register writing finishes immediately before it.
The reading is taken through the reference divider rather than the one the
source ends up on, which is a lead and not a measurement.

### The correction reached the rate and not the key

The periodic recheck re-solves once per boot and reaches the settled rate every
time, 50474 of 50474. The key did not follow it: `adoptSourceKey()` returns
early when the arriving key compares equal to the one held, and **the corrected
reading does compare equal -- it is the same source.** One `source key:` line
per boot and none after the correction.

So the key is replaced not on a change of source, of which there is none, but on
the reading behind it getting better. The early return asks whether the rate
behind the key in force had been confirmed when it was taken; while it had not,
a solve re-derives. Measured after the change, two `source key:` lines a boot:

    09.44  source key: 311@50.45/732++, framing recalled, shape 13333
    19.62  source key: 311@50.48/732++, framing recalled, shape 13333

The gate is what stops the stored framing being recalled over a user who has
tuned it and not yet had it written out, so it is not simply dropped.

**The recheck is therefore still load bearing, and more so.** It was the only
thing correcting the boot's rate; it is now also the only thing correcting the
boot's key. Deleting it needs the first solve's reading made good first.

## Identity is not transitive, and a first match let the file decide

50.474 is 0.46 per thousand from 50.45 and 2.65 from 50.61, so it is inside
tolerance of both -- while they are 3.11 apart and outside tolerance of each
other. `SourceKey` equality is symmetric and **not transitive**, which is
structural rather than a defect in the relation.

Two consequences, both closed:

- `FramingTable::indexOf()` was a linear scan returning the FIRST match, so with
  both records present the order the file happened to be written in decided
  which framing the source got back. It returns the nearest now, by
  `SourceKey::distanceTo()` -- the separation on each tolerant axis as a
  fraction of what that axis allows, summed.
- Nearest-match makes a lookup deterministic and does not stop the split. Two
  records both identified by one reading shadow each other for ever, and a later
  reading lands on whichever it sits nearer. A store now drops every other entry
  the stored reading also identifies, collapsing them onto the one just written.

**Widening `SourceIdentityPerThousand` is not the answer and was rejected.** The
spread is a transient in the measurement rather than a property of the source,
so identity is the wrong place to absorb it; widening also puts more records
inside tolerance of each other, every one of them still resolved by proximity to
a reading that wanders. A bucket would be transitive but straddles boundaries,
which `SourceKey.h` rejects by measurement.

## The reading itself is what was fixed

No order statistic recovers a value from a spread, so the instrument is where it
had to be answered. One timing was one vsync period between two edge ISRs, which
puts the whole of both ISRs' latency into the answer; `MeasurePeriod` counts
edges and stops on the Nth now, so a timing spans
`SourceMeasurement::UnconfirmedRatePulses` pulses and divides that fixed error
by the count.

Only the boot pays for it -- `rateConfirmed_` already marks that window, every
later acquisition reads the source exactly, and N pulses cost N field periods a
timing. Measured across eight boots afterwards, every first-solve reading on the
same source:

| reading | boots before | boots after |
|---|---|---|
| 50.45 .. 50.69, four rungs | 4.8 per thousand | -- |
| 50.47 / 50.48 only | -- | **0.2 per thousand** |

and the key lands on 311@50.47 or 311@50.48 every time, recalling the stored 4:3
on **8 boots of 8** rather than on whichever boots were lucky. It costs nothing
in time: three readings settle it where six did not, so the first key lands at
9.2--9.6 s against 8.3--12.6 s before.

## The re-solve that gave back an identical picture

A re-solve takes the sync pad away for the encoder relook, so one armed on a
rate that had not moved is about 0.9 s of black for nothing. Two separate arms
were doing it.

**The boot's confirmation armed unconditionally.** It had to: both readings were
timed over one pulse, so the tolerance had to be wider than the error being
looked for and a boot error of about one per thousand sat inside it. With both
readings spanned the comparison is worth making, at
`BootRateConfirmPerThousand` -- 1, against the drift tolerance's 2, because a
one-line excursion divided by the span lands at 0.4. A boot whose rate is right
is confirmed where it stands; one wrong by a per thousand still re-solves.

`confirmRate()` no longer drops the held rate with it. Dropping is what a SOLVE
needs, and a confirmation arming none must not, or the next reading is judged
against nothing and `sourceLineRateHz()` reports 0.

**The periodic corroboration read one timing and judged it at 2 per thousand**,
which a single timing cannot support. Measured, with the boot's confirmation
already corroborating, one boot of eight still blanked -- and its arm landed
6.9 s after the solve rather than at `RateRecheckPasses`, so it was the line
period's arm, and the reading behind it was:

    sampling: 311 lines x 50.73 Hz -> line rate 15828

5 per thousand high on a source running 50.47, about 1.6 lines. A disagreement
is now asked again over a span before anything is armed, with the cheap reading
still in front of it: a span costs that many field periods, and paying for one
only once the cheap half has seen something is the same split the line period
and the rate already use.

**The host fake cannot judge either span.** It has no jitter to divide, so
mutating the span to one pulse leaves every suite green. What the stub CAN model
is that one timing may report a different rate from a spanned one, which is what
`g_oneTimingRateHz` says, and that is what separates the two paths in a test.

## What is still open

- A boot can file a record under a transient key if a framing is pressed inside
  the first ten seconds, and the collapse on store is what folds it back.
- **What makes the first solve's readings different is still not established.**
  Spanning makes the reading good without saying why one timing is worse there
  than it is a minute later.
