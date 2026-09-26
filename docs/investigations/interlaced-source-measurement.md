# The sync processor cannot measure an interlaced source accurately

Measured on a Wii at PAL 576i over YPbPr, 2026-08-26 — the first interlaced
source this project has had.

| quantity | reads | truth | error |
|---|---|---|---|
| line rate from `HPERIOD_IF` (431) | 15625.0 Hz | 15625.0 | exact |
| the engine's `lineRateHz` | 15969 | 15625 | **+2.2%** |
| `STATUS_SYNC_PROC_VTOTAL` | 310 | 312.5 | −0.8% |
| `VPERIOD_IF` | 624 | 625 | −0.16% |

`STATUS_SYNC_PROC_VTOTAL` counts a **field** and `VPERIOD_IF` a **frame**, which
is why 310 against 624 is not a disagreement. But 576i carries **312.5** lines
per field, and a line counter cannot hold the half. The engine derives its line
rate from that count and a field rate measured beside it, and lands 2.2% high.

`HPERIOD_IF` is exact because it does not count lines at all: it measures the
line period against the chip's own 27 MHz, so interlace is invisible to it.
`27e6 / ((431 + 1) * 4)` is 15625.0 Hz to the digit.

## What it costs today

The framing a user tunes is stored against the source's measured identity, and
this source saved as:

```
0 310@52 = 462 9356 387 8887
```

**52 Hz**, for a 50 Hz source. The key is wrong, so a later solve that measures
51 or 50 will not match it and the framing will not come back. On a progressive
source the same mechanism works — `311@50` and `524@60` are both correct in that
same file.

## What it does not establish

That the engine should switch to `VPERIOD_IF`/`HPERIOD_IF`. The geometry model
is built on `STATUS_SYNC_PROC_*` throughout, and **those count in ADC samples
while the IF registers count in IF units against 27 MHz** — the downstream
arithmetic assumes the former. `HPERIOD_IF` also has its own failure modes;
`hperiod-if-railing.md` records three, one of which is a stable wrong value that
every health check scores as healthy.

What is established is narrower: on an interlaced source the sync processor's
line count is short by half a line by construction, and anything derived from it
inherits that. A source key built from it is wrong by enough to miss.

## The other thing interlace does

The last captured line alternates between fields, so a capture window that
reaches into input blanking shows a single shimmering line at the bottom while
everything above it sits still. It reads as a deinterlacing fault and is not one
— it is the framing, and pulling the vertical extent in clears it. The rest of
the picture is stable throughout, which is the tell: bob judder moves the whole
image.


## NTSC 480i alternates where PAL 576i held steady

The same console, the other output mode, measured 2026-09-12 over 1417 samples:

```
STATUS_SYNC_PROC_VTOTAL   260 x736, 259 x681   -- two values, near evenly
VPERIOD_IF                524 x1417
```

576i reads a single steady 310, so **the half-line is expressed as an
alternation in one mode and absorbed into a constant undercount in the other**.
Both are short of the true field: 259.5 against 262.5, and 310 against 312.5.

**The 576i reading survives dense sampling, so the hole is real.** The original
was a single value taken before the sampling log existed, which invited the
explanation that it had simply been under-sampled -- this register is known to
mislead over HTTP, where point reads gave 149, 160, 230 and 299 among the 310s
that the log reported 1050 times out of 1050. Re-measured on the device:

```
Wii PAL 576i    STATUS_SYNC_PROC_VTOTAL   310 in 1186 of 1186, 0 changes
                VPERIOD_IF                624 in 1186 of 1186
                IF_HS_DEC_FACTOR 1, STATUS_IF_VT_OK 1
```

**So an alternating count is not an interlace detector.** One interlaced source
alternates and another does not, which rules out the obvious reading of the
2026-09-12 measurement. Nor is the ratio `VPERIOD_IF / STATUS_SYNC_PROC_VTOTAL`,
which is about 2 on both -- and is 2.02 on the progressive RISC PC under
composite sync as well, because `VPERIOD_IF` counts the doubled IF line rather
than the source's.

**What it costs is acquisition.** `VideoSourceAcquisition::countHeld()` needs four
consecutive identical counts, which an alternating field count never supplies, so
480i never reaches `acquired`: no solve runs for the mode, the output clock is
never seeded, and the picture rolls while every register reads correct.
`mode-detect-answers-before-any-measurement.md`.


## The parity of `VPERIOD_IF` is not a scan type, and is no longer read as one

An interlaced field carries a half line, and `VPERIOD_IF` can hold one -- but
only where the input formatter doubles the line, which is what puts the count in
half lines. Where the line is not doubled the bottom bit is the bottom bit of a
line count and carries nothing.

The rule drawn from that premise was one line:

    interlaced  <=>  (VPERIOD_IF + lineDoubled) is odd

and it was measured unanimous over 3163 samples. Every one of those states was
line-doubled SD, so the premise held throughout and never had to be checked;
`scanTypeFor()` then applied the parity to any count between 200 and 1300.

**It misreads an undoubled source whose frame total is even.** A RiscPC at
800x600@60 over composite sync reads 1255 -- odd, inside the range, and the
source progressive. Measured 721 samples from `loop()`, 1255 in 539 of them,
with the motion-adaptive deinterlacer engaged on it: `MAPDT_VT_SEL_PRGV` 0 and
`WFF_ENABLE`/`RFF_ENABLE` 1, against a clean 640x480 reading 524 with the
deinterlacer off. Separate sync then left it engaged, because `STATUS_IF_VT_OK`
0 makes the period 0, which scored as neither scan type and advanced no run.

So the parity is withdrawn and the alternation below is the whole rule.
`ScanUnknown` is withdrawn with it: a source that cannot be shown to be
interlaced is taken as progressive and the ladder says so. The costs are not
symmetric -- deinterlacing a progressive source corrupts the picture, where
leaving an interlaced one alone combs it and the deinterlacer's own preference
turns it on.

**What that costs is stated where it is paid.** A Wii at PAL 576i holds a steady
310 while genuinely interlaced, 1186 samples with zero changes, so the
alternation does not see it and it is steered as progressive.

**`VPERIOD_IF` still measures the frame, and that is what it is read for**:
against the sync processor's count it gives the vertical sync an unserrated
composite source loses.
[the-risc-pc-composite-sync-is-not-serrated.md](the-risc-pc-composite-sync-is-not-serrated.md)

**The classification cannot supply this.** `s0_00..05` is byte-identical across
a real interlace change at 15 kHz and 50 Hz, `a7 00 00 00 40 10` both ways, so
`STATUS_IF_INP_INT` and `STATUS_IF_INP_PAL_INT` report a vertical-period family
and nothing about scan. Consulted first, as `Deinterlacer` used to, it engaged
the motion-adaptive deinterlacer on the progressive RISC PC and held it there
through a real interlace change in both directions -- measured on the bench,
`MAPDT_VT_SEL_PRGV` 0 with `WFF_ENABLE` and `RFF_ENABLE` 1 in all three states.
With the measurement answering, the same three states read off, engaged, off.

**It only works where `VPERIOD_IF` does**, which is with the sync separator in
the path. On separate sync the register holds debris -- 33 to 101 on the bench
source -- and `STATUS_IF_VT_OK` reads 0 beside it, which is the gate the caller
gives it. There the scan type has no source at all.

### The measured scan type cannot replace the classification here, and the reason is circular

**`countIsSerrations()` and the two `ModeDetect` predicates are deleted**, so nothing takes this decision any more. The circularity below is why the
obvious cleanup was never available, and it is kept because the same shape recurs wherever a count is judged against a witness derived from it.
[two-owners-of-the-coast-lengths-double-the-count.md](two-owners-of-the-coast-lengths-double-the-count.md)

The obvious cleanup -- feed `countIsSerrations()` the measured scan type
instead of the STATUS_00 bits -- does not work, and it fails in the direction
that matters.

`scanTypeFor()` needs the line doubling, and the doubling is solved from the
line count. On a count the serrations have doubled, the scan mode is solved for
that corrupted count and comes out undoubled; the parity rule then reads the
period as progressive, and a progressive source cannot have doubled -- so the
check that exists to catch the doubling disables itself on exactly the source it
was written for. Measured: a 607-line count against a 624 period stops widening
the coast, and the source never comes up.

The gate has to be independent of the count under suspicion, and the
classification bits are. They do not answer the scan type -- nothing here
retracts that -- but they are not derived from the count, which is the property
this use needs.

**`SourceMeasurement::countIsSerrations()` still takes the classification**, and
it is the second consumer of the same unreliable bit. It asks whether a count
could have doubled, which only an interlaced source can do. On every state
measured here it reaches the same verdict either way, so there is no fault to
chase -- but the measured scan type is the better input and is now available
beside it.


## A PAL interlaced source is where the alternation is blind

576i reads `VPERIOD_IF` 624 against a count of 310 and holds both steady, 1186
samples with zero changes on each. The parity rule named it interlaced from
that single sample; with the parity withdrawn, nothing here does.

**Why it holds steady is not established.** The sync-route table above is the
closest thing to a mechanism -- the separator retimes vertical sync and can
absorb the half line into a constant undercount -- and the Wii 480i row
contradicts it, being SOG with the separator in path and alternating anyway. A
broad-pulse width explains the two Wii readings and predicts the four RISC PC
rows wrongly, since a pulse width is a property of the signal where the
behaviour follows the route.

So the bound on the alternation is stated as measured rather than as derived:
**it is blind to at least one genuinely interlaced source, and which sources
those are is not predictable from the timings.**

## On separate sync the count alternates at every raster tried

The alternation is the only scan-type signal that survives separate sync, and it
had been measured at one raster. A second one, same machine and cable, only the
mode and the interlace flag moving:

| mode | field | progressive | interlaced |
|---|---|---|---|
| 320x256@50 | 312.5 | 311 steady, 0 changes in 372 | 311/312, 271 in 369 |
| 640x200@60 | 262.5 | 261 steady, 0 changes in 384 | 261/262, 188 in 382 |

Steps are exactly plus or minus one in both, and no progressive sample ever
moves.

**Set against every interlaced state measured, the hole belongs to the sync
separator rather than to the raster:**

| sync route | field | interlaced |
|---|---|---|
| separate | 312.5 | alternates |
| separate | 262.5 | alternates |
| composite, separator in path | 312.5 | steady 309 |
| composite, separator in path | 262.5 | steady 259 |
| SOG, Wii 576i | 312.5 | steady 310 |
| SOG, Wii 480i | 262.5 | alternates 259/260 |

The separator retimes vertical sync and can absorb the half-line into a constant
undercount; separate sync passes the VSync pin through more directly and the
half-line reaches the counter. **That puts the hole only where the alternation is
not needed**, since `VPERIOD_IF` is a measurement wherever the separator is in
the path.

**What this does NOT test is the signal structure.** Every interlaced state the
RISC PC can produce comes from `*TV vert,interlace`, which offsets the fields but
does not synthesise broadcast equalisation and serration pulses. So the raster is
tested and the vertical interval is not, and a genuinely broadcast-interlaced
source on separate sync is the one thing this bench cannot make. A mode file does
not close that gap -- it would match the active area, not the pulse structure.
The one separator-path source with real serrations, the Wii at 480i, does
alternate, which is at least not evidence against.

## Three explanations of the steady count, all refuted

Why a 312.5-line field holds a steady count on the separator path is still
unexplained, and so is the factor the reconciliation has to pick. These are the
models that looked sufficient and are not, so they are not re-proposed.

**A broad-pulse width — PAL 2.5 lines against NTSC 3 — does not explain it.**
The six-state table above refutes it from its own rows: the *same* 312.5-line
field alternates on separate sync and holds steady on composite. The behaviour
follows the **route**, and a property of the source's pulse cannot vary with
which of this board's separators the signal took.

**"A sustained refusal of the reconciliation means interlaced" is not a
detector.** It works at 312.5 and fails at 262.5, where a composite interlaced
source reconciles to a plausible vertical sync width and the refusal never
comes. A detector that holds on one field count and not the other is reporting
the field count.

**An odd or even frame total does not predict the 1x/2x factor.** Failed on
three of four fresh modes. `SourceMeasurement::reconciledFrame()` picks the
factor by testing both against the counted frame rather than deriving it, which
is why it survives modes this model would have mis-assigned.

The reconciliation does not apply to an interlaced source and does not claim
to: the counter holds a field where `VPERIOD_IF` holds a frame, no factor
reconciles them, and the raw count stands.

## `VPERIOD_IF` is in half-lines on sources the line doubler is not doubling

The premise the withdrawn parity rule rested on -- that only line doubling puts
the register in half lines -- is refuted by measurement. A RISC PC at
800x600@60 over composite sync runs **undoubled** (`IF_HSYNC_RST` equal to
`PLLAD_MD`, not half of it) and reads 1255, where the DMT total is 628 and
`1255 + 1 = 2 x 628`. A RISC PC at 640x480@60, also undoubled, reads 524 against
a DMT total of 525, which is whole lines.

So the register's scale is a property of the source that neither the sync
arrangement nor the engine's scan choice predicts, and
`SourceMeasurement::reconciledFrame()` resolving it by testing both factors is
the only thing on the board that establishes it.

### The high byte tears, and the bottom bit does not

Sampled from `loop()` at 25 ms, 800x600@60 composite gives 1255 x740, 615 x137,
1267 x123 and 627 x75 in 1075 samples, 542 transitions. Every pair differs by
exactly **640**, which is 5 x 128 -- the field spans `s0_07[7:1]` for its low
seven bits and `s0_08[3:0]` for bits 7..10, so the corruption is the high
nibble going 9 to 4 inside the chip's own two-byte burst. An atomic read does
not avoid it.

Two consequences. A ratio taken per sample is wrong on a torn reading -- 616
against a count of 624 computes 1 where the source is 2 -- so the ratio must be
held. `reconciledFrame()` already rejects every torn sample, because
`frame >= counted` fails at 616 against 624 on both factors.

And the bottom bit is untouched, 640 being even: `(VPERIOD_IF + 1)` is even in
1075 of 1075 samples with interlace off and odd in 1062 of 1062 with it on,
through those 542 transitions.

## The alternation fires on one interlaced state of five

`countAlternated()` is the whole interlace rule. Measured across every
interlaced state this bench can produce, with `MAPDT_VT_SEL_PRGV`,
`WFF_ENABLE` and `RFF_ENABLE` read at each:

| interlaced state | count | alternates | motion adapt |
|---|---|---|---|
| Wii 480i | 259/260, 258 steps each way | yes | **engaged** |
| Wii 576i | 310 in 1072/1072 | no | bypassed |
| RISC PC 320x256@50 `INTERLACE ON` | 309 steady | no | bypassed |
| RISC PC 800x600@60 `INTERLACE ON` | 624 in 1060/1062 | no | bypassed |
| RISC PC 640x480@60 `INTERLACE ON` | 523 in 706/706 | no | bypassed |

Four of the five are steered as progressive and shown as a single field, line
doubled -- correct in shape and aspect, at half the vertical resolution, which
is why the cost is invisible without a reference.

**480i reaches `acquired`.** An earlier reading that it cannot predates
`SteadyRun::agree()` treating a pair alternating by one as agreeing.

## A parity rule keyed on the measured ratio, pre-registered and not yet adopted

The rule under test is

    interlaced  <=>  (VPERIOD_IF + 1) is odd,  and only where the held ratio is 2

where the ratio is `reconciledFrame()`'s factor, held rather than recomputed.

**What makes it a different rule from the withdrawn one** is the correction
term. The withdrawn rule corrected by `lineDoubled`, which is the engine's own
scan decision; this one corrects by a ratio measured from the witness against
the count. At 800x600@60 those disagree -- `lineDoubled` 0 against a ratio of 2
-- and that state is what refuted the withdrawn rule.

**The mechanism**, which the withdrawn rule had none of: `VPERIOD_IF + 1` is the
frame total in the witness's own unit. At ratio 2 that unit is half lines and a
progressive frame is an even number of them by construction, so odd is the extra
half line an interlaced field carries. At ratio 1 the unit is whole lines and
the parity is the mode's own total, which carries nothing, so the rule declines
there rather than guessing.

**States used to construct it**: RISC PC 320x256@50, 800x600@60 and 640x480@60
each `INTERLACE OFF` and `ON`, Wii 576i, Wii 480p.

**States predicted before measurement**: Wii 480i -- ratio 2 from a field count
of 259/260 against `VPERIOD_IF` 524, so odd and interlaced, where 480p at the
*same* register value gives ratio 1. Measured: 524 in 1083/1083, ratio 2.01/2.02
in every sample, `(VPERIOD_IF + 1)` odd in 1083/1083, motion adapt engaged.

### The pre-registered test

Seven modes, none of them in the construction set, five with **odd** progressive
totals -- the property that produced the withdrawn rule's false positive, since
an odd total read as whole lines scores interlaced.

| mode | DMT total | line rate |
|---|---|---|
| 800x600@56 | 625 odd | 35156 |
| 800x600@75 | 625 odd | 46875 |
| 640x400@85 | 445 odd | 37861 |
| 1920x1080@60 | 1125 odd | 67500 |
| 1024x768@60 | 806 | 48363 |
| 1280x960@60 | 1000 | 60000 |
| 1152x864@75 | 900 | 67500 |

Predictions, each falsifiable on its own:

1. Where the ratio is 2, `(VPERIOD_IF + 1)` is **even** with `INTERLACE OFF` and
   **odd** with `INTERLACE ON`.
2. The ratio is the same for both interlace states of a mode.
3. `(VPERIOD_IF + 1) / ratio` equals the mode's published progressive total.

4. The parity is **unanimous across every sample of a state**, not merely the
   most common one. A rule keyed on a bit that flips intermittently misfires
   intermittently, which is worse than one that never fires: the register's high
   byte tears on a fifth of samples at some modes, so this has to be shown
   rather than assumed.

Any mode at ratio 2 reading odd with `INTERLACE OFF` refutes the rule, and so
does a parity that splits within one state. Parity then stays withdrawn.

### Outcome: predictions 1 to 3 hold, prediction 4 fails, so parity stays withdrawn

Seven modes, both interlace states, sampled from `loop()` at 25 ms, composite
sync throughout.

| mode | published | `INTERLACE OFF` | `ON` | ratio | `(VPERIOD+1)/R` off / on |
|---|---|---|---|---|---|
| 800x600@56 | 625 | 1249, even 358/358 | 1250, odd 358/358 | 2 | 625.0 / 625.5 |
| 640x400@85 | 445 | 889, even 372/372 | 890, odd 372/372 | 2 | 445.0 / 445.5 |
| 1024x768@60 | 806 | 1611, even 358/358 | 1612, odd 359/359 | 2 | 806.0 / 806.5 |
| 1152x864@75 | 900 | 1799, even 366/366 | 1800, odd 368/368 | 2 | 900.0 / 900.5 |
| 1280x960@60 | 1000 | 1999, even **354/357** | 2000, odd 78/78 | 2 | 1000.0 / 1000.5 |
| 800x600@75 | 625 | 624 | 625 | **1** | 625.0 / 626.0 |
| 1920x1080@60 | 1125 | 1124 | debris | **1** | 1125.0 / -- |

**Prediction 3 holds exactly on all seven**, at both ratios: the derived total
equals the published total to the unit, and the interlaced state adds exactly
**0.5**. That is the strongest result here and it establishes the ratio itself
-- `reconciledFrame()`'s factor recovers the mode's raster rather than fitting
it.

**Prediction 4 fails at 1280x960@60 with `INTERLACE OFF`.** `VPERIOD_IF` reads
1999 in 354 samples, 2000 in two and 1998 in one, so parity reports interlaced
in 3 of 357. A **one-count dither flips the bit**, where the 640-count tear
measured elsewhere cannot. The rule as pre-registered is per sample, so it is
refuted at the rate of roughly one sample in 120.

A corroborated variant may survive -- the three dithered readings are rejected
by `reconciledFrame()`, because at ratio 2 a reconcilable reading must be even
and 2001 and 1999 are not. **That is a different rule and it needs its own
pre-registered test**, not this one's result. Rescuing a refuted rule with a
qualifier the data suggested is how the first parity rule was arrived at.

### The ratio is not a property of the raster

800x600 at **56 Hz reads ratio 2** and at **75 Hz reads ratio 1** -- same
resolution, same published total of 625, same cable and sync arrangement. So
the factor is not derivable from the raster, and it does not follow the line
rate either:

| line rate | ratio |
|---|---|
| 31500 | 1 |
| 35156 | 2 |
| 37861 | 2 |
| 37879 | 2 |
| 46875 | **1** |
| 48363 | **2** |
| 60000 | 2 |
| 67500 | 1 and 2, at different modes |

Nine points, no threshold and no ordering. This is why the factor is tested
against the count rather than derived.

**1920x1080@60 interlaced does not settle** on this bench -- the count wanders
1119..1153 and `VPERIOD_IF` falls to 201..203 debris, so that row is a mode
limit rather than evidence about the rule.

## The scan type is a stored choice, not a measurement

Nothing on this board detects interlace reliably. The classification bits report
a vertical-period family, the parity of `VPERIOD_IF` is refuted twice, and the
count alternates on one interlaced state in five. The design stops trying.

The scan type is resolved in this order:

1. **The choice stored against the `SourceKey`**, alongside the framing.
2. **The published raster**, where `SourceTiming::matching()` finds one and that
   row is interlaced.
3. **Progressive.**

The default is progressive because the costs are not symmetric: motion adapting
a progressive source corrupts the picture, where leaving an interlaced one alone
halves its vertical resolution and looks correct. An unresolved source is
therefore shown, not guessed at.

**The published row is what makes the default right for a standard source, and
it is not optional.** A bobbed interlaced source is stable, correctly shaped and
correctly proportioned -- there is no cue that anything is wrong, so a
choice-only design leaves every standard interlaced source at half resolution
until somebody measures one. `SourceTiming::Raster` carries `interlaced` for
that reason, and the interlaced CEA rows sit beside the progressive ones already
in `Published[]`.

**`countAlternated()` is discarded.** It fires on Wii 480i and on none of Wii
576i, RISC PC 320x256@50, 800x600@60 or 640x480@60 with `INTERLACE ON`. A rule
that answers one state in five is not a detector, and `CrossingsForInterlace`
and `AlternationStaleRun` exist only to stop a single off-by-one reading being
taken as interlace -- a signal defended against itself.

`SteadyRun::agree()` treating a pair alternating by one as agreeing **stays**.
That is what lets an interlaced field count settle at all; without it 480i never
acquires. Only `alternated()` and the three constants serving it go, with their
cases in `test_steady_run.cpp`.

**Order of work.** Alternation is today the only reason Wii 480i engages motion
adapt, so the interlaced published rows have to be in and matching on the bench
before it is removed, or 480i regresses to a bob.

**What the published row cannot reach.** The sync processor counts a field on
some interlaced sources and a frame on others -- Wii 576i counts 310 against a
625-line frame, where RISC PC 800x600@60 `INTERLACE ON` counts 624, one more
than the same mode progressive. A frame-counted interlaced source matches no
interlaced row and rests on the stored choice. The keys do separate the two
variants of a mode, 623 against 624 and 308 against 309, so a stored choice
attaches to the right one.
