# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

### The ADC gain seeds clip the top of the range on BOTH colour spaces

Measured 2026-10-16 against a CALIBRATED ramp, which is what previous attempts
lacked. `PATTERN PM5544`'s greyscale staircase draws six known levels --
`(i*255) DIV 5`, so 0, 51, 102, 153, 204, 255 -- and the emitted frame is read
off the USB capture. The source steps are 51 apart, so the emitted gaps say
directly where the transfer saturates.

`vga` at 640x480@60 into 1920x1080, sweeping `ADC_RGCTRL`/`GGCTRL`/`BGCTRL`
together:

| gain | 0 | 51 | 102 | 153 | 204 | 255 | top gap |
|---|---|---|---|---|---|---|---|
| 0x33 | 0 | 60.2 | 133.8 | 210.6 | 235.6 | 235.3 | **-0.3** |
| 0x50 | 0 | 53.6 | 116.5 | 185.7 | 235.4 | 235.2 | **-0.2** |
| **0x7B** (`RgbGain`) | 0 | 42.6 | 99.3 | 158.6 | 219.1 | 235.3 | **16.1** |
| 0x98 | 0 | 38.2 | 89.9 | 143.1 | 200.3 | 235.3 | 35.1 |
| 0xB0 | 0 | 35.6 | 83.4 | 132.3 | 185.3 | 235.3 | **50.0** |
| 0xB8 | 0 | 34.7 | 80.8 | 129.8 | 181.2 | 234.5 | 53.3 |
| 0xC0 | 0 | 33.8 | 78.2 | 127.1 | 177.4 | 230.0 | 52.6 |

**0xB0 is the knee**: the first value whose top step is intact -- 50.0 against
the source's 51 -- while white still reaches the 235 ceiling. Above it white
falls away and range is thrown away for nothing. The seeded `RgbGain` 0x7B
delivers **16 of the 51**, so everything the source puts between 204 and 255
lands within sixteen levels.

**`ypbpr` IS WORSE, AND IT IS THE SEED THAT SHIPS.** The Wii's menu at 480p
into 1920x1080, the same sweep, scored on the share of the frame pinned at the
ceiling:

| gain | white p99.9 | frame at or above 234 |
|---|---|---|
| **0x33** (`ComponentGain`) | 237.9 | **52.08%** |
| 0x50 | 237.1 | 12.30% |
| 0x5C | 236.9 | 2.42% |
| 0x64 | 235.5 | 1.08% |
| **0x68** | 234.1 | **0.12%** |
| 0x70 | 228.0 | 0.01% |

**More than half the frame is pinned at one level** at the value in force.
0x68 is the knee by the same rule. What it is worth on the picture: the Wii
menu's disc icon against its tile went from **23.8 levels of contrast to 62.8**,
and the tile texture, the Mii faces and the channel lettering come back.

**THE EARLIER READING THAT THE REGISTER DOES NOTHING ON RGB IS REFUTED, AND THE
REASON IS THE INSTRUMENT.** `investigations/the-adc-gain-is-the-colour-spaces.md`
swept it on RGB and saw the mean move 0.6 of a grey level across a threefold
range, concluding the stage was not in that path. Those are AGGREGATE
statistics over a whole test card -- a mean, a percentile and a clipped
fraction -- and a knee near white moves none of them, because the card's large
flat areas dominate every one. **A calibrated ramp sees what a histogram
cannot**: on the same path the top step goes 16.1 -> 50.0 across that sweep.
Do not score a transfer curve on summary statistics.

### FIXED: auto gain never moved the seeds, and a field's width is why

`auto-gain` read 1 while `ADC_RGCTRL`/`GGCTRL`/`BGCTRL` sat at exactly their
colour space's seed for the life of a boot. The loop ran -- every gate in
`loop()` opened, continuously -- and it was reading a signal that carries no
level.

**The cause is a field declared one bit wider than upstream's.**
`DEC_TEST_SEL` is `s5_1F[6:4]` upstream with `DEC_TEST_ENABLE` at `[3]`; the
fork made it `[6:3]`, matching RD-5725-1.1's own four-bit description, which
makes the two names OVERLAP. Every call site asking for a logic by number then
writes the enable with it: `write(1)` selects logic 0 rather than 1, and
`write(2)` switches the measurement path OFF. Measured at `TEST_BUS_SEL` 0x0b on
a clipping picture, logic 0 returns `0 32 64 ... 224` whatever the gain and
logic 1 returns a level that follows it.

**It hit the ADC offset calibration too**, which writes 1, 2, 3 to walk green,
red and blue: the `2` cleared the enable, so red searched against a dead bus.
Both fields are split again, `[6:4]` and `[3]`.

`Tv5725::AutoGain` reads `TEST_BUS_SEL` **14, 15 and 16** rather than the
decimator path -- one per channel, ten bits, no flag bit, where the decimator's
own byte carries a seven-bit level under a flag that `== 0x7f` only matches when
it happens to be clear. `investigations/the-auto-gain-loop-read-a-selector-with-no-level.md`
is the whole account, including how to tell a gate fault from a measurement
fault in one HTTP read.

**What it is worth, measured on both inputs.** On `vga` at 320x256@50 with
PM5544, whose greyscale staircase draws six known levels 51 apart:

| gain | steps | top gap |
|---|---|---|
| 123, the shipped `RgbGain` | 25, 60, 111, 164, **236** | the top two have merged |
| 199, the knee itself | 24, 49, 89, 132, 177, 226 | 49 |
| 211, where the walk settles | 23, 47, 86, 128, 172, 219 | 47 |

At the seed only five plateaus survive -- 204 and 255 land on each other. On
`ypbpr`, the Wii's menu at 480p: **14.0% of the frame at one level near the top
at the shipped 0x33 against 4.2% at the 87 the walk settles on**, and the
busiest level drops from 237 to 204.

**The walk settles on evidence and then looks rarely, rather than stopping.** A
pass with nothing bright on it says only that nothing bright was on screen, so
settling needs a run of passes that reached `EvidenceLevel` AND asked for no
step -- a dark picture never settles it. Settled, it looks every `SettledMs`:
measured, a hand-written gain of 64 under a settled walk is corrected to 211
within 15 s. Stopping outright was tried and is wrong, because the level is
established against whatever was on screen at the time.

**A pass costs tens of milliseconds of the loop**, so the interval is what makes
it affordable -- against a build that looked at its stepping rate for ever, the
frame time lock reported `not armed: something keeps disturbing the lock` on a
locked source.

**The seeds stay hot on purpose.** The walk only ever backs off, because a level
below the band says nothing about how much headroom is left. A seed that did not
clip would strand a hotter source, so a seed is a starting point and the walk is
what makes it one.


### FIXED: the clamp kept an inverted-SOG offset onto active picture

Reached on `ypbpr`, the Wii at 480p into 1920x1080, by a pass-through round trip
and back to the scaling path. Read by name while the fault was live:

| field | read | what the arithmetic wants |
|---|---|---|
| `SP_CS_CLP_ST` | **78** | 19 |
| `SP_CS_CLP_SP` | **82** | 23 |
| `SP_SOG_P_INV` | **0** | -- |
| `SP_SOG_MODE` / `ADC_SOGEN` | 1 / 1 | the csync component path |
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 1212 / 1212 | latched, so the divider is not the fault |

`SyncProcessor::clampStartFor()` on that path takes `ClampStartCsyncComponent`
0.089 of the quarter-count line, which at 31468 Hz is 213 -- so 19 and 23. The
registers are **59 counts later**, and 59 is what
`clampOffsetForInvertedSog()` returns for a sync duty of 6.9% at this line rate.
**`SP_SOG_P_INV` reads 0**, so the inversion that offset exists for is not in
circuit and the offset should not be there.

**THE OFFSET PUTS THE CLAMP ON ACTIVE PICTURE.** The usable range measured on
this exact source and divider is 0..66, with picture from 67, at 2.667 emitted
columns per count -- so 78 is about eleven counts, thirty columns, inside the
picture. That is the documented clamp fault and it has two symptoms, both
reported here: the black bar at the left, and the black level coming off
picture so **the whole frame changes with its content**, which in a game reads
as the colours shifting as the scene does.
`investigations/the-clamp-had-two-owners-and-two-units.md`.

**THE COMPONENT COLOUR REST IS NOT THE CANDIDATE FOR THAT.** `VDS_Y_OFST` 14,
`VDS_U_OFST` 3 and `VDS_V_OFST` 4 read exactly `ColourBalance`'s `Component`
resting row, which is the intended pedestal compensation and not a leftover.
**A colour reading taken while the clamp is on picture is not evidence about
it** -- the black level it is compensating is itself wrong -- so the balance is
judged again once the clamp is back in the back porch, not before.

**THE WRITER IS `SyncProcessor::init()`, WHICH WROTE THE REGISTER WITHOUT THE
FACT.** `sogReferenceInverted()` is the one owner of whether the inversion is
in force, held rather than read back; `init()` cleared `SP_SOG_P_INV` directly
and left the fact true. `configureScalingPath()` runs `BringUp::init()` without
`applyForSyncType()`, so leaving pass-through opens a window where the two
disagree, and a clamp placed in it takes an offset for an inversion the
hardware no longer has. 78 is 19 plus exactly that offset, which is reachable
no other way.

`init()` goes through `invertSogReference(false)` now, so the register and the
fact cannot part. Guarded by `the static setup clears the held SOG inversion
with the register` in `test/test_sync_processor.cpp`.

**Writing 19/23 by hand held for 30 s with nothing rewriting it**, which is
what says the placement runs once per acquisition rather than continuously --
so the stale window only has to be open at that one moment.

**MEASURED FIXED, ON THE ROUND TRIP THAT PRODUCED IT.** Wii 480p on `ypbpr`,
`/sc?k` into pass-through and `/sc?~` back, sampled every ten seconds for 80 s
afterwards:

| stage | `SP_CS_CLP_ST`..`SP` | `SP_SOG_P_INV` | `DAC_RGBS_BYPS2DAC`/`OUT_SYNC_SEL` |
|---|---|---|---|
| scaling, before | 19..23 | 0 | 0/0 |
| pass-through | **78..82** | **1** | 1/1 |
| scaling, +10 to +80 s | 19..23 | 0 | 0/0 |

**78 is CORRECT in pass-through**, where the inversion really is in force, and
that is what names the fault precisely: the value was never wrong on its own,
only the pairing of it with `SP_SOG_P_INV` 0 surviving the return. The two now
move together at every stage.

The path registers are what say the leg was taken -- an earlier attempt used
`/uc?k`, which is `resetAvPicture()`, and the clamp held at 19 while the unit
never left the scaling path, which proves nothing. Bypass is `/sc?k`, and
`DAC_RGBS_BYPS2DAC` and `OUT_SYNC_SEL` reading 1/1 is what confirms it.
`docs/rgbhv-bypass-trap.md`.

Still open: the colour balance has not been re-judged with the clamp back in
the back porch.


### The reference clock list can spin until low power, and nothing measures through any entry

Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1`, output 1920x1080,
`scale-rgbhv = 0`, on an input selection followed by `/sc?~`. Detection found
sync in 6.4 s, and from there the console carried **209 `measure: the reference
clock measured nothing, trying the next` lines in 68 s**, each with its own
`settle: spent after 100 ms` and nothing else at all, ending in `LOWPOWER:
entered (no sync found)`.

```
  72.32  DETECT: 6387ms, syncFound 1
  72.72  measure: the reference clock measured nothing, trying the next
  72.72  settle: spent after 100 ms
  ... 207 more, 0.2 to 0.4 s apart ...
 140.64  LOWPOWER: entered at t=426562ms (no sync found)
```

So the source is pulsing, detection sees it, and no entry in the reference list
measures a rate through it. The list exists because no entry latches on both
bench sources; what this state says is that for one source and one arrangement
none of the four does.

**TWO SPINS HAVE NOW BEEN READ AT THE REGISTER, AND THEY ARE NOT THE SAME
STATE.** Both are `vga` at 800x600@60 with `SYNC 1`, output 1920x1080:

| | `SP_SOG_MODE` | `VTOTAL` | `PLLAD_MD` | `HTOTAL` | `SP_H_PULSE_IGNOR` |
|---|---|---|---|---|---|
| after two `/sc?~` round trips, scaled then passed through | **0** | **0** in 10 readings over 50 s | 1446 / 2040 | 988..1978 | 16 |
| `test_acquires_every_source.py`, `composite-sync-passed-through` | 1 | 623 | 2040 | 963..991 | -- |

So one spins with the arrangement WRONG and no vertical count to measure, and
the other spins with the arrangement right, the count right, and the reference
clock still in force. **The threshold is not either one's cause**: it reads 16
throughout the first, which is the value sized for a block counting at the
reference.

**THE FIRST SPIN IS FIXED, AND IT HAD TWO ROUTES IN.** Both were caught on
the console at 320x256@50 on `vga`, and both are closed by host tests.

The first route is the chain below, reached by `/sc?~` on a source whose type
the engine already held as composite: the probe answers composite, no `sync
arrangement:` line follows, and the chip stays on the separate arrangement
with `SP_SOG_MODE` 0 and `SP_H_PULSE_IGNOR` 16 while `isCsync()` answers true
-- which also gates off the arm that re-probes a source driving no V. A type
the probe has just measured is now put in force whatever the engine's record
says; the record only short-circuits passes on which nothing was re-measured.
Measured after: `/sc?~` with composite in force acquires and presents in 15 s.

The second route needs no `/sc?~`: a `SYNC 1` on a source held as separate.
The mode re-apply moves the count for a moment, the count arm opens a
measurement, and the arm that would re-probe the type was asked only between
mode changes -- so the measurement cycled the reference clocks through the
separate arrangement for the whole of a 75 s window and the ladder never ran.
The same question is asked on every failed reading inside the measurement
now, and the same change logs `measure: no source V sync, re-probing the sync
type` and is acquired on the composite arrangement 4.1 s after the request.

**The chain as it stood, for the record.** `/sc?~` reaches
`SyncMeasurement::forget()` directly rather than `VideoPath::forgetSyncType()`,
so `csync_` goes false while `syncTypeApplied_` and `syncTypeInForce_` keep
the previous answer. Then:

1. `establishSyncType()` finds nothing measured and probes.
2. `SyncMeasurement::hasOwnVsync()` measures in the separate arrangement and
   restores `applyForSyncType(isCsync(), ...)` -- `isCsync()` being the value
   held *before* the probe assigns its answer, which the forget made false. The
   chip is left on `SP_SOG_MODE` 0.
3. `probe()` then sets `csync_` true, and `applySyncType(true)` returns early
   because `syncTypeApplied_` is true and `syncTypeInForce_` is already true.

The console shows it: `measured: own V sync no, composite or SOG` with **no
`sync arrangement:` line after it**, so the engine believes the arrangement it
measured is already in force while the chip is on the other one.
`establishSyncType()`'s own comment states the rule that breaks -- one record of
whether the type is known, and it is `SyncMeasurement`'s -- and
`applySyncType()` keeps a second one of what is in FORCE.
`investigations/sp-sog-mode-had-two-owners.md` is the same field with the
arrangements the other way round.

**And a forgotten sync type also decides which test-bus signal the RATE is
measured from.** `TestBusRateMeasurement::sourceFieldRateHz()` and `pllRateHz()`
both branch on `isCsync()`, and the separate branch watches vertical sync
activity, which on a composite source is not there to see -- so a composite
source whose type has been forgotten cannot have its rate measured through any
reference entry. That is the shape the second spin would need, and what it holds
has not been read: nothing on the board reports `isCsync()`.

**THE SECOND SPIN IS FIXED, AND IT WAS NOT INTERMITTENT.** It is the unlocked
state with no arm: the reference divider in force, the count plausible, held and
equal to the one the last solve measured, so every arm in `sourceMoved()`
declined while only a solve could rewrite the divider. It reproduces in ONE
request -- selecting the input already in force, 8 of 8 on composite sync
against 0 of 8 on separate -- and `VideoSourceAcquisition::UnlockedArmPasses`
arms a re-solve through it, 0 of 8 after.
`investigations/the-unlocked-state-had-no-arm.md`.

**What made it look intermittent was the check, not the fault.** An acquisition
wait that starts at the `/input` request answers about the state BEFORE it,
because the selection is queued for `loop()` and the engine reports the previous
solve until it has been consumed: the same eight trials read 5 of 6 acquiring
without a hold check and 0 of 8 with one. Seven narrower reproductions had each
passed for that reason, and the budgeted measurement -- several full six-case
runs per build -- was never needed.

**The first spin is fixed too**, above: `SP_SOG_MODE` 0 with no vertical
count is the chain, reached by either route.

### `apply-aspect` lags the engine by one toggle

Measured on `vga` at 640x480@60 into 1080p, reading `/geometry` and the emitted
frame together against the preference:

| | `apply-aspect` | `/geometry` `shaping` | emitted side borders |
|---|---|---|---|
| as found | 0 | **true** | 242 / 251 |
| after one `/uc?S` | **0** | false | 2 / 16 |
| after a second | 1 | true | 242 / 251 |

So the persisted byte reports the state BEFORE the toggle, and a tool that reads
it to decide whether to toggle moves the engine the wrong way. **`/geometry`'s
`shaping` is the witness**; `shaped` is not, staying true across both states.

`/uc?S` does `geometry.setApplyAspect(!geometry.appliesAspect())` and then
mirrors it into `uopt->applyAspect`, which should agree -- so what is wrong is
between that and what `/preferences.txt` serves. Whether the engine's state is
correct across a reboot is unmeasured, and is the thing that matters.

### 800x600@60's near horizontal border never reaches the emitted frame

`card_edges` finds no green run in the near quarter of the frame on this mode,
on BOTH sync types, so the surviving fraction of the source's outermost pixel
reads 0.00 across while the far edge reads 0.70 to 1.00.

**It is not the fit and not this axis's rounding.** Measured A/B with
`fitToRaster()` rounding to nearest against rounding up, two runs each: the near
reading is 0.00 on every one of the four, and 800x600@60's VERTICAL solve is
byte-identical across the pair -- `VDS_VSCALE` 569 with the aperture 41..1121 --
while reading 0.50 to 0.54 at one end throughout. A deficit that survives an
unchanged register set is downstream of the VDS.

Measured at 1080p with the shape off and the framing reset, `PLLAD_MD` 1342,
which is the line delay's ceiling rather than the output's.

Where to look: the shown window, which is latched when the link acquires and
takes its origin from something no output register describes. 320x256@50 on the
same runs keeps its near edge and loses the far one, so the two modes lose
OPPOSITE ends -- which a single displacement of the sink's window would do.
`docs/investigations/the-shown-window-is-latched-at-lock.md`,
`docs/investigations/the-sinks-own-window-position-is-per-raster.md`.

### What acceptance means on the VGA sources

Stated here because several of the entries below are measured against it and a
criterion carried in one session's head is the reason the same screen is fixed
twice. On `vga`, with `PATTERN CARD` up and the animation off:

1. **The green border on all four emitted edges**, with no black between it and
   the frame's edge.
2. **A stable acquisition** -- no shake, in any band of the picture.
3. **The same framing on every acquisition** of the same source, both in what
   the engine solves and in what reaches the screen.
4. **No difference between composite and separate sync.** One source, one
   cable, one raster.

**At 1080p, with shaping OFF.** Lower output resolutions have their own open
entries and are out of scope. The shape is turned off rather than set to 16:9:
a declared shape narrows the room, the bars it pillarboxes with are the board's
own blanking, and an edge read at the frame's edge then measures them instead of
the picture. `Aspect::Fill` is the absence of a shape, so the picture takes the
whole raster whatever the user has stored -- which cycling `/uc?G` cannot
promise, the stored shape being per source. `/uc?S` is the toggle and
`apply-aspect` is how it reads back.

**THE MODE MATRIX IS COVERED NOW, AND IT SAYS THE SCREEN EVERYTHING WAS
MEASURED ON IS THE EXCEPTION.** `test_card_framing.py` asks the first
criterion on five modes against both sync types, at 1080p with the shape off,
and `test_acquires_every_source.py` asks all four on 800x600@60.

Measured over two full runs of the matrix, no case is flush on both axes and
**the two sync types agree on 800x600@60 and 1600x600@60 and nowhere else**:

| mode | separate | composite |
|---|---|---|
| 240x352@70 | one horizontal edge off, 2 rows at the top | one edge off on both axes |
| 320x256@50 | 2 at the near edge on both axes | one horizontal edge off, vertically flush |
| 640x480@60 | one horizontal edge off, vertically flush | **neither edge on either axis** |
| 640x512@50 | one horizontal edge off, vertically flush | 2 at the near edge, and **29 rows at the top** |
| 800x600@60 | 2 at the near edge, vertically flush | 2 at the near edge, vertically flush |
| 1600x600@60 | one horizontal edge off, vertically flush | one horizontal edge off, vertically flush |

800x600@60 is the mode every other measurement here has been taken on, which is
what the matrix exists to catch. The table is the suite's standing record, so a
case passing is the gap having closed and a case not in it failing is a
regression.

**It is the state BEFORE the polarity correction followed the sync path**, and
1600x600@60 has since left the default set: its solve is 800x600@60's, one
`SourceKey` serving both, so its reading measures the clash rather than the
framing.

Two things the verdicts do not say, both of which bound what can be concluded
from the table:

- **A near margin of 2 fails by one pixel against `card_edges.FLUSH`**, and the
  sink's own window placement is in that margin -- latched when the link
  acquires and held until it acquires again. **It is NOT taken from
  `VDS_DIS_HB_SP`**, which is refuted below, and the encoder tuning the near
  edge to its content is pass-through only; what it keys on is not established.
  Whether 1 is the right allowance is an acceptance threshold rather than a
  measurement, so it has not been widened to suit a reading.
- **`one edge` carries no magnitude.** Green is found at one end only, so there
  is no near margin to report and a one-pixel shift reads the same as a
  hundred-pixel one. With the shape off the picture fills the raster exactly,
  so rounding at either end puts the outermost green column off the frame --
  which is why one case reads `one edge` in one run and `2 at the near edge` in
  the next.

**AND ONE STUCK CASE USED TO POISON EVERY CASE AFTER IT.** The first run of the
matrix reported five consecutive composite cases as `never acquired` without
judging any of them, which reads as five broken modes and was one state nothing
cleared: `/sc?~` does not clear it where an input bounce does. The suite
recovers per case now, which also cut the run from 14:25 to 6:48.

### 320x256 on composite sync flickers, and entering it can fail to acquire at all

Seen on the bench, `vga` at 320x256@50 with `SYNC 1` into 1080p. What flickers,
and whether it is the same event as the top-band shake below, are open.

**ARRIVING AT THAT MODE ON COMPOSITE CAN MISS ENTIRELY, AND A MODE CHANGE DOES
NOT RECOVER IT.** Measured from a fresh boot, separate sync first: 320x256@50,
800x600@60 and back each solved in 3 to 5 seconds, counts 311, 627, 311 and
dividers 2200, 1342, 2200. Switching the source to `SYNC 1` at the same mode
then held `state: absent` for the whole 60-second budget with
`STATUS_SYNC_PROC_VTOTAL` **1323** -- outside the 200..1300 a count can be a
source at all -- and `PLLAD_MD` 1446, which is the ceiling for a 60 Hz source
rather than anything a 15.6 kHz line wants. Two further mode changes did not
clear it, the second landing on 1667 and the reference divider.

**`/sc?~` clears it at once**, and the mode is then correct on composite: count
308, the frame restored to 312, `cv` 624 for the doubled line and `PLLAD_MD`
2200 -- the same picture the separate leg solves. So this is the stuck-divider
family and not the count.

It is a second mode showing composite-sync instability, which is what makes it
worth recording rather than one more observation of the 800x600 case.

**ENTERING IT IS FIXED, BY THREE ROUTES.** A `SYNC 1` on a source held as
separate re-probes from inside the measurement it opens, and a `/sc?~` on a
source already held as composite puts the measured type in force rather than
trusting its own record -- "The reference clock list can spin until low
power" has both measurements. The third is the mode change itself, and it is
the one this entry was written on: returning from 800x600@60 to 320x256@50 on
composite, the sync processor counted **160** through the 1342 divider the
previous solve left while the formatter read its own frame of 623
half-lines, so every pass was dropped as a count the formatter contradicts
and the 800x600 solve stood, `state: acquired`, `present`, for as long as the
unit was left -- with nothing on the console but the frame time lock's
`no INPUT vsync`. The same shape with the count at **0** is the silent
post-detection state above. A contradicted run of
`VideoSourceAcquisition::ContradictedArmPasses` arms a re-measure now, and
the same round trip solves 320x256 in 2.3 s. What flickers once it is in is
the roll, below.

**AND `/sc?~` DOES NOT ALWAYS CLEAR IT.** Measured on 800x600@60 with `SYNC 1`,
arriving from a settled separate-sync state: the mode was set, `/sc?~` issued,
and the engine held `state: absent` for 95 s with `STATUS_SYNC_PROC_VTOTAL`
**0** and `PLLAD_MD` **2040**, which is the detection sweep's value rather than
anything the mode wants. Entering pass-through and leaving it again did not
move it; the source came back at once on `SYNC 0`.

So the family has two signatures and they want telling apart: a count OUTSIDE
the plausible band with a stuck divider, which `/sc?~` clears, and a count of
**0** with the divider on the sweep's value, which it does not. The second is
the sync processor not counting at all rather than counting the wrong thing.

**The same mode acquired on composite earlier in the same session** at
`PLLAD_MD` 1342 with a framing verdict taken off it, so this is not a mode that
cannot work on composite -- it is one that sometimes does not start.

### One source on one cable emits two different pictures, one per sync type

**THE COUNT IS NO LONGER THE DIFFERENCE, AND THE REGISTER IS NOT THE THING TO
ASK.** `STATUS_SYNC_PROC_VTOTAL` cannot agree across the two sync types: the
RISC PC's composite sync is a NOR, so the hsync pulses inside the vertical
pulse are absent from the wire and the counter is correctly short by the
vertical sync width -- 3, 4 and 6 lines on three modes whose DMT widths are 3,
4 and 6. What has to agree is the frame the ENGINE holds, restored from the
input formatter's own measurement.
`investigations/the-risc-pc-composite-sync-is-not-serrated.md`.

Measured at 800x600@60 into 1080p with the framing reset, both legs now solve
the same source:

| | separate `SYNC 0` | composite `SYNC 1` |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 627 | 623 -- the pulses are not on the wire |
| frame the engine holds | 628 | 628 |
| line rate | 37879..37896 Hz | 37879 Hz |
| `PLLAD_MD` | 1342 | 1342 |
| capture `ch` x `cv` | 1343 x 628 | 1343 x 628 |
| framing `oh`/`eh`/`ov`/`ev` | 275/1017/27/600 | 275/1017/27/600 |
| `VDS_HSCALE` / `VDS_VSCALE` | 1000 / 569 | 1000 / 569 |
| `IF_HB_SP2`/`ST2` | 274..1293 | 177..1196 |

The one register still apart is the capture window's position, and it is the
engine's own compensation rather than a difference in the solve:
`VideoSourceLine::SeparatorPulseEndLeadSamples` has the separator's output
reach the counter a whole sync width less 66 samples late, which is 96 here
against the 97 measured.

**WHAT IS STILL OPEN IS THE PICTURE.** With every number above agreeing,
the emitted frames still differ -- **14.1%, 36.5% and 17.0% of pixels** by more
than 16 levels on three runs, against 0.0 to 0.1% between two captures of one
state. **It varies per run, so it is not a fixed displacement**, which points
at what a single acquisition chooses rather than at the sync type: the sampling
phase search is the candidate and its band question is open below.

**The separation registers recover nothing**, which is the measurement that
says the pulses are absent rather than filtered. At 800x600@60 composite,
frozen, each written and read back with the count sampled eight times per
value: `SP_DLT_REG` reads an identical 623 at 48, 112, 256, 1024 and 4095 and
collapses the separator at 0 (190/212/97/100/112/205/152/97);
`SP_H_PULSE_IGNOR` reads 623 at 0, 16, 32 and 128 and collapses at 255;
`SP_DIS_SUB_COAST`, `SP_NO_COAST_REG`, `SP_COAST_INV_REG`, `SP_JITTER_SYNC` and
`SP_H_PROTECT` each move it not at all. The coast lengths were walked
separately and read 623 at every pre coast from 2 to 18 and post coast from 2
to 20.

Guarded by `test_one_source_counts_the_same_on_both_sync_types`, which passes,
and `test_one_source_looks_the_same_on_both_sync_types`, which does not.

### The top band shakes on composite sync, about one acquisition in five

Measured off the USB capture as the horizontal displacement of each row band
across 60 frames, `vga` at 800x600@60 into 1080p at 16:9. When it happens the
top band moves by **sd 2.1 to 4.0 columns, p2p 4.4 to 11.5**, and every band
below 135 rows reads 0.004 to 0.022 -- so it is confined to roughly the first
90 to 135 output rows, which is where the lines just after the vertical interval
land.

It is a property of the ACQUISITION, not of a register: repeated solves at the
default coast gave 1 of 6 shaking, and at `SP_PRE_COAST` 2 gave 1 of 4. Inside
one shaking acquisition, writing the pre coast down to 2 by hand quiets it
(0.006 against 2.573 at 7) and the post coast does nothing across 2..20 -- so
the pre coast is a lever within the state rather than what decides entry to it.

Separate sync in the same arrangement reads 0.005 to 0.014 in every band.

**It is not the whole-picture shake**, which is vertical, affects every band
alike, and is a property of the boot. `investigations/the-unlocked-state-had-no-arm.md`.

### The composite picture jumps vertically, several times a second

Measured off the USB capture with
`tools/gbsc-pro-hwtest/picture_stability.py`, `vga` at 320x256@50 into 1080p,
as each frame's vertical offset against the profile the clip holds. The
picture is displaced in **17% to 53% of frames at 6.3 to 8.6 jumps a second**,
against a separate-sync floor of **0% -- every frame at offset 0, over six
runs in one session**. Both legs run in one session on one source mode, one
output and one capture, so the sync arrangement is the only thing differing.

**THE JUMPS ARE WHOLE SOURCE LINES.** 1080 output rows over 256 active source
lines is 4.22 rows a line, and the measured offsets are 2, 4 and 8 rows --
half a line, one line, two -- with occasional jumps of 107, 197, 214 and 216,
which are 25 to 51 lines. It rolls as a WHOLE: the top, middle and bottom
thirds correlate to one offset, so it is not a tear.

**IT IS NOT EITHER SHAKE ALREADY RECORDED HERE.** It is not the top-band shake
above, which is horizontal and confined to the first 90 to 135 rows. And it is
not the boot shake, which is a property of the BOOT and sub-pixel -- sd 0.126
to 0.288 of a row -- where this is whole lines and toggles with the sync type
six times inside one boot.

**WHAT IT LOOKS LIKE IS BLACK BARS, AND COUNTING THEM MEASURES THE WRONG
THING.** The rows the picture leaves behind go black, so the fault presents as
horizontal bars a few times a second -- but a band count scores only the jumps
big enough to black out a mid-picture row. In one clip of 60 frames, 5 carry a
band and 21 are displaced.

**THE COMPOSITE LEG USED TO COME BACK WITH NO PICTURE ABOUT HALF THE TIME**
after the `/sc?~` re-probe a sync-type change needs -- one leg read 482 of 482
frames with nothing to correlate. That was the acquisition family above, both
routes of which are fixed under "The reference clock list can spin until low
power", and it is why the judge voids a reading with no picture in it instead
of passing it: a black capture is displaced in 0% of frames and reads as the
steadiest picture there has ever been.

`test_picture_holds_still.py` asserts it. **The composite leg is RED on
purpose** -- not an xfail, the defect having gone unmeasured for want of
anything failing.

### A selection edge is swallowed in the pass-through cold state, so the sync type is never established

Measured on `ypbpr`, the Wii at 480p, output 1920x1080, reached by leaving a
`vga` pass-through run part way and then selecting `ypbpr`. The engine holds
`state: absent` for as long as it is left, with no `measured:` line at all over
60 s and only three `settle: spent after ~100 ms`:

```
state=absent  md=2040  ht=850  vt=97   ign=255  sog=1
state=absent  md=1446  ht=427  vt=100  ign=255  sog=1
```

`ht` is the 27 MHz reference count for the source's line rather than the
divider, so the clock is not latched and nothing is measured. **The `sync
arrangement:` line is missing from the selection**, which a selection that
establishes the type prints.

**The edge is dropped before anything can act on it.** `sourceMoved()` returns
early where `solvedLines_ == 0` and the output is passed through -- the one
cold state with no way forward, since bypass has no raster to solve -- and
`selectionMoved()` sits inside that early return. So the selection arms no mode
change, `runPass()` reaches `establishSyncType()` only while one is pending, and
the type stays as `LoadDefault()` left it: forgotten. Nothing probes, and
nothing says so.

**Selecting the other input is what recovers it**, not `/sc?~`: `vga` acquires
in about 10 s and `ypbpr` then acquires in 3.1 s. So it is a stuck state
reached by leaving an excursion part way rather than one a selection walks
into, and it does not reproduce on demand -- composite-sync `vga` in
pass-through acquired within 25 s on one attempt, which left `solvedLines_`
non-zero and took the edge normally.

**WHAT MADE IT UNRECOVERABLE IS FIXED SEPARATELY.** A forgotten sync type read
as separate sync, and the separation threshold keyed on that read wrote 0xff --
every pulse ignored -- onto a sync-on-green source, which then could not be
counted at all, so the type was never established either. The threshold is a
fraction of the line for every source now and asks nothing about the
arrangement, so a forgotten type cannot reach it.
`investigations/the-pulse-ignore-is-a-fraction-of-the-line.md`.

**`isCsync()` STILL ANSWERS FOR TWO STATES**, and one more consumer keys on it
while a source is being searched for: `applyDynamic()` writes the search's
coast inversion only under `if (source.csync)`, so a forgotten type skips it.
`isSet()` is the discriminator and nothing on that path asks it. Nothing is
measured about what that costs -- the threshold alone was enough to hold the
source absent, and one register written by hand was enough to release it.

### The composite count flapped the presence verdict -- FIXED

The verdict was derived from ONE reading where earning presence takes a run:
`plausible && held` false made `sourceState_` `SourceAbsent` and the next
reading took it back, so `/geometry` alternated every ~1.6 s with the divider
latched throughout. Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1`
passed through:

```
 22.43  source absent: 557 lines, 2038 samples against divider 2038
 22.43  source acquired: 626 lines, 2038 samples against divider 2038
 24.03  source absent: 175 lines, 2038 samples against divider 2038
 24.03  source acquired: 626 lines, 2038 samples against divider 2038
```

**WHAT IT COST IS THE UNBROKEN ACQUIRED RUN, not the verdict.** The auto gain
needs `acquiredPasses()` past 90, which is 1.82 s at the 20 ms pass, and a flap
at 1.6 s zeroes the run at about 80 -- so it never ran at all on a composite
source while the picture was perfect. The frame time lock arms on
`FrameTimeLock::ArmPasses` and holds past `HeldPasses`, so it was interrupted
for 400 ms out of every 1.6 s.

`VideoSourceAcquisition::AbsentPasses` is what the verdict waits now, and it is
derived rather than chosen: one scattered count costs the steadiness run its
whole length, so a shorter run cannot survive a single miscount.
`UnsettledArmPasses` is the same figure for the same reason, the arm that
question raises rather than the verdict it reaches. `countContradicted()` does
not reach these readings -- 175, 453 and 557 lines all pass it -- and the run is
what tells a miscount from a source leaving: a miscount does not persist.

Measured on the same arrangement passed through to 1920x1080, `/geometry` polled
at about 58 Hz against the 3.6 Hz of the reading before it:

| | before | after |
|---|---|---|
| console `source absent` transitions | every ~1.6 s | **0 in 240 s** |
| readings acquired | 788 of 865, 91.1% | **12810 of 13838, 92.6%** |
| longest unacquired run | 4.9 s | **3.5 s** |

**WHAT REMAINS IS NOT THE VERDICT.** The 13 excursions left in that window are
`measureSource()`'s own absent verdict on a measurement that failed, which takes
no run and should not -- a measurement that failed is a stronger statement than
one count -- and 644 of the readings are `SourceUnlocked`, the divider not
latched under a count that reads the source. Both are the open pulse-ignore
floor question below rather than the flap.

It also exposed a second defect, fixed with it: the dynamic sync-processor
configuration is keyed on the sync type as well as on the search, and the latch
held only the search answer, so the pass that falls back into searching -- which
is the pass that forgets the sync type -- recorded the configuration as applied
with its csync half skipped.
`investigations/the-search-coast-inversion-outlived-the-search.md`.

`investigations/the-composite-sync-count-re-arms-its-own-solve.md`,
`investigations/the-engine-makes-the-composite-count-unmeasurable.md`.

### Composite-sync VGA in pass-through acquires and emits nothing

Measured 2026-10-14 on `vga`, RISC PC at 800x600@60 with `SYNC 1`, output
1920x1080, `scale-rgbhv = 0` so pass-through is permitted. The engine acquires
and claims the output -- `state: acquired`, `present: true`, line rate 37638
against the source's 37919, `DAC_RGBS_BYPS2DAC` 1 and `OUT_SYNC_SEL` 1 -- and
the capture dongle reads **mean luma 0.00 with no picture at all**. The same
source on `SYNC 0` through the same route is a full 1920x1079 frame at mean
luma 82.2.

**It is the only one of the three sync arrangements never tested on this
route.** Separate-sync VGA and the Wii's sync on green both pass through; this
combination had no reading until it was asked for.

`SP_SOG_MODE` reads 1, so the engine is on the csync path, which is correct for
the source. `SP_CS_CLP_ST`/`SP` read 6..10 against 21..25 on separate sync, and
`SP_SOG_P_INV` reads 0 -- so the sync-on-green inversion is not implicated and
the clamp is the one window that moved.

**Reaching it costs a dark screen for a minute**, because `SYNC` arms no
re-probe: `/sc?~` is what makes the engine follow the sync type, and the
recovery is `SYNC 0` followed by another `/sc?~`. Script the restore in a
`finally` so a failed run cannot leave the bench dark.

**THE ENCODER AND THE COAST ARE BOTH REFUTED, AND THE SEPARATOR LEVEL IS THE
LEVER FOR THE COUNT.** Measured at 800x600@60 into 1920x1080:

- `PAD_SYNC_OUT_ENZ` is not stuck and toggling it does not recover the picture.
  The pad is driven and withdrawn **six times in 80 s**, paired, because the
  engine re-solves continuously and each solve takes the output away -- so a
  hand-written 1 reads back 0 within 2.5 s, and every black capture lands in a
  withdrawal window. `present` is therefore not a stable property of this
  state, which is why an earlier reading of it as `true` and this one as `false`
  are both right.
- **Eleven coast pairs, every one emitting nothing.** The pair does tighten the
  count -- 0/4 gives sd 28.1 against the default 7/6's 415.1 -- so it is not
  inert and it is not why nothing is emitted.
- **`ADC_SOGCTRL` 20 takes the count to sd 0.4**, a steady 623/624, where the
  24 the engine free-runs at scatters by 384 and 4, 8 and 13 by about 300. The
  level is the lever and it is **not monotonic**, so a walk without an
  acceptance test cannot find it.

**A steady count is still not a picture**: at level 20 the engine stays
`absent`, the count is four lines short of 627/628, and nothing is emitted. So
the level is necessary and something downstream of the count still refuses.

`investigations/the-separator-level-steadies-the-count-but-does-not-present.md`
carries the tables. The related row below -- the 640x480 one whose horizontal
half is fixed -- is the same defect at another mode, and its reading that 4 and
above steadies the count does **not** transfer: at 800x600 level 4 is among the
worst.

**WHAT DISCARDS THE SOLVE IS THE IDLE PASS'S COUNT, AND TWO CLASSES OF IT ARE
NOW REFUSED.** The source is measured right and solved right -- 15 complete
solves in 42 s -- and each is thrown away by a reading the solve never saw. A
count the input formatter's own frame contradicts is dropped, and a count that
is a whole multiple of the solved one while the line period has not moved no
longer arms. `present` has gone 1 and 2 polls of 60, to 6, to **13 of 60**.

**The residue is a third class and the tolerance must not be widened to reach
it.** The 28 count arms left in 141 s run from 0.340 to 1.901 times the solved
count -- a vertical found somewhere INSIDE a field rather than lost at its edge
-- and none is within 20 per thousand of a multiple.
`investigations/the-composite-sync-count-re-arms-its-own-solve.md`.

**AND WHAT MAKES THE SOURCE UNREADABLE IS `SP_H_PULSE_IGNOR`.** Frozen at a
solved state with the divider latched and nothing else written, 99 reads the
source's field rate in 25 of 25 and its count in 25 of 25, where 50, 16 and 14
read 36.99 Hz and a count in the hundreds or thousands -- both halves turning
together on one register, reversibly. `pulseIgnoreFor()` writes
`ProgressiveCsyncPulseIgnore`, a constant 14 measured on the Wii at 480p through
a 1446-sample line, to every progressive composite source, including this one
through a 2038-sample line.

**A single rule is not yet derivable and must not be guessed.** 70 is measured
BAD on the Wii, so the two sources bracket from opposite sides and neither a
fraction of the line nor of the pulse fits both. In absolute time the windows
overlap at roughly 0.7 to 1.4 us -- a fixed duration, which is neither
normalisation tried -- but that lands in the untested gap on both sources,
50..99 on the RISC PC and 14..70 on the Wii. Those two sweeps are the
outstanding measurement.
`investigations/the-engine-makes-the-composite-count-unmeasurable.md`.

### No walk reaches a separator level above the starting guess

`acquireSeparatorLevel()` runs `SyncOnGreen::acquire()`, which only **descends**
from the level the input is due -- so a source needing a level above
`ComponentLevel` cannot be found. A full-field walk that climbed existed with no
call site and is deleted rather than wired in, because the level is not a
reproducible lever: two consecutive acquisitions of one source in one mode read
624/624 and 1691/1691 at level 18, so no run's evidence supports a choice.

What is still open is a source that needs one. Nothing on the bench does, and
the measurement that would reopen this is a source acquiring above
`ComponentLevel` and reproducibly failing below it.
`investigations/the-separator-level-steadies-the-count-but-does-not-present.md`.

### A full-screen framing on the Wii shakes vertically, blanks, and re-acquires

Observed 2026-10-14 on `ypbpr`, Wii at 480p into 1920x1080, with the line filter
switched on. The framing was widened by hand to fill the screen; the picture
shook vertically, went black, re-acquired on its own and came back stable and
full screen. It has not been reproduced deliberately, and the two entries below
are the defects the investigation did find, each reproducible on its own.

**The reported event itself carries no instrument**, so what follows is measured
on the same source and output across a line-filter toggle rather than across the
event. The frame time lock is healthy there: `in 59939 mHz, out 59939 mHz` on
every correction, the display clock inside 8 Hz of 108021588, and a phase error
of 5236 against a 2669378 tick period, which is 0.2% and well inside the clamp.
That is the settling shape in
`investigations/the-frame-time-lock-saturates.md`, not the saturating one -- so
saturation is not what a line-filter change provokes, and whether it is what the
event was remains open.

**And `PAD_SYNC_OUT_ENZ` is never dropped across a line-filter change**, so
nothing re-presents the output. A black interval and a re-acquisition therefore
came from the sink re-locking to a timing that moved under it, not from the
engine taking the pad away and giving it back.

What is NOT established is what moved vertically. The vertical registers are
untouched by the defect below -- `VDS_VSCALE` 431, `VDS_VB_SP`/`ST` 34/1121 and
`VDS_VSYNC_RST` 1124 are identical either side of a line-filter toggle -- so a
horizontal-only mechanism does not reach it.

**The source identity is NOT what moved**, and that is measured rather than
assumed: ten acquisitions of this source give one key, and the raster is
generated from the key.
`investigations/the-wii-480p-source-key-is-stable.md`. So a stored framing
recalled against a key that drifted mid-change is refuted as the explanation,
and what is left is the vertical path itself.

Where to look: whether widening the framing past the line buffer re-solves the
capture mid-change, and what the playback fetch does while it does. A capture
re-solved under a picture the memory is still being read for is the one
mechanism left that reaches the vertical axis without touching a vertical
register.

### A framing wider than the line buffer drops the line filter and keeps its divider

`VideoPath::applyLineFilter()` writes the bypass from
`picture_.lineFilter() && fits`, where `fits` is the capture width against
`VideoProcessor::LineBufferSamples`, 1020. `VideoPath::dividerCeilingForLineFilter()`
reads only `picture_.lineFilter()`. **Past 1020 captured samples the two
disagree**: the one-line delay leaves circuit and the divider ceiling it imposed
stays.

Measured 2026-10-14, Wii 480p into 1920x1080, `line-filter = 1` on flash:

| line filter | `PLLAD_MD` | capture width | `VDS_D_RAM_BYPS` | `VDS_HSCALE` |
|---|---|---|---|---|
| on | 1212 | 966 | 0 | 708 |
| off | 1446 | 1153 | 1 | 845 |

At the solved framing the capture is 966 and fits. A framing widened past 1020
leaves `VDS_D_RAM_BYPS` 1 with the preference still 1 and the menu still reading
On, because `MenuTree` reads the preference rather than the register. **That is
the filter's cost without the filter**: 1212 kept samples a line where the
output alone would allow 1446, a sixth of the horizontal sampling density
surrendered for a delay that is out of circuit.

The setting itself persists correctly -- `line-filter = 1` is in
`/preferences.txt` and survives a boot. What moves is the register.

Where to look: the ceiling is computed from the published raster's active
fraction before the capture window is solved, so it cannot simply ask `fits`.
Either it follows the framing actually in force, or the capture is held to the
buffer while the delay is wanted, or a framing that cannot carry the delay says
so rather than silently dropping it.

### FIXED: the separation threshold on the pass-through channel was chosen by nothing

`SP_H_PULSE_IGNOR` on the channel was the interlaced fraction of whatever
divider was in force when the sync type was established, which bore no relation
to the source's scan. Entering the channel from the scaling path left the
scaling divider's fraction -- 59 on the Wii at 480p on `ypbpr` from a divider of
1212, harmless, and nothing picked it. **Booting straight into pass-through left
the CHANNEL's**, 99 from 2038, and that one emits nothing: measured mean luma
0.00 with `state: acquired`, a silent console, `STATUS_SYNC_PROC_VTOTAL` steady
at 524 and every register reading correct. 14 written by hand into the same
acquisition emitted a 1670x1028 picture.

**The scan is measured on every route now, and the threshold follows it from one
place.** `SourceMeasurement::measureScanType()` sat behind the channel's own
return in `maintainSource()`, so the scan was never measured there at all --
which is why letting `applyDynamic()` own the threshold had been tried and
refuted, the dynamic pass deriving the same fraction for itself. Measuring the
scan ahead of that return is what makes the one owner correct; the deinterlacer
steering beside it stays off the channel.

Measured after: a boot into pass-through lands on 14 by itself over five reads
and emits mean luma 163.45 across four identical frames, and the console prints
`scan: progressive, count 524` where it printed no scan line before. `vga` on
separate sync holds 255 through the channel and acquires in 6.5 s, and the
`ypbpr` scaling path converges to 14 at divider 1212 as it did.
`investigations/the-pulse-ignore-outlived-the-channels-entry.md`.

**A composite source on the RGBHV connector now takes the measured-scan value
where it kept the fraction.** That route is measured indifferent across the
whole range -- 107 and 2 both counting 623 on the RISC PC at 800x600 with
`SYNC 1` -- so nothing argues against it, and it is untested on this build,
ModeServ not having answered the session the change landed.

### The Wii's 480p does not acquire at 1920x1080 -- the field rate is FIXED

**The title named the field rate for two sessions and the field rate was the
engine's own estimator, not the separator.** Read the four entries that follow
this one for what is actually left -- the scan oscillation is FIXED, and what
remains is the reference clock's latch and a field rate that reads two thirds at
the solved clock. Everything below here is the measurement trail, kept because
four of its hypotheses are refuted and worth not repeating.

**480i acquires and 480p does not, on the same input, cable and sync
arrangement.** Measured 2026-10-11 on `ypbpr`, sync on green. `/geometry` sits at
`state: absent` with `lineRateHz` 29363 -- the right order for 480p's 31469 and
nowhere near 480i's 15734 -- so the engine sees the source and cannot converge on
it.

**THE FAILURE IS IN THE FIELD-RATE MEASUREMENT, UPSTREAM OF EVERYTHING ELSE.**
`/samplinglog?rates=` times the source's field rate off `DEBUG_IN_PIN` exactly as
`SourceMeasurement` does and runs from `loop()` rather than the acquisition tick,
so it answers with the engine out of the way. 60 readings:

```
rate,ms,sp_vtotal,field_rate_mhz
rate,5,98,0          <- 0 in the large majority of samples
rate,8238,0,15365    <- 15.4 Hz against a true 59.94
rate,8344,0,18543    <- 18.5 Hz
rate,8710,100,20146  <- 20.1 Hz
```

15365, 18543 and 20146 mHz are 59.94 divided by 4, 3 and 3. **A whole multiple of
the frame period is the signature of MISSED EDGES**, the same shape the frame time
lock's second owner had -- `investigations/the-frame-time-lock-saturates.md`. So
the vertical sync is not being caught reliably at the pin on this source, and
every downstream symptom follows: `line rate 0` is the reading `rateFollowsCount()`
rejected, no divider can be sized from a rejected rate, the ADC PLL free-runs
(`STATUS_MISC_PLLAD_LOCK` 0, `STATUS_SYNC_PROC_HTOTAL` 413 against a 2040
divider), and `STATUS_SYNC_PROC_VTOTAL` never settles -- 97, 98, 100, 128, 222,
243 across one session.

**`VPERIOD_IF` IS NON-ZERO THROUGHOUT**, so the input formatter counts the
source's vertical while the sync processor has nothing coherent to count. That
split is the same one the free-running-PLL entry below records, and it is what
says the signal is present rather than absent.

**Four hypotheses are REFUTED, and none of them should be reached for again:**

| refuted | by |
|---|---|
| a stranded held rate | `/restart` holds no rate at all and still reads `243 lines x 0.00 Hz` |
| the separator level | `ADC_SOGCTRL` is 13, not the parked 0, and 480i locks at the same level |
| the sync type or SOG configuration | `SP_SOG_MODE` 1 and `ADC_SOGEN` 1 are correct for component, and 480i acquires with both identical |
| the HC32's routing, or the Wii | 480i acquires on the same input and cable, and `vga` acquires fully with the frame time lock converging |

`/sc?~` and `/restart` both fail to recover it, which separates it from the stuck
divider and from the sync-type latch.

**A TESTBUS SWEEP CANNOT BE READ AS SIGNAL ABSENT HERE.** Selectors 14, 15 and 16
drop from ~3500 transitions per 25 ms on a clean ypbpr state to 0, which reads as
a dead input and is not one -- they are pipeline-internal and go quiet whenever
the part is unlocked. With `sp=4` out, selector 10 carries 98 transitions in
25 ms, about 2 kHz, which is neither the field nor the line rate.

**IT IS NOT A REGRESSION FROM THE RATE CORROBORATION, and that is measured.**
`SourceMeasurement` and `VideoSourceAcquisition` reverted to `c145e0d6a~1` --
before the corroboration commits, with everything else held -- fails too, and
fails more loudly: the count reads a correct **524 lines** while the field rate
scatters over 67.15, 70.00, 72.41, 77.85, 81.79, 81.89, 95.97, 97.16, 97.85,
99.29 and 112.60 Hz, touching the true 59.93 twice. It then acts on the garbage,
installing dividers of 1322, 1700, 1254 and 1920 from line rates of 52128, 26582,
32827 and 13965, and still ends at `state: absent`.

**So the corroboration is not the fault and reverting it is not the fix** -- it
rejects the bad readings where the older code acted on them. **The count is not
the fault either**: the sync processor counts 524 correctly on that build, so the
part can count this source.

**WHAT IS LEFT IS THE VSYNC EDGE ITSELF, AND IT BOUNCES RATHER THAN MERELY GOING
MISSING.** A reading ABOVE 59.94 is a period measured too short, which takes an
extra edge; 32.93 takes a missed one. Both appear in one run, so the signal
reaching `DEBUG_IN_PIN` is ragged on this source rather than simply absent -- the
direction a sync-on-green separator fails in on a progressive source. Why 480i
survives the same path is the open question.

**THE READING AT THE REFERENCE CLOCK IS CORRECT, AND THE FAILURE FOLLOWS THE
INSTALLED DIVIDER.** Measured on two consecutive boots, from `/bootlog`:
`524 lines x 60.00 Hz`, then `59.93`, then `59.93` -- three correct readings at
reference divider 2040 -- then `rate 31467 doubled 0 -> divider 1446`, and
nothing after it. So the source is measurable where the engine starts and
unmeasurable through what it solves, which narrows this from "the field rate" to
"the field rate through this divider".

**1446's ORIGIN IS `VideoPath::dividerCeilingForOutput()`**, and a previous
reading of it as unknown is withdrawn: it is the capture a 1080p raster can
show, in IF units, which is what `OutputWindow::maximumCapture()` returns for
that raster, and `test_video_source_acquisition.cpp`'s own `RasterDivider` of
1444 states the same arithmetic. `docs/sampling-table.md`'s 2046 is not in
conflict -- that ceiling is the line counter's and the table never models the
output's.

**THE ENGINE USED TO STOP TRYING AFTER FIVE SECONDS. THAT IS FIXED.** The
reference-clock retry was stamped against `unmeasuredPasses_`, which saturates
at `AcquiredPassCeiling`, so the gap it was keyed on stopped growing 255 passes
in and every retry stopped with it -- measured as total console silence across
50 s, a `/sc?~` and a `/restart`, with `loop()` demonstrably running because
`/samplinglog` and `/testbus` both answered and printed throughout. The retry is
counted in its own passes now and stays reachable for as long as the source is
unmeasurable, so the state is a cycle rather than a stall. **It does not make
480p acquire**: the reading still fails through the solved divider.

Confirmed on the unit, `ypbpr` unmeasurable, one 75 s window each:

| build | reference-clock retries | span |
|---|---|---|
| before | 17 | 3.03 s to 7.95 s, then silent |
| after | **235** | 2.21 s to 75.14 s, continuous |

`investigations/the-recovery-clock-saturates-and-the-engine-goes-silent.md`.

**THE INSTALL LEFT THE CAPTURE COUNTER WITH NO ORIGIN, AND THAT IS FIXED.**
`SP_RT_HS_SP` read **0** through the whole unmeasurable state, and a selection's
sync-processor reset is what zeroes it: `retimeStopFor()` answers 0 where no duty
has been measured, `writeRetimeStop()` declines to write a 0, and a counter with
no origin cannot retime, so it never counts, so the duty that would replace the 0
never arrives. Frozen, the whole PLL group written through `/sampleclock` six
times across dividers 2040 and 1446 left the origin at 0 every time -- so
sweeping the clock could not escape it, because a reference install goes through
`Adc` alone and does not write the origin. `VideoPath::writeRetimeOrigin()` is
now the one writer for both the install and the solve, and the fallback origin it
already carried for the reference retry reaches both. Measured after the fix on
the same source: `SP_RT_HS_SP` **1963** at divider 2040, where it was 0.

**That also closes the doubled-scan feedback.** A count taken after the install
used to come back wrong -- 218, then 333 -- and `shouldDoubleLine()` reads a
count that low as SD, so the engine re-decided line doubling and halved
`IF_HSYNC_RST` to 723 against a 1446 divider on a source it had itself logged as
`doubled 0`. The state then sustained itself: every later divider install kept
the doubled scan. After the fix the scan stays progressive, `IF_PRGRSV_CNTRL` 1
and `IF_HSYNC_RST` 2040 against divider 2040.

**IT STILL DOES NOT ACQUIRE, AND WHAT REMAINS IS NARROWER.** With the origin
written, the scan progressive and the divider at the reference, an input change
to `ypbpr` reads `STATUS_SYNC_PROC_VTOTAL` 97 and
`STATUS_SYNC_PROC_HTOTAL` 433 against `PLLAD_MD` 2040 -- the sync processor
counting nothing coherent at a divider a BOOT counts 524 at. So the open question
is no longer the field rate, the divider, the clamp or the scan: it is why the
sync processor counts this source from a boot and not from an input change, with
the measured arrangement identical. `investigations/the-retime-stop-is-the-counters-origin.md`.

**Two causes are REFUTED by direct test and must not be reached for again:**

| refuted | by |
|---|---|
| the held clamp starves the SOG slicer | `SP_NO_CLAMP_REG` cleared by hand and held clear 12 s: the count stays 97..100 and `HLOW_LEN` goes on dithering 1..32 |
| the divider value 1446 | neither 2040 nor 1446 recovers the collapsed state, and 2040 measures the source correctly from a boot |

**THE DIVIDER AND THE SCAN DESCRIBED DIFFERENT LINES AT EVERY REFERENCE, AND
THAT IS FIXED.** `PLLAD_MD` and `IF_HSYNC_RST` disagreed in both unmeasurable
states -- 1400 beside 2040 reached by an input change, 2040 beside 1446 reached
by a boot. The line counter is a count of IF units and the decimation is what an
IF unit IS, so the two are one setting: `setResetParameters()` and the input
selection each pair a reference divider with the scan for it, and
`tryNextReferenceClock()` installed the next one through `Adc` alone. The
source's field rate is timed off that block's vertical, so every rate the retry
read was timed through a line that was not arriving.
`VideoPath::tryNextReferenceClock()` now applies the scan the reference line
implies. Measured after the fix, on both paths:

| path | `PLLAD_MD` | `IF_HSYNC_RST` | before |
|---|---|---|---|
| input change | 2040 | **2040** | 1400 beside 2040 |
| boot | 1446 | **1446** | 2040 beside 1446 |

**THE COLLAPSE IS NOT YET COVERED BY AN INSTRUMENT, AND THE OBVIOUS READING OF
IT IS NOT LICENSED.** Across a boot the engine logs
`524 lines x 59.93 Hz -> line rate 31468` twice at the reference, installs
divider 1446, and then reads `524 lines x 0.00 Hz`, `39.88`, `40.04` and
`192.50 Hz` against a true 59.93. A `/samplinglog?ms=25` run over the same boot
reads `sp_vtotal` **97 or 98** steadily, with `sp_htotal` 1446 at divider 1446
and 430 at 2040 -- but its first sample lands about a second AFTER the engine's
last correct reading, so **the log does not cover the window the collapse happens
in** and the two figures are a second apart rather than two instruments
disagreeing. Reading them as a disagreement is the error
`CLAUDE.md` warns about. What the log does establish is that the state after the
collapse is steady rather than dithering. A log queued before detection is what
would cover the transition, and nothing has done that yet.

**THE DIVIDER SEPARATES THE LOCK FROM THE COUNT.** At the ceiling-chosen 1446
the ADC PLL latches -- `STATUS_SYNC_PROC_HTOTAL` 1446 against `PLLAD_MD` 1446 --
and at the reference 2040 it does not, reading 430..439. The boot's one correct
measurement is taken at 2040, the divider the PLL does not latch at, and 2040 is
above `dividerCeilingForOutput()`'s 1446 for a 1080p raster. So the clock the
source can be measured through and the clock the output can show it at are not
the same clock on this source.

**WHAT STILL SEPARATES A BOOT FROM AN INPUT CHANGE IS `BringUp::init()`, AND IT
IS NOT YET TESTED.** A boot runs it immediately after `applySavedInputSource()`,
so `Adc::init()`, `SyncProcessor::init()` and `InputFormatter::init()` all run
with the component input already selected; an input change reaches none of them
unless a low-power entry has armed it. An earlier reading that the bring-up
cannot be the difference was drawn from a `vga` diff of two states that had both
been through boots, and does not reach this case.

**THE `fieldRateHz() > 0.0f` PROXY IS NO LONGER FATAL, ONLY SLOW.** It gates the
retry on having read SOME rate, which separates "the count refused a confident
reading" from "nothing pulsed" -- and this source produces both, the boot log
interleaving `0.00 Hz` readings with the 39.9 ones, so the gate fires
intermittently on the very case it serves. With the retry reachable for as long
as the source is unmeasurable, an intermittent gate costs passes rather than the
whole recovery.

**IT IS READ ON A BOOT AND NEVER ON AN INPUT CHANGE**, which is the sharpest
split measured on it. Three boots of three produce the correct reading at the
reference divider and then the strand; three input changes of three produce not
one `sampling:` line -- `DETECT: 27ms, syncFound 2` and then nothing but the
reference retry, from a bypassed output and from a scaled one alike. So whatever
the boot establishes before `applySavedInputSource()` is part of what makes this
source readable, and an input change does not establish it.

**THE TWO TEST BUSES DISAGREE CATEGORICALLY, in one acquisition with a control
repeat.** Frozen, `/samplinglog?rates=24&pulses=1` off each in turn:

| bus | readings |
|---|---|
| input formatter's vertical, `sp=0` | **0 mHz, 24 of 24** |
| sync processor's out proc, `sp=1` | **11.99 Hz** x19, ~14.9 Hz x5 |
| input formatter's vertical again | **0 mHz, 24 of 24** |

11.99 Hz is 59.94 / 5 exactly, and the IF bus reading 0 either side of the SP
reading is what rules out the selection having been disturbed by the first run.
**So the field rate is timed off a bus that carries nothing here** -- 
`TestBusRateMeasurement::sourceFieldRateHz()` takes `selectInputVsync()` on
every path a measurement uses, and the input formatter is configured FROM the
divider the measurement sizes. In a second state both buses read 0 for 72 of 72,
so neither is a usable instrument once the source has been lost.

**AND THE RETRY HAS A SECOND GATE, WHICH IS STILL CLOSED.**
`tryNextReferenceClock()` is passed `status == Unmeasurable && fieldRateHz() >
0.0f`, and where no reference is in force that is the whole permission to
install one. On the boot path the rate reads `0.00 Hz`, so **zero retries fired
in 90 s** even with the saturation fixed -- the engine is silent again for a
different reason, one gate further on. Removing or narrowing that proxy is the
next change, and it is what the sampling-clock deadlock note in `Adc.cpp` argues
for: a clock that cannot be measured through is a deadlock whether a rate was
read or not.

**THE ENTRY SEQUENCE RETURNS THE BLOCKS TO A DEFINED STATE NOW, AND IT HOLDS
THE COUNT.** The sequence is: the change is detected, the output is blanked, the
blocks the measurement reads are returned to the state an unmeasured source is
counted through, then the source is measured.
`VideoPath::inputTimingsChanged()` calls `SyncProcessor::prepareForDetection()`
for the third step -- the clamp and the coast window, which the processor counts
WITHIN and which the previous source's values can place outside this source.

**A DEFINED STATE, NOT A RESET.** Pulsing `SFTRST_SYNC_RSTZ` there was tried and
is withdrawn: the reset zeroes `SP_RT_HS_SP`, no origin can be derived until a
duty has been measured through the block that will not retime without one, and
the measured result was a source that stopped acquiring at all. Resetting on a
cadence of its own was worse again -- the processor never got a quiet window to
complete a count in, which is the ladder's own lesson.

**What it bought is the count.** Before, the count collapsed 524 -> 219 -> 208
-> 97 within seconds of the divider going in and never returned. After, it holds
a correct **524 across every reading** while the rate is refused:

```
sampling: 524 lines x 39.88 Hz -> line rate 0
sampling: 524 lines x 39.87 Hz -> line rate 0
sampling: 524 lines x 40.01 Hz -> line rate 0
sampling: 524 lines x  0.00 Hz -> line rate 0
```

**So what is left is the field rate alone**, which is what the entry at the head
of this section describes -- and it is now the only thing left, on a count that
is stable and correct.

**AND THE RECOVERY IS GATED ON THE WRONG STATUS.** The reference retry fires
only where `measureRate()` answers `Unmeasurable`, and a block that cannot count
answers `NotSteady` -- the count never settles, so the pass returns before
`measureLineRate()` and before any rate is read. Both of the engine's exits from
an unmeasurable source are therefore shut in exactly the state that needs them:
`NotSteady` skips the status test, and `fieldRateHz() > 0.0f` shuts the other
when a rate was read as 0.00. Measured in the host suite as **0 advances across
56 passes** on a wedged block, and on the unit as 0 retries in 90 s.

**What settles it is a decision rather than a measurement.** The rule the proxy
stands in for is *"a pass that measured nothing writes nothing"*, which
`test_video_path_raster.cpp` and `test_video_path_windows.cpp` both pin with
`g_fieldRate = 0.0f`. Narrowing it to *"a pass that measured nothing writes no
divider DERIVED from the measurement, and may still install a reference to
measure through"* opens both exits and changes what those two tests assert.

**AND SILENCE IS NOT A STALLED LOOP HERE**, which is worth knowing before
reading one as a wedge. `measureRate()` prints only from `measureLineRate()`, so
a pass that returns `NotSteady` or `ClockSettling` runs and says nothing -- and
a count whose vertical-sync reconciliation is fed a wandering `VPERIOD_IF`
never settles, so every pass takes that exit.

**THE FIELD RATE IS NO LONGER THE EXPLANATION, AND THE MULTIPLES ABOVE HAVE A
CAUSE.** Two defects in the measurement are fixed and the readings quoted
throughout this entry -- 39.88, 39.87, 40.01 Hz against a true 59.93, which is
two thirds -- were the engine's own doing rather than the separator's.

`sampleFieldRateHz()` has two estimators: the probes, which ask which value
RECURS across nine single timings, and a median of three. On this source a single
timing is right in 32 of 228 readings, so the median returns a multiple. Which
one was in force was decided by `rateConfirmed_` -- a flag that answers whether
the boot's one re-measurement has been TAKEN, set before the reading it took was
judged, and cleared only by the constructor. So the recheck retired the only
estimator that works on this source, permanently, a few hundred passes after a
fully acquired state: `PLLAD_MD` 1446 latched against
`STATUS_SYNC_PROC_HTOTAL` 1446, `STATUS_SYNC_PROC_VTOTAL` 524,
`STATUS_MISC_PLLAD_LOCK` 1. The refusal that followed reached
`tryNextReferenceClock()`, which replaced the latched divider.
`investigations/the-cheap-field-rate-is-earned-by-the-probes.md`.

The estimator follows the probes' own unanimity now, which is a measured property
of the source. **And the rate is read once per rate/duty pair** rather than
re-read five passes after the install, through the clock it was itself used to
choose -- `investigations/the-rate-is-re-read-through-the-clock-it-chose.md`.

**Measured after both fixes**, on `ypbpr` at 480p: the field rate reads a steady
59.93 Hz through the solved divider, the source reaches `source acquired`, the
duty is measured, a source key is formed and the rate match steers the display
clock. At output 720x480 it acquires fully -- `installing: divider from line rate
31468, doubled 0 -> 1858`, `duty: 136 pulse / 1858 divider, htotal 1858`,
`source key: 520@59.94/732??`, `rate match: source 59938 mHz, output 59919 mHz`.
`vga` is unaffected: acquired in 10.2 s, `PLLAD_MD` 2200 against
`STATUS_SYNC_PROC_HTOTAL` 2200, count 311, frame time lock converged to an error
of 11920 and holding.

**IT STILL DOES NOT ACQUIRE AT 1920x1080**, and what is left is the two entries
below rather than anything about the rate.

### FIXED: the scan decision oscillated, and it was taken from one reading

Measured 2026-10-12 on `ypbpr` at 480p into 1080p, from one boot's log, before
the fix:

```
measured: 524 lines x 59.93 Hz -> line rate 31467
installing: divider from line rate 31467, doubled 0 -> 1446
measure: rate held 59.93 Hz, measuring duty
installing: divider from line rate 31467, doubled 1 -> 2200
if line counter: 2200 does not fit, holding 1100
measured: 521 lines x 59.93 Hz -> line rate 31288
installing: divider from line rate 31288, doubled 0 -> 1446
```

**`lineDoubled_` flipped between two installs of one measurement**, putting a
progressive 524-line source on the doubled scan at a divider of 2200 — which the
input formatter's eleven-bit line counter cannot hold, so `IF_HSYNC_RST` was held
at 1100, which IS the doubled scan on a progressive source.

**The scan was decided from the LAST READING of the count where every other
decision waits for a settled run.** `runPass()` passed
`SourceMeasurement::readSourceLines()` straight to
`VideoPath::prepareToMeasure()`, and `VideoSignal::countIsSource()` accepts
anything from 200 to 1300 — so a single reading below
`InputFormatter::DoubleBelowLines` reads as a short source. The count on this
source reads 524 for three readings and then 390.

It takes `SourceMeasurement::settledSourceLines()` now, which is 0 until the
steadiness run has agreed, and `solveLineDoubling()` ignores a count of 0.
Guarded by `test_video_source_acquisition.cpp`, which fails against the old
reading.

**The latch is NOT the right gate here and must not be used as one.** The sync
processor counts the source's vertical directly and does not care what the input
formatter is doing, which is what breaks the circularity: a scan mode left wrong
makes the line rate unmeasurable, and one derived after that gate is never
reached. `test_video_path.cpp` asserts that circularity stays broken.

Measured after the fix, one boot on `ypbpr` at 480p:

```
measured: 524 lines x 59.93 Hz -> line rate 31468, clock 2040@15625 htotal 1661 UNLATCHED
installing: divider from line rate 31468, doubled 0 -> 1446
measure: rate held 59.93 Hz, measuring duty
duty: 106 pulse / 1446 divider, htotal 1446, negative
source key: 524@59.94/733??
```

One install, one correct duty — 106/1446 is 7.33% against 480p's own 7.2%, where
the oscillating state read 161 or 222 of 2200. `vga` acquires in 8.5 s with
`sync pad: driven`.

### The reference clock latches on `vga` and not on `ypbpr`

Measured 2026-10-12, with the latch witness now on every reading. The reference
clock is the clock every measurement of an arriving source is taken through, and
`STATUS_SYNC_PROC_HTOTAL` against the divider is the only thing on the board that
reports whether the PLL is running it.

| clock | source | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|
| 2040 @ 15625, the reference | `vga` 320x256@50 | **2040**, exact, every reading |
| 2040 @ 15625, the reference | `ypbpr` 480p | 1670, 1670, 225 |
| 1446, trialled as the reference | `vga` | **1446** |
| 1446, trialled as the reference | `ypbpr` 480p | 877, 1199, 871, 197, count 524 -> 213 |
| 2200, solved | `vga` | **2200** |
| 1446, solved | `ypbpr` 480p | **1446** |

**Every divider a measurement chooses latches. The reference latches on one of
the two sources, and no single value fixes that** -- 1446 is as good as 2040 on
`vga` and worse on `ypbpr`.

**The count and the field rate both survive an unlatched clock** -- the sync
processor counts the source's vertical directly and the field rate is an ESP
timing off `DEBUG_IN_PIN` -- so a reading reads entirely healthy through a
pipeline clocked by the 27 MHz crystal. **The duty does not**:
`STATUS_SYNC_PROC_HLOW_LEN` reads 1..8 unlatched against a steady 106 latched,
which is 7.33% against 480p's own 7.2%.

**What this blocks is measuring the duty at the reference**, which is what would
let the whole measurement happen in one place and need one solved install. The
duty is dimensionless and portable across dividers, so what it needs is a LATCHED
clock rather than the final one.

**Two readings after the sync-type probe are not evidence.** 2073 then 2264 on
`vga`, whose settled readings are exactly 2040: the probe writes
`SP_EXT_SYNC_SEL` and the chip latches that as a SOG switch. Judge on settled
readings.

`docs/investigations/the-reference-clock-latches-on-one-bench-source.md` has the
refuted divider sweep, why the VCO gain crossover explains nothing, and why a
frozen poke cannot answer it.

**Next**: a first stage that installs a candidate, waits the settle and tests
`VideoPath::samplingClockLatched()`, advancing on a clock that did not latch
rather than on a measurement that failed for any reason. The witness exists and
no caller consults it yet.

### FIXED: the black band 8% into the line is the ADC clamp, not the line filter

Reported as the VDS line filter's, because it comes and goes with `/uc?m`. It is
`SP_CS_CLP_ST`/`SP_CS_CLP_SP` sampling the black level off picture: the register
counts the chip's 27 MHz reference, `VideoPath::applySamplingClock()` handed it
the ADC divider and `VideoSourceAcquisition::placeClampWindow()` handed it the
quarter-count the fractions were fitted to, and the pair held whichever wrote
last. On the Wii at 480p into 1920x1080 at divider 1212 that is a start of 108
against 19, which lands 41 source pixels inside the picture.

The correlation with the filter is the DIVIDER: the filter's line buffer caps it
at 1212 where it is otherwise 1446, and the broken writer placed the clamp from
whatever the divider was.

Two costs, and the bar is the smaller: the frame is also clamped dark, a picture
mean of 152.4 against 205.7 at a start of 19. Both writers convert inside
`SyncProcessor` now.
[investigations/the-clamp-had-two-owners-and-two-units.md](investigations/the-clamp-had-two-owners-and-two-units.md)

### FIXED: the line filter could not be turned back on, and the install moved the count

The symptom: turning the filter off took the divider from 1212 to 1446;
turning it back on left it at 1446, `applyLineFilter()`'s `fits` test failed
against the 1020-sample line buffer, and `VDS_D_RAM_BYPS` stayed 1. Four
presses in one run, the register unmoved after the first.

**`VideoPath::setLineFilter()` is not where it was.** The route reaches
`installSampling()` both ways, and given a key that matches a published raster
the whole cycle works, which `test_video_path_windows.cpp` holds still.

**THE CAUSE WAS OUR OWN WRITE BEING READ AS THE SOURCE MOVING.**
`writePulseIgnore()` ran on every install declaring the scan unknown, so it put
the fraction back over the value the measured scan chose. Polled across the off
press on the Wii at 480p into 1080p:

| t (s) | `SP_H_PULSE_IGNOR` | `SP_H_CST_SP` | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|---|---|
| 0.03 | 14 | 830 | 524, settled |
| 0.08 | **70** | 830 | **530** |
| 0.23 | 70 | **256** | 514 |
| 0.25..0.86 | 70 | 256 | 521, 455, 508, 521 |
| 0.97 | 70 | **834** | 524, and 524 thereafter |
| 1.46 | **14** | 834 | 524 |

The byte alone took the count off the source. The engine read that as a source
change, `inputTimingsChanged()` put the coast window back to `DefaultCoastStop`
and the count was unusable for 740 ms -- which is where the key was taken. 480p
is 525 lines and `SourceTiming::lookUpIn()` matches the count exactly, so
`521@59.94/…` matched nothing, `dividerCeilingForLineFilter()` had no
`activePixels()` and returned 0, and no later press had a bound to bring the
divider down with.

**Fixed at both halves.** `writePulseIgnore()` asks `SourceMeasurement` for the
scan, and the latch settle is a duration -- `SourceMeasurement::LatchSettleMs`,
timed from the first reading after the latch, with the console saying how long
each wait was. Re-measured on the same press:

| | before | after |
|---|---|---|
| `SP_H_PULSE_IGNOR` | 14 -> 70 | 14 throughout |
| `STATUS_SYNC_PROC_VTOTAL` | 530, then 514/521/455/508 for 740 ms | 524 in all 232 samples |
| `SP_H_CST_SP` | 830 -> 256 -> 834 | 830 throughout |
| `SP_RT_HS_SP` | 1441, from a duty of 68 | 1403, which is `1446 - 106 + 63` |
| duty at the install | 68 of 1446, twice | 106 of 1446, the settled reading |
| four presses | IN, OUT, OUT, OUT, OUT | IN, OUT, IN, OUT, IN |

The console across the four presses is now four `installing:` lines and nothing
else: no source absent, no timings change, no re-solve, because the count never
moved.

**What the duration alone did not reach.** With the count still moving, the
settle got the duty right in three runs of four and read 159 in the fourth, and
`STATUS_SYNC_PROC_HTOTAL` echoed the divider through every one, so
`Adc::dividerLatched()` could not tell the bad reading from the good. The scan
fix removes the transient the duty was being read through rather than waiting
it out, and the two together leave nothing to wait for on this press.

**Whether the stale key also reaches the aspect is NOT established**, and the
obvious reading of it is a trap. `aspect` was seen going 17778 to 13333 across
the press, but **`SourceTiming`'s 720x480p row declares `F43`**, so a key that
matches gives 13333 too -- the 16:9 in force was the user's own `/uc?G`, and
losing it on a re-derivation is a different defect. See the aspect entry below
for the trigger that does isolate it.

**Still open, on a GENUINE source change.** The coast window is a function of
the source's line rate alone -- `27e6 / lineRateHz`, in the chip's own
reference, so no divider moves it -- and it is reset to `DefaultCoastStop` when
a new source arrives, which is correct there. The loop count -> rate -> coast
-> count converges in two steps, but a key may still be adopted at the first
step rather than at the fixed point. The test with a right answer is that the
coast in force equals the coast the count implies. One residue of the same
measurement: the coast is left at 834, derived from the rate the bad count
gave, where 830 is right, because nothing re-places it once measuring stops.

### FIXED: the transmitted window's opening is a FRACTION of the line, and the field-rate band was the duration base

**The band is an artefact of what it was scored against.** `solve()` placed the
window at `sync + backPorch` converted as DURATIONS, and the display clock is
pinned near its ceiling, so that base sits at 139 samples whatever the raster
while the sink's opening grows with the line. A 50 Hz raster is 316 units
longer than a 60 Hz one, so the fraction grows 27 samples and a fixed base
reports it as a jump. Scored against `T x (syncPx + backPorchPx) / totalPx`
instead, the same readings plus two taken since are one residual of **6.01**,
spread 1.28, over rasters 1590 to 2402, two inputs, two output standards and
both field rates.

`solve()` now opens the window on that fraction and
`TransmittedWindowDelayPx` is 6. Measured on `vga` 320x256@50 into 1080p with
the picture lifted: our emitted blanking goes from **14 columns at the right to
0**, with 2 at the left. On 800x600@60 into 1080p it is 4 columns before and 4
after, and the aperture solves to 145 against the 145 the sink's window starts
at. `dividerCeilingForOutput()` recovers the standard's far edge correctly as a
consequence, so the 480p ceiling rises 1804 -> 1852.
[investigations/the-near-end-is-a-fraction-of-the-line.md](investigations/the-near-end-is-a-fraction-of-the-line.md)

**The warning below against 6 was correct for the base it was written against**
and does not transfer: on the duration base 6 leaves the 50 Hz raster 29 samples
short, which is what it says. What follows is the measurement that stands.

Four states, each a four-point fit with residuals under 0.3 columns, read off
the USB capture with automation frozen:

| source | input | output | T | field Hz | window opens | sync + porch | delay |
|---|---|---|---|---|---|---|---|
| Wii 480p | `ypbpr` | 1080p | 1602 | 59.94 | 146.05 | 139 | **+7.1** |
| Wii 480p | `ypbpr` | 720p | 2402 | 59.94 | 384.70 | 378 | **+6.7** |
| RiscPC 640x480@60 | `vga` | 1080p | 1600 | 60.02 | 145.19 | 139 | **+6.2** |
| RiscPC 320x256@50 | `vga` | 1080p | 1916 | 50.08 | 173.78 | 139 | **+34.8** |

**Both candidates a single off reading left open are ruled out.** Not the input
or the component path -- `ypbpr` at T 1602 and `vga` at T 1600 agree to 0.86
units. Not the output mode -- the same source gives +7.1 at 1080p and +6.7 at
720p. Not the lock's placement -- three `PAD_SYNC_OUT_ENZ` toggles leave the
first lit column at 262 every time. What moves it is the output's FIELD RATE, in
the bands [investigations/the-sinks-own-window-position-is-per-raster.md](investigations/the-sinks-own-window-position-is-per-raster.md)
already measured, so the quantity is the sink's own window position and not a
delay in the chain.

**The four readings of ~20 measured our own aperture.** They were taken with the
picture zoomed hard against the aperture's edge, and on a raster whose own
position is after the aperture the sink lands its window ON the aperture -- so
the walk extrapolates to the value already in force, whatever it is. A
measurement of this has to put BLACK at the aperture's edge or leave the
aperture well past the own position.

A change of placement is safe for the odd-width parity bias, which
`OutputWindow::solve()` re-applies per solve inside whatever bounds it is handed
-- insetting one edge or changing the span is what would break it.

**What is left is the source's own leading blanking**, which the sink used to
clip and now shows: 18 columns on the bench 320x256@50, none of it ours. That is
a question about whether a mode's border belongs in the capture, not about where
the window opens.
[investigations/the-window-delay-is-a-field-rate-band-not-a-constant.md](investigations/the-window-delay-is-a-field-rate-band-not-a-constant.md)

### 640x480@60 clips the top border and 8 units of the left, with its raster exact

Measured 2026-10-14 on `vga` into 1920x1080 at 16:9, the stored framing
forgotten so the computed default is judged. **This is the one mode of six whose
raster the engine matches EXACTLY and whose default still clips**:
`published_rasters.py` scores the loaded file's 640x480@60 at `+0.0` on all four
edges against DMT's row, and the engine places `oh` 260 against `144/800` of a
1445-unit counter and `ov` 35 against `35/525`.

What the card shows at that default: **the top border absent at every pan** --
green dominance 0.0 against 215 on every other mode -- and the left border
leaving between `oh` 252 and 254, which is 8 units before the default. The right
and bottom borders are present throughout. So both clips are at the NEAR end of
each axis.

**The horizontal candidate is the measured pulse**, which `retimeStopFor()`
subtracts straight into the counter's origin: `STATUS_SYNC_PROC_HLOW_LEN` reads
167 of `PLLAD_MD` 1444 where DMT states `96/800`, which is 173.3 -- **6.3
samples narrow**, and a pulse read narrow puts the stop high and the video early,
so a window at the published fraction opens late by the same 6.3 units. The
H-negative family reads 2.7 to 4.8 samples narrow and `RetimeOriginSamples` is
calibrated on it; this mode is 2 to 4 samples worse than its family.
[investigations/the-capture-origin-varies-by-mode-at-one-line-rate.md](investigations/the-capture-origin-varies-by-mode-at-one-line-rate.md)

**The vertical has no candidate at all.** No pulse feeds it, the capture margin
is 2 lines, and `videoAt()` puts the window 2 lines before the row's own start.
It is one line, and `320x480@60` -- the same 525-line frame at the same divider,
matched to a row whose `activeStartLine` is 34 rather than 35 -- keeps its top
border. So the two differ by exactly the one line between the rows.

**The mode-file collision is NOT the cause and was checked.** The loaded
definition carries both an AKF-derived 640x480 whose picture starts at 138 and
VESA's at 144, and the two are reachable separately -- `MODE X640 Y480 C256 F71`
answers `F71` and `F60` answers `F60` -- so `F60` lands on the VESA entry, which
is the row the engine matched.

### Composite sync frames 8 units early where separate sync frames flush

Measured 2026-10-14 on `vga`, 320x256@50 into 1920x1080 at 16:9, the stored
framing forgotten with `/sc?B` so the computed default is what is judged, and
the card's border read on all four edges of the emitted frame. One mode, one
cable, one raster, `SYNC 1` then `SYNC 0` with `/sc?~` between so the engine
re-probes:

| sync | `SP_SOG_MODE` | `HLOW` | `SP_RT_HS_SP` | `IF_HB_SP2` | default `oh` | all four at |
|---|---|---|---|---|---|---|
| composite | 1 | 154 | 2123 | 191 | 237 | **245** |
| separate | 0 | 156 | 2126 | 236 | 237 | **237** |

The published placement is identical -- `oh` 237 is `110/512` of the counter on
both -- and the 45 units between the two capture starts is
`VideoSourceLine::forDuty()`'s separator lead. **So the 8 units are the lead's,
not the raster's**: the lead applied is 45 units where this measurement wants
37, which is `SeparatorPulseEndLeadSamples` 82 rather than 66 on this mode. One
unit of the 8 is the `ceilf()` on `syncUnits`, which turns a 154-sample pulse
into 156.

**It is not today's origin term.** The lag is added to both sync types equally
-- 2126 against 2123 is the 2-sample difference in the measured pulse -- so it
cancels in the comparison.

**What it disagrees with is the lead's own measurement**, which fitted
`HLOW - 66` over six modes to an rms of 0.77 samples, this doubled mode among
them at a residual of 1.4.
[investigations/the-separator-lead-is-not-a-fraction-of-the-line.md](investigations/the-separator-lead-is-not-a-fraction-of-the-line.md)
read the card's frame POSITION per sync type and differenced the two; this reads
whether any black is left beside the border. **The two instruments differ by 8
units on this mode and agree on separate sync**, so what is needed is the
border criterion applied across the same six modes rather than another
constant fitted to one.

The dongle dropped two black frames out of nine during the composite sweep,
which the shear below accounts for; the seven that delivered are monotonic in
the black count and consistent.

### The RISC PC's composite sync shears about two frames a second, and the engine is not writing it

Measured 2026-10-13 at 320x256@50 on `vga`, `PATTERN CARD`, read off the USB
capture with a per-ROW metric: a clean frame has the same edge column on every
band, and the spread ACROSS bands inside one frame is the shear.

| | composite, `SYNC 1` | separate, `SYNC 0` |
|---|---|---|
| frames sheared past 4 px | **20 of 1693** | **0 of 1655** |
| of those, past 256 px | 18 | -- |
| rate | **0.67 per second** | 0 |
| edge position sd over 90 frames | 0.005 px | 0.002 px |
| `STATUS_SYNC_PROC_VTOTAL` | **308** | 311 |

The source sends 312 lines, so the separator undercounts by three on composite
and by one on separate.

**IT SURVIVES A FREEZE, so ongoing register writes are not necessary for it.**
Frozen, with `IF_HB_SP2`, `PLLAD_MD`, `VDS_HSCALE` and `VTOTAL` read identical
either side of a 30-second window, three frames of 1561 still shear past 256 px.
That does not exonerate a static misconfiguration, which a freeze preserves --
but it rules out the solve rewriting a register underneath the picture.

**The deinterlacer is not the difference.** `MADPT_PD_RAM_BYPS`,
`DIAG_BOB_PLDY_RAM_BYPS`, `MADPT_Y_WOUT_BYPS` and `MADPT_VIIR_BYPS` all read 1
on both sync types, so it is out of circuit on both -- despite the console
printing `scan: interlaced, count 311` and `deinterlacer: motion adapt engaged`
on the composite path. What differs is the separator's own configuration, which
is correct to differ: `SP_H_PULSE_IGNOR` 107 against 255, `SP_DLT_REG` 192
against 0, coast 7/6 against 0/0.

**The count dip is what the engine reacts to, not what it causes.** The console
shows `source absent: 307 lines` / `source acquired: 311 lines` pairs several
times a second, each dip resetting the acquired run, re-running the sampling
phase search and resetting the frame time lock -- the amplification
[investigations/the-pulse-ignore-fraction-breaks-a-progressive-source.md](investigations/the-pulse-ignore-fraction-breaks-a-progressive-source.md)
describes.

**This is not new.** A csync path reading `SP_VTOTAL` 308 and glitching, against
separate sync reading 311 and clean, is recorded from 2026-08-14 and 2026-08-15.
What is new is the rate and the freeze result.

### A latched separate-sync path answers to `/restart`, not to `/sc?~`

Reached by a mode the separator never reads -- 1280x720@60 on composite, a 2.42%
sync duty. The engine lands on `SP_SOG_MODE` 0 with a composite source present,
`SP_H_PULSE_IGNOR` 255, `SP_RT_HS_SP` 15 -- the bare origin, nothing measured --
`STATUS_SYNC_PROC_VTOTAL` 0 and the previous mode's geometry still held.

Three `/sc?~` cycles with a mode re-issue between each left it there. `/restart`
cleared it in **28 s**, repeatedly, the source acquiring with `SP_SOG_MODE` 1 and
the divider latched. So the TRIGGER decides the recovery and the registers do not
say which trigger it was, which the `/sc?~` entry above already records for the
input-change case.

### ~~The Wii's 480p is placed by a separator lead that is wrong for its raster~~ -- FIXED

**FIXED 2026-10-13.** The lead is the source's measured sync width less 66 ADC
samples, not a fraction of the line, which gives this raster 40 units where 70
per mille gave 101. Measured over six published modes as the difference between
the two sync types; a free fit returns a slope of exactly 1.000 on the measured
width with residuals under 1.5 samples.
[investigations/the-separator-lead-is-not-a-fraction-of-the-line.md](investigations/the-separator-lead-is-not-a-fraction-of-the-line.md)

On the emitted frame the black down the left falls from **134 px to 20** and the
44 px that was cut off the right is no longer cut. What was measured before:

**It costs picture at BOTH ends, and only one of them looks like a fault.** The
capture window's picture region is 107..1321 against content at 191.5..1348.7:

| | capture units | emitted px |
|---|---|---|
| black shown at the left | 84.5 | 134 |
| content **cut** at the right | 27.7 | 44 |
| our display window opening late | -- | 12 |

The window is already pinned at the capture floor -- `videoAt()` asks for 105 and
`firstCapture()` is 106 -- so the lead is clamped and still 84 units out.

**The bound on the true lead needs no assumption about the source's border.**
Content cannot lie outside active video, which CEA puts at 205.6..1419.0 of a
1446-unit counter, so `205.6 - L <= 191.5` and `1419.0 - L >= 1348.7` give
**14.1 <= L <= 70.2** against the 101 applied. A black border the source paints
only ever makes the lit region smaller, so it cannot widen that bound.

**`VDS_Y_OFST` does NOT separate captured blanking from the source's own black
border.** It lifts everything inside the display window, so both read as the same
grey -- which is why this is a bound and not a value, and why the RISC PC's card
carries the measurement instead.

**The 12 columns at the left are ours and separate**, the display window opening
about 13 raster units late, which is `OutputMode::HsyncStartPx`. 1.37284 emitted
px per raster unit, residuals <=0.5 px over 1120 units.

**Panning cannot fix it and runs out.** The capture is cropped to compensate, and
the window reaches the end of the line -- `oh` 256 + `eh` 1189 against `ch` 1447
-- before the border is gone.

### The Wii's 480p matches its CEA raster on half its acquisitions

Measured 2026-10-14 on `ypbpr`, 480p into 1920x1080, six acquisitions taken by
switching to `vga` and back, each read once settled. **They fall into two
families and nothing the engine reports distinguishes them:**

| | `oh` | `eh` | placed from | black L R T B | lit |
|---|---|---|---|---|---|
| 3 of 6 | 206 | 1214 | **CEA 720x480p**, `122/858` and `720/858` | 39..40, 46, 33, 21 | 1834x1026 |
| 3 of 6 | 169 | 1250 | the generic envelope, `Axis` 0.117 and 0.864 | 69..97, 42..70, 42, 27 | 1780x1011 |

`STATUS_SYNC_PROC_VTOTAL` is 524 on every one of them, so the frame is not it,
and the held duty is inside `SyncDutyTolerance` of CEA's 62/858 on five of the
six -- including two that still landed on the envelope. **So the duty is not the
discriminator either**, and the candidate left is the held FIELD rate: the
entry below has it reading two thirds at the solved clock, and 40 Hz against
CEA's 60 fails `StandardRateDeviationPerThousand` outright where 59.94 passes.
A timestamped console capture across the acquisition is what would say, since
`/geometry` does not report the field rate.

**A THIRD landing is worse than either.** One acquisition the same day held a
sync duty of **11.0%** where `STATUS_SYNC_PROC_HLOW_LEN` read 105..106 of
`PLLAD_MD` 1446 steadily over six samples -- 7.33%, which is CEA's to a tenth of
a point. `SP_RT_HS_SP` 1350 is `1446 - 159 + 63` and `/geometry` reported `sh`
160 against the 106 the other landings show, so the capture floor rose 54 units
and the counter's origin sat 54 samples early. A single reading of 159 against a
counter giving 105..106 steadily is the same mechanism as the entry below.

**THE DUTY'S SPREAD IS WHAT DECIDES WHETHER THE STORED FRAMING IS RECALLED, and
when it misses the shape falls back to 4:3.** Three `/input?src=ypbpr`
selections in a row, same source, nothing else touched:

| measured pulse | key | framing | shape | divider settled on |
|---|---|---|---|---|
| 93 of 1272 | `524@59.94/731??` | recalled | 17778 | 1212 |
| 100 of 1212 | `524@59.94/825??` | **none stored** | 13333 | **1272** |
| 89 of 1212 | `524@59.94/734??` | recalled | 17778 | 1212 |

731 and 734 are inside the key's tolerance of the stored 733 and 825 is not, so
the 8.25% reading is a second identity for one source. **The divider difference
between landings follows from the shape rather than causing it**: 4:3 narrows
the room on one axis, a narrower room moves the line filter's ceiling, and the
ceiling picks 1272 where 16:9 picks 1212. So a landing that reads as a different
sampling decision is the recall having missed, one step earlier.

**THE SINGLE READING IS CLOSED: THE DUTY NOW NEEDS TWO THAT AGREE.**
`SourceMeasurement::takeDuty()` takes a reading only once a second agrees with
it at `SyncWidthIdentity`, which is the tolerance the key compares at and so
the tolerance that decides what a disagreement costs. A reading in question
does not let a held one through either, because on a mode change what is held
is the outgoing source's duty.

**WHAT IS NOT ESTABLISHED IS THAT THE GATE CATCHES THIS PARTICULAR READING**,
because the reading would not come back: 16 `/input?src=ypbpr` selections in
one session gave 89 of 1212 on all 15 that re-measured, every one recalling the
framing. So the gate is proven at the host layer and the bench shows it
running -- two `duty:` lines a selection where there was one, and one landing
that caught the dither it exists to admit, 88 then 89 -- rather than shown to
reject the excursion. The next 8.25% reading is what confirms it.

**AND A WANDERING PULSE IS STILL NOT SEPARATED.** `DutyAgreementAttempts` takes
the latest reading as it stands after eight disagreements, which is the escape
the field rate already has and the same trade: a frozen capture is worse than a
framing chosen from one reading. A source whose pulse genuinely moves past
identity on every pass therefore still reaches a second identity.

**WHAT THIS DOES NOT MEAN IS THAT THE BLACK BORDER IS THE ENGINE'S.** On the
landings that DO match CEA the capture is exactly the standard's active region --
`IF_HB_SP2` 164 against the 164.8 the row puts video at, `IF_HB_ST2` 1380
against 1379 -- and the Wii still shows 39 columns of black at the left, 46 at
the right, 33 rows at the top and 21 at the bottom. In the source's own units
that is about 15 px, 17 px, 15 lines and 9 lines: **a safe-area border the Wii
draws inside its own active video**, confirmed against the RISC PC, whose modes
put the card's border flush to all four edges from the computed default on the
same output.

### The picture is washed out against the vendor image, and the ADC gain is the candidate

Measured 2026-10-13 on the Wii's menu at 480p into 1920x1080, both images
scored on the same 40-frame burst off the dongle through the same capture band,
twenty-five minutes apart:

| | ours | vendor 1.3 |
|---|---|---|
| mean RGB | 218.9 / 226.7 / 221.8 | 133.3 / 136.7 / 136.7 |
| spatial standard deviation | 32.1 | 74.0 |
| band above black | 98.7% | 78.1% |

Neutral in both, so it is a LEVEL and contrast difference rather than a tint --
the mean sits near the top of the range with the structure compressed to under
half, which is what clipped highlights look like.

**`ADC_RGCTRL`/`GGCTRL`/`BGCTRL` read 51 against the vendor's 123**, equal
across the three channels in both, and they are the only gain in the 101-field
diff between the two images on this source. `ADC_?OFCTRL` is 64/64/64 against
65/69/62, which is too small to explain it. `HD_Y_GAIN`/`U`/`V` are 0 here and
128 there, with `HD_MATRIX_BYPS` 0 in both, so whether that path is in circuit
on a scaled component source has to be established before it is ruled in.

**THE GAIN IS THE CANDIDATE, AND THE VENDOR'S VALUE IS NEAR THE MEASURED
KNEE.** Settled 2026-10-16 against PM5544's calibrated staircase -- see *The
ADC gain seeds clip the top of the range on BOTH colour spaces* above. On this
input the seeded 0x33 pins **52% of the frame at one level**, and the knee is
0x68. The vendor's 0x7B is on the right side of it; ours is not, which is
exactly the direction this comparison reported.

**AN EARLIER REFUTATION OF THAT WAS WRONG, AND THE INSTRUMENT IS WHY.**
`investigations/the-adc-gain-is-the-colour-spaces.md` concluded 0x33 clips
nothing, from aggregate statistics over a whole test card -- a mean, a
percentile and a clipped fraction. A knee near white moves none of them,
because the card's flat areas dominate every one. On a ramp the same path's top
step reads 16.1 of 51. Do not score a transfer curve on summary statistics, and
do not reinstate the reading that the register is inert.

**What is left is downstream of the ADC, or off the board.** `VDS_Y_OFST` 14 is
`ColourBalance`'s Component rest, a pedestal lift applied in the VDS and
already inside the 249.3; the sink's own range handling is the other half, and
the MS9288A's HDMI range signalling is not configurable from here.
**The discriminator is a stepped greyscale read on the capture AND the panel at
once**: near-white steps resolved on the capture and crushed on the panel are
the television's range or its enhancements, not the board's levels. The 78.1% against 98.7% is a separate thing --
the vendor pillarboxes this 4:3 source and the engine fills the raster, which is
`aspect-ratio.md` and not a fault.

Snapshots: `tools/gbsc-pro-hwtest/snapshots/wii-480p-1080p-ypbpr-stable-2026-10-13.json`
is ours; the vendor's is not in the tree, `bench-sources.md` has how to take one.

### The Wii's 480p vertical count is bimodal, and that is what is left

Measured 2026-10-12 on `ypbpr` at 480p, output 720x480, divider 1858 latched
against `STATUS_SYNC_PROC_HTOTAL` 1858 and the field rate reading a correct
59.93 Hz throughout. `Tv5725::SamplingLog` at 25 ms over 20 s:

| `STATUS_SYNC_PROC_VTOTAL` | share |
|---|---|
| 521 | 70% |
| 524 | 20% |
| 433..494 | 10% |

521 and 524 are three apart, so `SteadyRun::agree()` calls them two
measurements and each run settles on whichever it sees four of first. The solve
settles on 524 and the idle run on 521, and the engine then reads its own two
readings as the source moving.

**IT IS THE ROOT OF THREE SYMPTOMS, and two of them were diagnosed separately
before this was measured.** The picture ROLLS, because the capture height
follows the count. The colour flips GREEN to MAGENTA per acquisition, because a
count that moves moves the line rate, which moves the CKO across a crossover
row, which installs a different oversampling ratio -- and `PLLAD_CKOS` against
the decimators is the solid-green cause this page already carries.
`Tv5725::ColourSpace::applyYuv()` is correct and is not the place to look:
`DEC_MATRIX_BYPS` 1, `ADC_RYSEL_*` 1/0/1 and equal gains are what it intends.

**THE MAGENTA IS CONFIRMED A CONSEQUENCE AND IS GONE.** With the count held
steady at 1080p the same capture band reads mean RGB 218.9 / 226.7 / 221.8 where
it read 219.7 / 70.4 / 225.2 while it dipped -- the green channel alone moving,
which is what a changed oversampling ratio does. Nothing in the colour path was
touched. `investigations/the-pulse-ignore-fraction-breaks-a-progressive-source.md`.

**THE COAST WINDOW IS THE ONLY THING MEASURED TO MOVE IT, AND ITS OPTIMUM IS
NARROW AND FRAGILE.** Frozen, sweeping `SP_PRE_COAST`/`SP_POST_COAST`:

| coast | `VTOTAL` 524 | note |
|---|---|---|
| 0 / 0 | 0% | a steady 521, 120 of 120 |
| 0 / 6 | 4% | nearly unimodal, duty 136/1858 |
| 7 / 6, shipped | 14% | worst of both |
| 20 / 20 | 41% | |
| 44 / 44 | 90% | |
| **48 / 48** | **95%** | |
| 50 / 50 | 3% | |

**A SEARCH SCORED ON THE COUNT ALONE PICKS 48/48 AND IS WRONG.** Unfrozen, that
coast holds the PLL off the reference long enough to destroy the duty --
`duty: 2 pulse / 1858 divider, NOT A PULSE`, then 32, 69 and 55 across four
solves -- and the capture counter's origin is derived from the duty. Any
autotune has to score the duty as well, and has to score it with the engine
RUNNING.

**AND A FROZEN SWEEP IS NOT A SWEEP.** Frozen, the output is blanked and the
state degrades as the run goes on: a live run of the same settings produced no
console output and an all-black capture from 20/20 onward, so those rows are
dead state rather than data. The live score is the one that means anything --
arms, `source absent` transitions and frames with a picture, per setting, with
automation running.

**Two causes are REFUTED by direct sweep and must not be reached for again:**

| refuted | by |
|---|---|
| the sync-on-green slicer level | `ADC_SOGCTRL` swept 5..24 frozen: flat at 13..19% correct counts across the whole range |
| the clamp being held off | `SP_NO_CLAMP_REG` released by hand: 4% to 6%, `STATUS_SYNC_PROC_HLOW_LEN` unchanged at 136 |

**What it still costs is the terminal retry cycle.** The engine re-solves on
every flip -- 14 arms and 17 installs in 45 s -- and one of those solves
eventually lands on a count outside the pair. Observed: a solved `lineRateHz`
of 6791 against a true 31468, after which nothing measures and the
reference-clock retry cycles indefinitely, 98 retries in 30 s. `/restart`
recovers it in about 15 s; `/input?src=ypbpr` does not, for the reason the
entry below gives.

### FIXED: the Wii at 480p never acquired at 1920x1080, because the pulse ignore ignored its sync pulse

`SP_H_PULSE_IGNOR` is compared against pulse widths counted in ADC samples, and
it held one byte, 107, fitted through a 2200-sample line. The 1080p raster at
59.94 Hz affords this source a divider of 1446, where its 7.33% sync pulse is
106 samples -- one under the threshold -- so the separator ignored the source:
the count collapsed to about half, the input formatter's vertical went quiet,
and the reference retry cycled for ever. The field follows the divider now, 49
per thousand of the line, and the search writes 16 because an unlocked block
counts at the chip's 27 MHz reference, where the same pulse is 63 or 31
samples. Measured after the fix, `vga` to `ypbpr` at 1920x1080: a reading 1.7 s
after the selection, acquired at 4 s, held with `STATUS_SYNC_PROC_VTOTAL` 524
against `PLLAD_MD` 1446 latched, the coast window placed, the DACs on and a
picture on the USB capture.
`investigations/the-pulse-ignore-is-a-fraction-of-the-line.md`.

**THE FRACTION ACQUIRES THIS SOURCE AND DOES NOT HOLD IT, and the threshold is
now chosen by the measured scan.** 70 at 1446 is 66% of the source's 106-sample
pulse, close enough that the separator drops a line whenever the slicer's output
narrows: the count dipped off 524 in 5..8% of samples and 2 to 24 frames of 40
were identical. The bound the fraction satisfies is a SERRATED vertical
interval's -- above the equalising pulse, below the sync pulse -- and a
progressive source's whole sync pulse is narrower than such a pulse, so one value
cannot serve both. `SyncProcessor::ProgressiveCsyncPulseIgnore` is 14 and
`SourceMeasurement::scanKnown()` decides when it may be used; unknown keeps the
fraction, which is what acquires either scan. Measured after: 40 of 40 frames
identical in each of four bursts over two minutes, count 524 steady.
`investigations/the-pulse-ignore-fraction-breaks-a-progressive-source.md`.

**What the hunt measured that stays true.** The output raster's horizontal
total is the display clock over the output's own lines and the SOURCE's field
rate, so one output resolution is several rasters and
`VideoPath::dividerCeilingForOutput()` follows it. Computed from
`OutputMode::solve()` at `EngineCeilingHz`, for a 524-line 59.94 Hz source:

| output | raster total | capture the raster can show | divider installed |
|---|---|---|---|
| 1920x1080 | 1602 | 1447 | 1446 |
| 720x480 | 2060 | 1858 | 1858 |
| 768x576 | 2162 | 2004 | 2004 |
| 1280x720 | 2402 | 2149 | 2046, the line counter |

1080p is the tightest because 1125 lines at 59.94 Hz leave the shortest line
at 108 MHz; the same output at 50 Hz solves 1920 and affords 1724, and `vga` at
320x256@50 is line doubled with a ceiling near 3448, which is why the band
never showed on the bench RISC PC. The divider itself was never the fault:
1446 latches, `duty: 106 pulse / 1446 divider, htotal 1446`, on every boot.

What remains on this source at 1446 is the 521/524 count pair and its dips to
433..494, the entry above: the state reads `absent` for a sample or two every
half minute, a dip steady enough to pass the run solves once on it and once
more on the true count, and an input change churns through a few such solves
in its first ten seconds before holding.

### The held line rate takes any reading at a moved count

`SourceMeasurement::rateFollowsCount()` accepts anything at a count that moved,
because a moved count is a mode change -- so a garbage pair read through a
collapsed count becomes `lineRateHz()` on ONE reading. Measured while the
separator was ignoring the Wii's pulse: `214 lines x 38.13 Hz -> 8198`,
`223 x 133.64 -> 29937` and a pinned 201 x 133.9 -> 26921, each reported by
`/geometry` for the rest of its run, and the one coast window placed from such
a rate landed at `SP_H_CST_SP` 3187 on a 2040-sample line. A count wrong by
half is inside `VideoSignal`'s bounds and so is a field rate of 134 Hz; nothing
checks the pair. No longer terminal now that the separator reads the source,
and not fixed: a reading like it still strands the held rate until the next
settled one replaces it.

### FIXED: a solve re-armed and re-blanked itself on a source standing still

Three defects measured 2026-10-12 on `ypbpr` at 480p, each of which on its own
kept a correctly measured source off the panel.

**THE SEPARATOR LATCH HAD A THIRD OWNER AND IT ANSWERED FOR NOTHING.** The
engine's own divider, clamp and coast placements switch the sync separator, and
the chip latches that in `STATUS_INT_SOG_SW` about a second later -- so every
solve armed a re-measure of a source that had not moved. Measured: 19 solves in
60 s, each blanking the output for an encoder relock, with the source measured
correctly throughout. `vga` never shows it because the bit is acted on only
where `SyncMeasurement::isCsync()`.

The latch is an event inside the rate question now rather than an arm of its
own: it opens the expensive half of `rateMoved()`, which refuses a reading
`VideoSignal::isVideo()` rejects. That is what makes it safe on this source,
where the rate reads two thirds through the divider a 1080p raster forces -- a
refused reading is not evidence the source moved.

**A LINE PERIOD THAT NEVER SETTLES GATED THE WHOLE RATE QUESTION.** A
`solvedLinePeriod_` of 0 returned before the recheck and the disturbance as
well as before the cheap half, so a source whose `HPERIOD_IF` never holds still
could reach neither exit. Only the cheap half needs the period.

**THE OUTPUT WAS DROPPED ON A COUNT THAT MERELY WOBBLED.** A pair three apart
restarts the idle steadiness run, the state reads absent for the passes it
takes to re-earn, and the display aperture was closed on every one of them.
Measured: **111 blanks in 30 s with no arm and no install behind any of them**,
which is the panel flickering under a solve that never moved. The blank waits
`VideoSourceAcquisition::OutputBlankPasses` now, derived from
`MinimumSyncAwayMs`: `presentWhenSettled()` holds the pad away that long once
it goes, so a blank the engine will undo sooner shortens nothing and buys an
encoder relock.

Measured after all three, same source and output: 0 blanks, 1 arm and 1 install
in 50 s, and a picture in every frame `hdmi_capture.py` takes where one in
three was black. Guarded by `test_video_source_acquisition.cpp`, which fails
against each of the old behaviours.

### The field rate reads two thirds at the solved clock, where the reference reads it right

Measured 2026-10-12 on `ypbpr` at 480p, one boot, after the scan fix:

```
measured: 524 lines x 59.93 Hz -> line rate 31468, clock 2040@15625 htotal 1661 UNLATCHED
duty: 106 pulse / 1446 divider, htotal 1446, negative
measure: start, TIMINGS change (interrupt), 524 lines, solved 524
measured: 524 lines x 40.18 Hz -> line rate 0, clock 1446@31468 htotal 1446
measured: 524 lines x 40.01 Hz -> line rate 0, clock 1446@31468 htotal 1446
measured: 524 lines x 39.98 Hz -> line rate 0, clock 1446@31468 htotal 1446
```

40.0 Hz is two thirds of 59.93 — a missed vertical edge every third field — and
it is **steady at four digits across readings**, so no agreement test rejects it.
`rateFollowsCount()` refuses each against the correct count, which is the `line
rate 0`.

**It is the LATCHED clock that reads wrong and the unlatched reference that reads
right**, so the latch is not what the rate's correctness follows. The count is a
correct 524 throughout, so the sync processor sees the source while the edge at
`DEBUG_IN_PIN` does not arrive every field.

`SP_PRE_COAST` 7 / `SP_POST_COAST` 6 are in force, on sync on green with
`SP_SOG_MODE` 1. The composite post coast entry further down this page is what
decides whether the vertical blanking reaches the pin, and that is the untested
candidate. **A frozen comparison cannot be used**: freezing on this source takes
`STATUS_SYNC_PROC_VTOTAL` to 97..135 and both test buses time nothing, 0 in 60 of
60 readings on each.

### FIXED in part: a count that never steadies no longer stalls the engine

The recovery gates were opened for `Unmeasurable` with a non-zero rate, and that
is not the exit this state takes. With the rate reading correctly and the count
collapsed, `measureRate()` answers `NotSteady` every pass: `sampleSteady()`
refuses a count outside the source bounds and restarts the run, so the pass
returns before `measureLineRate()`, before any status the retry tested, and
without printing anything.

Measured: 45 s and 97 s windows of total console silence with `state: absent`, a
full solve held (`/geometry` capture 1447x522, `lineRateHz` 31288), `PLLAD_MD`
1446 latched against `STATUS_SYNC_PROC_HTOTAL` 1448 and
`STATUS_SYNC_PROC_VTOTAL` 98. `loop()` is running throughout -- HTTP field reads
answer, which are deferred to it. Reproduced in the host suite, which prints
nothing across the window either.

**The `NotSteady` exit reaches the recovery now.** What both exits share is the
discriminator: something is pulsing, so the signal is there and this clock cannot
count it. Guarded by `test_video_source_acquisition.cpp`, which fails against the
old condition.

**WHAT IS NOT FIXED IS THAT THE SOLVED DIVIDER GOES STRAIGHT BACK.** The retry
installs a reference and the next pass re-derives the solved divider from the
rate still held, so the clock the recovery chose survives one pass -- measured in
the host suite, the divider reads 2200 either side of a retry that demonstrably
ran. So the state is a cycle rather than a stall, and the cycle does not reach a
clock that can measure the source.

### The OLED cannot change input while the reference retry is cycling, and HTTP can

Observed 2026-10-12 on `ypbpr` at 480p, in the state the entries above leave:
the engine cycles the reference-clock retry about three times a second,
indefinitely, and the console repeats `measure: the reference clock measured
nothing, trying the next` and nothing else. **The OLED menu will not select
another input in that state.**

**`/input?src=vga` DOES work from there, and is the way out.** Measured from the
request: `input selected` at 0.4 s, the sync-type probe answering at 0.6 s,
`clock 2040@15625 htotal 2040` at 4.4 s, and `state: acquired` with a clean
picture at 7.0 s. So the selection path itself is sound and this is the OLED's
route to it.

**It is not a stalled `loop()`, which is the obvious reading and is excluded.**
The retry fires every `ReferenceRetryPasses` passes and the retries are ~300 ms
apart, so `loop()` is turning over at about 21 ms throughout. `/input` is queued
for `loop()` and is acted on, which says the same thing a second way.

`SyncProcessor::PathSettleMs` is 500 ms and `VideoPath::establishSyncType()`
does `delay()` for it, but it returns early unless the sync type actually
changes, so it is not spending the loop here. **What the OLED path does that
`/input` does not is unestablished.**

**THE PANEL STOPPING ON `Input RGBs` WAS A DIFFERENT FAULT AND IS FIXED.** It
was not a confirmation page and nothing was frozen: what the panel shows there
is the DESCRIBED tree's Input page, which `Osd::Panel` renders as the level and
the selected label alone and so is indistinguishable from the icon tree's
confirmation. An open described menu shuts the `NEW_OLED_MENU` gate, and that
gate is what `oledNav`'s only consumer sits behind -- so the encoder reached
nothing while the tree went on painting the panel, and the sketch's 16 s timer
excluded exactly that menu. `Osd::Menu::IdleCloseMs` closes it now.
`investigations/the-panel-cannot-drive-the-tree-that-draws-on-it.md`.

**So this entry is the OLED tree's own refusal, and that is still open.** The
part measured on 2026-10-12 -- the icon tree declining to select another input
while the reference retry cycles -- is untouched by the fix above, which
addresses neither tree's handling of a selection.

**What the panel still cannot do is DRIVE the described tree.** The encoder has
three gestures and the tree needs Up, Down, Ok and Exit, so routing `oledNav`
to whichever tree owns the gate wants a Back row on every page or a long-press.
That is a design decision rather than a defect.

Free heap reads 9720 in this state, below the 10000 `/fs/*` requires -- so
`/fs/download?f=/preferences.txt` refuses and the input cannot be read back --
and above the 8000 the console broadcast needs.

### An input with nothing plugged into it is never left again

Selecting an input that carries no source leaves the unit with no picture
indefinitely: nothing times out, nothing sweeps, and the engine runs its
recovery ladder on an input where no recovery can succeed. Reached at the bench
by picking RGBs with nothing on the connector. The OLED entry above used to
make it unrecoverable from the panel; the described menu closes itself now, so
the panel comes back to the icon tree on its own.

**An explicit selection is a command and stays one** -- it is honoured whether
or not a signal arrives, because a user choosing an input with the source not
yet powered must not have the choice taken away. What is missing is the bound:
after a source has failed to acquire for long enough that it is not arriving at
all, the engine should sweep the inputs until one carries a source, trying the
input that last carried one first.

`applySavedInputSource()`'s `default:` branch has the same hole from the other
end -- nothing meaningful saved sends no frame and sweeps nothing.

### The sync-type probe cannot say yes once the separator is in the path

**`hasOwnVsync()` clears `SP_EXT_SYNC_SEL` and reads
`STATUS_SYNC_PROC_VSACT`, and that bit reports the sync path in force rather
than the source.** With the separator still in circuit -- `SP_SOG_MODE` 1 and
`ADC_SOGEN` 1 -- it reads 0 whatever the source drives, so the probe answers
`own V sync: no` and the engine holds a separate-sync source as composite.

Measured 2026-10-11, the RiscPC at `MODE X320 Y256 C256 F50`, `SYNC 0`,
confirmed emitting, after a session on `ypbpr`:

| | |
|---|---|
| probe | `own V sync: no after 1000..1002ms`, every attempt |
| held | `SP_SOG_MODE` 1, `ADC_SOGEN` 1, `SP_EXT_SYNC_SEL` 1 on the VGA input |
| count | 303, 328, 316, 317, 315, 318 at ~50 Hz -- read, dithering, never settling |
| `STATUS_SYNC_PROC_VSACT` | 0 |

**The same source and mode answered `own V sync: yes after 2ms` earlier the same
day, from a boot on which the separator had never been enabled.** So the
discriminator is not the source: it is whether anything has put the separator in
the path since the chip was powered.

**IT IS SELF-LATCHING AND NOTHING REACHABLE CLEARS IT.** Tried and failed:
`/sc?~` three times, `/restart`, re-issuing the source's `MODE`, an input bounce
through `rgbs`, and clearing `SP_SOG_MODE`, `ADC_SOGEN` and `SP_EXT_SYNC_SEL` by
hand -- detection's own separator sweep re-enables them while it hunts, so a
hand-cleared state does not survive to the next probe.

**FIXED, AND THE FIX IS WHAT RECOVERED THE LATCHED STATE.** `hasOwnVsync()`
now puts the processor in the DEFINED separate-sync arrangement for the
measurement -- `SyncProcessor::applyForSyncType(false)`, one owner rather than
a field cleared by hand -- and restores the arrangement held afterwards, which
the caller re-applies from the answer. Measured on the unit, from the state
above that `/sc?~`, `/restart`, a source re-issue, an input bounce and three
registers cleared by hand had all failed to clear:

```
own V sync: yes after 13ms
own V sync: yes after 3ms
sync type: 0/3 field rate probes plausible, own V sync yes -> separate H/V
sampling: 311 lines x 50.08 Hz -> line rate 15627
sync pad: driven
```

No power cycle. `STATUS_SYNC_PROC_VSACT`'s entry below stands: clearing
`SP_EXT_SYNC_SEL` alone does not revive the bit, and that is why the whole
arrangement is what the probe puts in force.

**CORROBORATED ON THE PATH THAT USED TO FAIL.** A `vga` -> `ypbpr` -> `vga`
round trip, the RiscPC held at `MODE X320 Y256 C256 F50` `SYNC 0` throughout,
answers `own V sync: yes after 2ms` on the return and acquires -- 311 lines at
50.08 Hz, frame time lock converging. The input change was the one path the
probe was last seen failing on, so the arrangement the probe puts in force is
what carries it there too, not only out of a latched state.

### An input change out of RGBHV bypass never completes detection

**Selecting a different input while the output is in RGBHV bypass leaves the
output in bypass and detection unfinished.** Measured 2026-10-11, the RiscPC on
`vga` at 1280x720 in bypass (`scale-rgbhv = 0`), `/input?src=ypbpr`:

```
input selected: ypbpr, reference divider 2040, applied +68ms
source moved: input (0 lines, solved 749)
sync pad: away
DETECT: enter t=900902ms ADCsel=0 S16=0x01 srcVT=0 HPERIOD=511
evt,900903,det enter,0
                       <- nothing for the remaining 37 s
```

`DAC_RGBS_BYPS2DAC` and `OUT_SYNC_SEL` both stay 1 throughout, so the route is
still bypass; `/geometry` reports `lineRateHz` 45013, which is the **vga** rate,
with `present: false, state: absent`.

**The arm is not what fails, and an earlier reading that the engine reports the
previous source as acquired and never re-detects is withdrawn.** The selection
is noticed, the pad goes away and detection ENTERS -- it is detection that does
not come back, and the verdict is absent rather than acquired.

**What holds the bypass is the order in `runPass()`:** the route leaves bypass
at `setOutputMode(carriedResolution())`, which sits past `reading(...)`, so it
is reached only once the ARRIVING source has been measured. A source that cannot
be measured therefore leaves the departing source's timing on the encoder for as
long as it takes, and bypass passes that timing straight through.

A `/restart` recovers it: the next boot selects `ypbpr` from the saved
preference and reads `524 lines x 59.93 Hz` at the reference divider, so neither
the board nor the source is at fault.

**IT IS NOT A BYPASS-ONLY FAULT.** The same input change out of a SCALED vga
state -- 320x256@50, acquired, full geometry -- also reaches `DETECT: 27ms,
syncFound 2` and then produces no reading at all. So bypass is one state it
happens in rather than the cause.

**AND THE SYNC TYPE LATCHES IN THE OTHER DIRECTION TOO.** Coming back
`ypbpr` -> `vga` leaves the separator in circuit on the VGA input --
`SP_SOG_MODE` 1 and `ADC_SOGEN` 1 -- so hsync is sought on green, where the
RiscPC puts none, and `STATUS_SYNC_PROC_HSACT` reads **0** with
`own V sync: no after 1000ms` on a source whose sync type is separate.
**`/sc?~` did NOT clear it, twice**, which contradicts the recovery this page
and CLAUDE.md both record for the same signature; a `/restart` cleared it at
once and the source came back in about twelve seconds.

### The default framing loses the source's outermost COLUMN on every mode

**`PATTERN CARD`'s one-pixel green frame does not reach the emitted frame at the
left, on 28 of 28 stock AKF50 modes.** It reaches it at the right, at the top and
mostly at the bottom. Measured with `card_edges.py` against the USB capture:
`across` reports the near edge off the frame on 26 of the 28, and `down` reports
flush or two columns on most.

**That sweep is SEPARATE SYNC ONLY and it carries no sizes**, both of which the
two subsections below supply: the displacement reverses sign on composite sync,
and four of the six covered modes are flush on separate sync once the allowance
covers the solve's own run-to-run spread. The margins recorded further down this
entry stand as readings; the verdicts beside them were taken against an
allowance of one output pixel.

The picture IS in memory and our own display blanking is what cuts it. Frozen at
640x480@60, walking `VDS_DIS_HB_SP` down from the solved 160:

| `VDS_DIS_HB_SP` | green dominance, emitted columns 0..13 |
|---|---|
| 160 (solved) | nothing -- black to column 7, then a grey ramp into the white band |
| 158 | nothing |
| 156 | 1.2 at column 3, **7.0 at 4..7** |
| 150, 140 | the same, plus 0.5 at 0..3 |

The right-hand edge reads **114** on the same frames, so the near edge is not
merely smeared -- `docs/investigations/full-screen-framing-on-the-vesa-modes.md`
records 43/73/51 across the first six columns at this mode, and it now reads 0.4.

**A frozen poke cannot place the picture on the emitted frame**, and this mode is
the one that trap was measured on, so the frozen walk says where our blanking
falls and not where the sink cuts. What stands on its own is the sweep, which
re-acquires per mode.

**The cheapest candidate is `Axis`'s capture margin, which is 1 horizontally and
2 vertically.** The vertical 2 was measured for this exact symptom -- the card's
green frame not reaching the panel from the engine's own window and reaching it
from one unit earlier -- and the vertical axis is the one that passes. The margin
is charged twice over, so raising it moves both edges: the capture opens a unit
earlier and `pictureOffset` puts the aperture a magnification later, which is
what makes the source's first pixel FULLY written rather than partly.

**The capture margin at 2 does not close it, and improves two modes.**
`AxisHorizontal`'s margin raised from 1 to 2, built, flashed and judged by
`test_card_framing.py`: still 6 of 6. Both columns are read on a FLASHED unit,
because a reflash is a reboot and a reading taken before one does not compare
with a reading taken after it:

| mode | margin 1 | margin 2 |
|---|---|---|
| X240 Y352 F70 | across -, down (2,2) | across -, down (2,2) |
| X640 Y480 F60 | across -, down - | across -, down - |
| X640 Y512 F50 | across -, down (0,0) | across (227,254), down (0,0) |
| X800 Y600 F60 | across -, down (0,0) | across (2,0), down (0,0) |
| X1600 Y600 F60 | across -, down (0,0) | across -, down (0,0) |

A dash is `card_edges` reporting the window off the picture rather than a margin
it could measure. **The effect is horizontal only**, which is the axis the margin
names: two modes go from an edge of the card missing from the emitted frame to
the whole card on it, and `X800 Y600 F60` lands at 2 against an allowance of 1 --
one unit short. Nothing vertical moves.

**The margin is left at 1 pending that one unit.** It closes no mode, and it
costs a dozen host expectations that encode the window arithmetic -- among them
`test_capture_window.cpp`'s tripwire on the constant and concrete `IF_HB_SP2`,
`VDS_HB_ST` and scale-register values. Raising it is a change to make with those
re-derived, not alongside them.

**`X320 Y256 F50` is not a usable case for this test.** It reports "neither edge:
the window is narrower than the picture" on one run and a measured margin on the
next, on identical firmware, where the other five repeat exactly. Judge a framing
change on the other five.

**What is left**: re-measure the write-start constants with `measure_origin.py`
-- `55 + 25m` is fitted and the aperture is floored from it.

#### The displacement reverses sign with the sync type

**Separate sync loses the NEAR edge and composite sync loses the FAR one, on
every mode either can be read at.** Measured over two full runs of the matrix at
1080p with the shape off and the framing reset, `card_edges.judge()` reporting
each edge's own margin and the size it is out by. **This is the state BEFORE the
polarity correction followed the path**; the entry below carries what the
composite column reads now.

| mode | separate | composite |
|---|---|---|
| 240x352@70 | near edge off, far 10 | near 14, far edge off |
| 320x256@50 | flush within the allowance | overruns both ends |
| 640x480@60 | near edge off, far 12..15 | overruns both ends |
| 640x512@50 | flush within the allowance | 17 rows at the top |
| 800x600@60 | near edge off, far 0 | near 5, far edge off |
| 1600x600@60 | flush within the allowance | near 2, far edge off |

A shift moves a picture's two ends together, so the column is ONE displacement
per sync type rather than six modes misframed independently.
`VideoSourceLine::SeparatorPulseEndLeadSamples` is the only composite-specific
term in the horizontal origin that SCALES with the source's pulse -- it makes
the displacement `sync width - 66` where a constant delay would not -- and
`SyncProcessor::InvertedPulseWidthSamples` is the other one, five flat samples,
which the entry below is about.

**THE SEPARATE COLUMN'S SPREAD SWAMPS A FEW PIXELS ON THE SHORT LINES, so judge
a horizontal change on 800x600@60.** Two runs on one firmware
give 240x352@70 out by 2 and then by 10, where 320x256@50 repeats at 2 and
800x600@60 at 1 and 2. The ordering tracks the magnification, so the quantity is
the source's own pixel rather than the emitted frame's.

**THE MEASURED POLARITY FLIPS WITH THE SYNC TYPE, AND THAT IS MEASURED.** One
source mode, both sync types, every field read by name in one pass with
`sync_polarity.py` -- 800x600@60 on `vga` into 1080p:

| | `HSPOL` | `VSPOL` | low | pulse | folded | `HS_INV` | `SOG_MODE` | `RT_HS_SP` | `IF_HB` start |
|---|---|---|---|---|---|---|---|---|---|
| separate | **1** | 1 | 164 | 164 | **159** | 1 | 0 | 1247 | **274** |
| composite | **0** | 0 | 162 | 162 | **162** | 0 | 1 | 1244 | **177** |

The RISC PC's composite sync is a NOR, so the same mode presents high-active on
its own hsync line and low-active through the separator.

**THE SEPARATED PATH IS WHAT DISCRIMINATES THE TWO MODELS, and the correction
follows the path now.** The seven direct-path modes
`SyncProcessor::InvertedPulseWidthSamples` was fitted on could not tell the
source being high-active from the count having been taken through
`normaliseHsyncPolarity()`'s inversion, because on a direct path those are ONE
condition. A NOR composite sync is where they come apart -- the source is
high-active, the separator presents it low-active, `SP_HS_INV_REG` reads 0 and
the inversion is out of circuit -- and the capture window still wants the term.
`hsyncPulseSamples()` therefore takes it off a high-active reading OR a
separated one: the measured bit answers where it describes the source, and the
path answers where it cannot.

**The two composite-specific terms are told apart by size.**
`VideoSourceLine::SeparatorPulseEndLeadSamples` accounts for the 97 units
between the two capture starts, 274 against 177, which is the sync width less
66. What was left is the 3 samples of polarity -- and at this mode's 1.43 output
pixels per capture unit that is the 5 columns the composite framing was out by
at the near edge.

| composite, 1080p, shape off, framing reset | before | after, three runs |
|---|---|---|
| 240x352@70 | near 14, far edge off | 2 to 4 across, 2 to 5 down |
| 320x256@50 | overruns both ends | near 16, three times |
| 640x512@50 | 17 rows at the top | 8 across, 1 down twice and 29 once |
| 800x600@60 | near 5, far edge off | far 2, 3, 3 -- it passes |
| 1600x600@60 | near 2, far edge off | far 6, 7, 8 |

**The vertical readings are the ones that still swing**, 1 and 1 and 29 on
640x512@50 and 2 to 5 on 240x352@70, on an axis the correction does not touch
-- `capturableFrame()` takes no polarity. So they are the spread this matrix
already carries rather than anything the horizontal change reached.

**IT COSTS THE KEY COLLISION, AND THAT COST IS NOT A FRAMING DEFICIT.**
800x600@60 and 1600x600@60 are one `SourceKey` -- same field rate, same line
rate, both 75.8% active -- so ONE solve frames both, and the two want
placements about 5 output pixels apart. The move takes 800x600@60 from 5 to 2
and 1600x600@60 from 2 to 6..8.

**A CLASH CANNOT BE OPTIMISED FOR**, so neither reading is a target: no framing
suits the pair better than it suits either, and whichever of the two the solve
is tuned for reports the other as a deficit. `test_card_framing.py` therefore
does not carry 1600x600@60 in its default set -- it is also an outlier of a
resolution, and not the one to weight the defaults toward. `--all-modes`
reaches it, with an entry saying the reading is the clash.

**AND THE DUTY MOVES WITH IT.** 5 samples of the divider is 0.0023 to 0.0037
against `SourceKey::SyncWidthIdentity` of 0.002, so a composite framing stored
by an earlier build is keyed differently and the computed default answers for it
instead.

**640x480@60 ON COMPOSITE DOES NOT ACQUIRE**, measured in the same pass -- 90 s
and `never acquired` -- and again on every matrix run since, where the state
line reads `PLLAD_MD` 2040 against a 2041-unit capture and a frame count of 312
on a mode that sends 525. Its cases all read the picture overrunning both ends,
which is the absence of a picture rather than a framing, so that case is an
acquisition entry and not a framing one.

#### A flush margin does not mean the outermost pixel is whole

**`card_edges` CANNOT TELL FLUSH FROM CLIPPED, AND THAT IS THE GAP.** The margin
it reports is the first green run's index, so a border sitting wholly on row 0
and one half off the frame above it both report 0. The acceptance the bench is
judged by -- the card's green frame visible on all four edges -- is therefore
not what the number asserts.

**WHAT TELLS THEM APART IS THE BORDER'S INTENSITY AGAINST ITS OWN PEAK.** The
frame is one SOURCE pixel, which is 4.21 output rows at 320x256@50 into 1080p,
so a partly-shown border reads as a fraction of a whole one. Measured on `vga`
at 320x256@50 into 1080p, 16:9 declared, framing reset, each position's greenest
moment over 40 frames:

| edge | profile | reading |
|---|---|---|
| top | rows 0,1 at 0; row 2 at 74.3; row 3 at 177.2; rows 4,5 at 210.1 | 2.65 rows of black, then the whole border |
| bottom | rows 1073..1078 at 0.8; row 1079 at **91.2** of 210 | 43% of one row, so **3.6 rows are off** |

**THE SINK'S LATCH IS RULED OUT ON THE VERTICAL.** Frozen, with every output
register read identical either side of a `PAD_SYNC_OUT_ENZ` toggle -- both
display windows, both apertures, both scales, both rasters -- the horizontal
green ramp MOVES (near 67.6, 71.4, 150.0 to 41.8, 53.2, 140.7) while the
vertical holds to a tenth (bottom 91.2 both times). So the right border landing
on a fraction of a column is the dongle re-placing its window, and the bottom
being short is in the emitted frame.

**BOTH MODES CLIP IT, WHICH IS THE PAIR THAT SETTLES IT.** The comparison
belongs at 1080p -- below it an undoubled 512-line source barely magnifies, and
at 480p it MINIFIES, which the VDS cannot do -- so both modes read there with
the shape off and the framing reset:

| mode | rows per source pixel | top | bottom |
|---|---|---|---|
| 320x256@50, doubled | 4.22 | rows 0,1 black, then 74, 178, 210, 210 -- whole | row 1079 at 91 of 210, **43%** |
| 640x512@50, undoubled | 2.11 | row 0 at 85 of 187, **45%** -- clipped above | row 1079 at 91 of 187, **49%** |

The undoubled mode clips BOTH ends. And what the two share is the amount in
OUTPUT ROWS rather than in source pixels -- 0.43 of a row against 0.49 -- which
is what says the quantity belongs to the raster's end and not to the source.

**IT IS NOT THE LINE-DOUBLED PATH, AND IT IS NOT A PLACEMENT TERM.** Swept with
`framing_sweep.py` over one doubled and one undoubled mode at four outputs, top
black in output rows:

| mode | 480p | 576p | 720p | 1080p |
|---|---|---|---|---|
| 320x256@50, doubled | 3 | 1 | 4 | 2 |
| 640x512@50, undoubled | 35 | 3 | 2 | 0 |

The 480p undoubled case is MINIFICATION -- 512 source lines into 480 rows, which
the VDS cannot do -- and carries no weight. What is left has no ordering by
doubling and none by magnification: at 576p the undoubled mode has the larger
margin. So `VideoSourceLine::DoubledFrameOriginUnits` is not implicated, and
neither is the per-magnification write origin.

**WHAT IS LEFT IS THE ROUNDING, AND IT IS A TRADE RATHER THAN A DEFECT.** The
solve fills the raster to within 0 to 4 output rows on every state measured,
which is `card_edges.SPREAD`. At that precision the source's outermost pixel --
several output rows wide under magnification -- necessarily lands partly off one
end or the other. **Guaranteeing the whole border means insetting the picture by
up to one SOURCE pixel**, which buys a black margin of that size, and
[[bias-wider-never-narrower]] is the standing rule it would cut against.

**THE INSTRUMENT SAYS IT NOW.** `card_edges.Judgement.whole` is a fraction per
edge -- 1.00 clear of the frame's edge, less running past it -- reported beside
the margin and asserting nothing. `down 0 (0|0) out by 0 whole 0.45|0.49` is
the shape of the gap: a verdict reading as perfect on an axis where both
borders are more than half gone.

**THE ERROR DIRECTION IS DECIDED: KEEP THE PIXEL.** Where the choice is between
clipping the source's outermost pixel and leaving a sub-pixel margin, leave the
margin. What is NOT decided is whether to pay an inset of up to one source
pixel for it, and that is what has to be settled before `whole` becomes an
assertion rather than a reading.

#### The composite column is not reproducible, and the separate one is

**Four full runs of the matrix on one firmware, same output, same shape
setting, framing reset per case.** The two sync types are not equally steady and
the difference is two orders of magnitude:

| mode | separate, four runs | composite, four runs |
|---|---|---|
| 240x352@70 | far 10, flush, far 2, far 10 | near 14 on all four |
| 320x256@50 | flush on all four | both ends, both ends, **307\|222**, near 28 |
| 640x480@60 | far 12, 15, 12, 13 | both ends on all four |
| 640x512@50 | flush on all four | 17 down, both ends, **161\|168**, 12 down |
| 800x600@60 | far 0, flush, flush, flush | near 5, flush, 19 down, near 4 |
| 1600x600@60 | flush on all four | near 2, flush, flush, flush |

**Separate sync repeats to a few pixels**, and what spread it has grows with the
magnification -- about 8 output pixels on the 240-pixel source line, 3 on the
640-pixel one, under 1 on the 1600-pixel one -- so the quantity that moves there
is the source's own pixel or the capture's unit rather than the emitted frame's
pixel. The green frame's captured width is too noisy to settle which: one clip
at a magnification of 4.5 gives 4 columns at one edge and 8 at the other.

**Composite sync does not repeat at all.** A mode reading the picture
overrunning both ends in two runs and a margin of 307 columns in the third is
not a framing spread; a margin that size is a picture far narrower than the
raster, which is a SCALE and not a placement. Two candidates, and no run so far
records which:

- the composite acquisition landing in a different state. 320x256@50 on
  composite is the entry above that can miss the source entirely, with the
  divider left on another mode's value, and a wrong divider gives a wrong
  capture and so a wrong scale
- the declared shape coming back. `reset_framing()` resets the declared ASPECT,
  it runs once per case, and the pillarboxed bars are our own blanking -- so a
  case judged with a shape applied measures the bars

**THE SHAPE IS REFUTED.** `PLLAD_MD`, `shaping`, the declared aspect and the
capture are read beside every verdict now, and a fourth run has them on all
twelve cases: `shaping False` on every one, with `aspect 13333` declared
throughout. So the declaration does come back under `reset_framing()` and the
solve does not apply it, which leaves no bars of ours at any edge.

**What is left is the acquisition's state, and the run where every divider was
mode-correct carries no scale at all.** The same fourth run: `MD 2200 ch 1101`
on 320x256@50, `MD 1342 ch 1343` on 800x600@60, `MD 1424 ch 1425` on
640x512@50, `MD 1272 ch 1273` on both 31.5 kHz modes -- every one the value its
mode wants. Its composite readings are 28 at the near edge on 320x256@50, 12
either side down on 640x512@50 and 4 at the near edge on 800x600@60, all of them
placements of a few to a few dozen columns. The 307 and 161 margins belong to a
run with no divider recorded beside them, which is the reading the state line
exists to catch.

So the composite column's size is a placement where the divider is right, and a
reading without the divider beside it is not evidence about either.

**AND A RUN HAS NOW CAUGHT THE WRONG STATE AND THE WILD READING TOGETHER**,
which is the positive form of the same finding rather than an absence. One run
of the reduced matrix, state line beside every verdict:

| composite case | reading | state | the mode wants |
|---|---|---|---|
| 320x256@50 | across 230, down 53 | `MD 2200 cv 536` | `cv 624` |
| 640x512@50 | neither edge, either axis | `MD 1746 ch 1747` | `MD 1424 ch 1425` |
| 640x480@60 | neither edge, either axis | `MD 2040 cv 268` | never acquires |
| 800x600@60 | far 4 | `MD 1342 ch 1343 cv 628` | all three right |

The divider is on another mode's value where the reading is wild and on its own
where the reading is a placement, in ONE run -- and 800x600@60 is mode-correct
and reads 2, 3, 3 and 4 across four runs. So the composite column's
irreproducibility is the acquisition, not the framing, and a verdict is worth
reading only where the state line says the capture belongs to the mode asked
for.


**The measurement's own order is ruled out.** The concern was that
`card_edges.py` changes the mode, which re-acquires, and THEN resets the framing,
so where a framing was stored for that key the sink placed its window before the
framing moved. Resetting first and re-locking with `gbs_unit.mode_round_trip()`
does not move the verdict: each order repeats exactly over two runs, and the two
orders agree on all five usable modes. What the test reports is the window the
engine solved.


### A black HDMI frame with the acquisition healthy is `DAC_RGBS_S1EN` left at 0 -- FIXED

**Measured after a hardware suite run, on `ypbpr` with the source acquired.**
Everything the "no HDMI with every register perfect" checks ask for reads right:
`/geometry` says `state: acquired` and `present: true`, `s0_46` is 0x7f so no
block is held in reset, `s0_49` bit 2 is 0 so HSOUT/VSOUT are driven,
`DAC_RGBS_PWDNZ` is 1, `DAC_RGBS_BYPS2DAC` and `OUT_SYNC_SEL` are 0 for the
scaling path, and both scales are sane. The dongle locks and delivers 1920x1080
frames at **mean luma 0.00**.

**`s0_45` reads 0x01 where a working unit reads 0x11**, and the missing bit is
`DAC_RGBS_S1EN`. Writing it back restores the picture at once -- mean luma 97.7
over the full 1439x1080 -- and it then survives an input change:

```sh
python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set DAC_RGBS_S1EN=1
```

**It is the same byte as the yellow-tint entry and a different bit**, so a unit
emitting nothing and a unit emitting a yellow picture are one bit apart in one
register.

**It is the COLD BOOT state, not something a run leaves behind.** Measured on a
unit unplugged for 24 hours with no USB attached: `s0_45` comes up 0x01, and it
returns to 0x01 after a `/restart` as well. `DAC_RGBS_S1EN::write(1)` appears
once, in `doPostPresetLoadSteps()`, so the bit is only set once a source has
been acquired and a preset applied -- **a boot whose first acquisition fails
leaves the output DAC half configured**, and the state cannot be read as
evidence that anything cleared it.

**What separates it from the encoder causes is that the LINK IS UP.** The dongle
reports 1920x1080 rather than no signal, so the encoder is locked and being fed
black. A sink reporting no mode, or the old mode, is a different fault and the
`PAD_SYNC_OUT_ENZ` toggle is its answer, not this.

**It is found by diffing against a known-good dump**, which is the check that
works: 63 fields differ between a clean `vga` state and this one, and all but
this are the legitimate consequences of the input and source being different.

**The sync DAC is `Tv5725::Chip::init()`'s now**, written beside the colour
three rather than at the end of a preset load, so the boot path owns it:
`setup()` calls `BringUp::init()` unconditionally and before detection, and
every path that tears segment 0 down goes through `holdAllBlocks()`, which arms
the bring-up again. Nothing else writes `s0_45` bits 2..4.
`test_bringup.cpp` holds it. **The bench confirmation is a cold boot away** --
the fix is host-tested and has not been read off a unit that came up from cold
with this image.


### A stored framing from before shapes existed comes up unshaped

**The mechanism is one line and the decision is open.** `FramingLine::read()`
starts the shape at `Aspect::Fill` and leaves it there when a record ends after
the fourth number, which is every record written before the shape existed. So a
source carrying one comes up `aspect: 0` where the same source with no record at
all is defaulted from the raster it matched -- 13333 on the bench RISC PC -- and
nothing on screen says why two sources with the same timings differ.

**Both readings are defensible**, which is what makes it a decision rather than a
defect. Replaying `Fill` reproduces the picture the record was tuned against,
because filling is what the engine did when it was written. Defaulting from the
raster makes a stored framing describe the window alone and leaves the shape to
the source, so two units running one source agree whatever their file history.

**What each costs**: defaulting from the raster re-shapes every stored framing on
every unit at once, and the window stored beside it was tuned against the
unshaped picture. Keeping `Fill` leaves a shape the remote can reach but nothing
explains, `Aspect::next()` stepping an unrecognised shape onto the first of the
ring rather than reporting it.


### The zoom-shear guard cannot be proven, and its self-proof is a coin flip

**`test_zoom_shear.py::test_an_even_memory_window_is_what_the_score_can_see`
passes alone and fails in sequence**, because the fault it injects is not the
fault. It freezes automation and adds one to `VDS_HB_ST`, which is a hand-set
subset of a solve that also set the scale, the aperture and the playback fetch,
and measured off the USB capture that injection displaces **0 rows of 1077 at
every one of ten consecutive granules**. What `picture_shear` reports as a
separation at 3 of those 10 is quantisation: the clean states themselves span
`displaced` 0.0000..0.0029, every reading a multiple of one pair in 1047.

**The artefact does not reproduce with the bias removed either.** A build with
the odd-width bias disabled solves 14 even widths and 10 odd over 24 consecutive
granules, nothing frozen, and `displaced` is 0.0000 at all 24.
`investigations/horizontal-scale-corruption.md` has the readings.

So the guard has nothing to be credited against on this bench, and the suite
carries a test that fails about seven runs in ten for no defect. **The thresholds
are not the problem and must not be loosened** -- `CLEAN_DISPLACED` and
`CLEAN_ROUGH` are placeholders over a measured clean envelope of `displaced`
0.0000 and `rough` 0.0068..0.0074, and no faulted state exists to set them
against.

**What would settle it**: a source or magnification where an engine-solved even
width does shear, which is what the guard needs and what 24 granules at
320x256@50 over `VDS_HSCALE` 523..560 do not provide. Failing that, the decision
is whether the bias stays unguarded -- it costs the outermost column, and
removing it does not close the framing defect above.


### FIXED in part: sync on green did not follow the source until a recovery ran

**THE SUB-COAST WAITED ON A MEASUREMENT THAT NEEDED THE SUB-COAST, and that is
closed.** `SP_DIS_SUB_COAST` came from `lowLineRate() && isCsync()`, an
unmeasured rate answered false, and a selection has no measurement of the source
arriving -- so the arriving Wii got the outgoing RISC PC's answer, the sync
processor counted its serrations as lines, and no rate could be measured through
it. The sync type decides it now, measured harmless at 37.6 kHz and required at
15.7. `investigations/the-sub-coast-waited-on-a-measurement-it-made-possible.md`.

**And the dynamic configuration follows the measurement both ways.** The
take-back was one-way and latched, so a source that fell back into searching
kept the settled configuration and the coast inversion with it --
`SP_COAST_INV_REG` 0 with `STATUS_SYNC_PROC_VTOTAL` at 97.

**REFUTED: the separator level is not the fault.** `ADC_SOGCTRL` walked by name
one step at a time from `ComponentLevel` 14 down to 1 on a frozen failing unit
left `STATUS_SYNC_PROC_VTOTAL` at 97..102 and `STATUS_SYNC_PROC_HTOTAL` at
1107..1247 at every level. The earlier reading -- a hand-driven run where the
count came right once the level reached 1..2 -- does not survive a controlled
walk.

**WHAT REMAINS IS A WRONG SAMPLING GROUP, NOT A WITHHELD CONFIGURATION.** A full
1536-address `snapdiff.py` across an acquired and a failing leg shows the whole
sync arrangement byte-identical -- `SP_SOG_MODE` 1, `SP_EXT_SYNC_SEL` 1,
`SP_DIS_SUB_COAST` 0, coast 7/6, window 16/256, `SP_H_PULSE_IGNOR` 107,
`SP_DLT_REG` 192, `SP_H_TIMER_VAL` 58, `ADC_SOGCTRL` 14 -- with the difference
in the SAMPLING group: `PLLAD_MD` 2098 against the 2200 the source wants,
`PLLAD_KS` 0 against 2, `PLLAD_FS` 0 against 1, both decimators bypassed and
both clock enables off, which is oversample 1 against oversample 4.

**THE STALL IS A FREE-RUNNING ADC PLL, AND THAT IS NOW MEASURED RATHER THAN
INFERRED.** `Tv5725::SamplingLog` at 25 ms across one whole failing leg, 1057
samples: `STATUS_MISC_PLLAD_LOCK` 0 in **1057 of 1057**,
`STATUS_SYNC_PROC_HTOTAL` within 2 of the divider in **0 of 1057**, the count
97..102, and `VPERIOD_IF` a correct **524 throughout**. So the input formatter
counts the source's vertical perfectly while the sync processor has no ADC clock
to count. The same log on settled `vga` is the other way round: `HTOTAL` within 2
of the divider in 1182 of 1182.

**AND THE FIELD RATE IS TIMED OFF A PIN CARRYING NOTHING.**
`/samplinglog?rates=120`, which times it from `loop()` exactly as
`SourceMeasurement` does, gave **98 of 120 readings in 16.1..16.5 Hz** against a
source running 59.94, median 16.43, the rest 1144..8438 Hz. That is the band
`VideoSignal.h` records for a pin the source is not driving. Both failing and
ACQUIRING legs measure that noise, so a leg lives or dies on which sample is
accepted.

**ONE HALF IS FIXED: the crossover row was sized from the OUTGOING source.**
`PLLAD_KS` and `PLLAD_FS` come from the line rate alone, and
`VideoPath::inputTimingsChanged()` re-asserted the divider in force while
reading the rate off `SourceMeasurement` -- which on an arriving source is the
rate of the one that left. Measured in a stall: `PLLAD_KS` 1 at the reference
divider, which is 1400 x 37879 = 53 MHz, where the arriving 22 MHz CKO needs
row 2. `Adc` holds the rate its group was built from now and every caller hands
the rate in.

Twenty trips from an 800x600@60 `vga` predecessor, measured to `sync pad:
driven`, judged on the count first:

| | before | after |
|---|---|---|
| `ypbpr` shown a picture | 13 of 20 | **20 of 20** |
| of those, on the correct count | -- | **20 of 20**, `cv` 518/520/522 |
| inside 10 s | 2 of 20 | 4 of 20 |
| spread | 6.1..24.4 s, median 13.9 | 6.8..25.7 s, median 15.5 |
| `vga` | 20 of 20, median 6.1 s | 19 of 20, median 6.3 s |

**THE RELIABILITY MOVED AND THE CLOCK DID NOT, and the median moving the wrong
way is why that reads as a wash.** The seven legs that never acquired are now
slow successes, which lands them in the tail. The times cluster on the act
intervals: **4 legs on the selection alone** at 6.8..8.9 s, **15 after one
`Reconfigure` act** at 11.8..19.3 s, and one after two at 25.7 s. So three
quarters of legs are waiting out a ten-second timer for something
`reconfigureForSource()` does that a selection does not -- and the `vga` leg
that never acquired is one leg of twenty, not a pattern.

**THE OTHER HALF IS OPEN: a garbage rate is still accepted.**
`SourceMeasurement::rateSettled()` returns true once `RateAgreementAttempts`
readings have been taken whether any two agree or not, and at
`RateAgreementPerThousand` of 1 two noise readings never agree -- so on a source
whose pin carries nothing the attempt cap is always what accepts, and it accepts
an arbitrary sample. Measured on the boot after the fix landed: `PLLAD_MD` 1940
and `PLLAD_KS` 0 against `/geometry` `lineRateHz` 41808, a group built for 81 MHz
on a source producing 22 MHz. `/sc?~` cleared it in 15 s.
`investigations/the-crossover-row-was-sized-from-the-outgoing-source.md` carries
why `reconciledFrame()` cannot be the gate that refuses it.

**THE THIRD HALF IS FIXED: ONE REFERENCE CLOCK IS NOT ENOUGH.** The ten-second
timer three quarters of legs were waiting out is the recovery reaching a clock
the source can be measured through, and `reconfigureForSource()` could only get
there by accident -- it re-asserted the divider in force, so what it corrected
was the crossover ROW, and a reference divider the ADC PLL will not lock to on
the arriving source is not recoverable that way at all. Measured on the Wii at
480i: nothing is measured through the reference divider of 1400 for as long as
it is left, three selections out of three, 60 s and five acts with no
`sampling:` line printed. `Tv5725::Adc` holds the references as a sequence now
and the act installs the NEXT one. Twenty trips from an 800x600@60 `vga`
predecessor, same session:

**AND THE REFERENCE'S VALUE IS THE WHOLE OF IT: `BringUpDivider` IS 2040.**
Asked end to end, two trials a cell, the discriminator being that a leg inside
the budget was measured through the first reference while one at 11-13 s means
the escape rescued it:

| source | 1400 first | 2040 first |
|---|---|---|
| `ypbpr` 480i, sync on green | 11.5, 11.6 s -- rescued | **2.3, 2.6 s** |
| `vga` 800x600@60, 37.9 kHz | 5.6, 5.8 s | 5.5, 6.3 s |
| `vga` 640x480@60, 31.5 kHz | 4.7 s | 4.9, 4.6 s |
| `vga` 320x256@50, 15.6 kHz | 2.9, 3.2 s | 6.3, 5.7 s |

Twenty legs an input, same session, measured to `sync pad: driven`:

| | 1400 | 2040 |
|---|---|---|
| `ypbpr` shown a picture | 20 of 20 | 20 of 20 |
| `ypbpr` **inside 10 s** | **0 of 20** | **19 of 20** |
| `ypbpr` spread | 11.6..12.4 s, median 12.0 | **2.5..14.2 s, median 3.0** |
| `ypbpr` on the correct count | 20 of 20 | **19 of 20**, one 263-line solve |
| `vga` | 20 of 20, median 6.3 s | 20 of 20, median 6.2 s |

So `ypbpr` meets its budget for the first time.

**AND THE RECOVERY LADDER'S THREE ACTS WERE COSTING THE REST.** Retiring them --
one recovery in place of reconfigure, reset-the-blocks and move-the-input -- took
the same reference clock from 2.5..14.2 s to **2.5..3.7 s**, and the one leg that
had solved 263 lines against the source's 259/260 went with the outlier:

| | three acts | one recovery |
|---|---|---|
| `ypbpr` inside 10 s | 19 of 20 | 19 of 20 |
| `ypbpr` spread | 2.5..14.2 s, median 3.0 | **2.5..3.7 s, median 2.8** |
| `ypbpr` on the correct count | 19 of 20 | **20 of 20**, `cv` 520/522 |
| `vga` | 20 of 20, median 6.2 s | 20 of 20, median 6.3 s, `cv` 628 throughout |

Which is "a recovery fired into an acquisition that was working" seen from the
other end, and it is why the acts are gone rather than retimed. `vga` is
unmoved, and 3060 bytes of globals and 5.9 KB of flash come back.
`video-source-acquisition.md`, "Three acts went".

`rateSettled()` accepting an arbitrary sample once `RateAgreementAttempts` is
reached remains open and is the next thing to reach for if a wrong count
returns. `investigations/the-reference-clock-can-deadlock-the-measurement.md`.

**A GROUP RE-ASSERT PLUS THE BLOCK RESETS RECOVERS THE STALL FROM THE ESP IN
ABOUT TWO SECONDS**, which no act of the ladder achieves in 330 s:
`/sampleclock?md=1400&os=4` -- the divider already in force -- took a 26 s stall
to `state: acquired` in **1.9 s**, and `md=2200&os=4` took a 330 s stall in
5.4 s. Both landed on the count 259/260, `HTOTAL` 2200 against a 2200 divider
and a measured 59.93 Hz.

**NEITHER FIGURE TRANSFERS TO ANOTHER STALL, because the route's crossover row
comes from the HELD rate rather than from the divider asked for.** The same
`md=1400` installed `PLLAD_KS` 1 in a stall holding vga's 37879 Hz where a
reference install at 1400 holds `KS` 2, and it recovered nothing there -- one
garbage reading in 30 s and still absent. Say which rate was held, or the
command names a different group each time.

### The old reading, which the fixes above supersede in part

**Fifteen seconds of a `ypbpr` acquisition are spent with the ADC PLL already
locked and the sync processor counting 97 lines.** The teardown repair means the
chip is measurable 1.6 s after the selection -- `STATUS_SYNC_PROC_HTOTAL` equals
`PLLAD_MD` at the reference divider of 1400 and every block reset is released --
and `STATUS_SYNC_PROC_VTOTAL` still reads 97, which is the value it holds when
it is not following the source at all.

Measured on the bench, Wii in 576i on `ypbpr`, with the repair in:

| t | console |
|---|---|
| 0.73 s | `DETECT: 22ms, syncFound 2` |
| 9.58 s | `recovery: lift SOG floor at pass 2` |
| 15.11 s | `recovery: coast window at pass 8` |
| 15.24 s | `recovery: sync processor dynamic at pass 27` |
| 15.64 s | the first `sampling:` line, and it is garbage -- `300 lines x 88.27 Hz` |
| 19.93 s | `recovery: restart sampling clock at pass 60` |
| ~29 s | acquired |

**Nothing between 0.73 s and 9.58 s prints**, because the count is not steady
and the engine pays for no rate measurement. Only two passes complete in those
nine seconds.

**~~The rungs that help are all gated behind `FirstAcquisitionGraceMs`.~~**
**~~Fixed, and it was not the whole fault.~~** **SUPERSEDED: the configuration
is the SELECTION'S and no recovery is waited on for it.**
`SyncProcessor::Dynamic` carried `searching` and `hunting`, and the selection
asked for the lesser answer -- the pulse-width difference alone -- so the only
writer of the coast window on a source nothing could count was a rung. Nothing a
caller knows could decide between the two, so there is one answer now: a
searching source gets the search configuration, and
`takeBackSearchSettings()` takes it back on the first count. The grace is gone
with the pass count; the three acts are ordered so the one that disturbs nothing
comes first.

**THE WALL CLOCK ON `ypbpr` DID NOT MOVE, AND THE REASON IS THE COUNT.** The
early rungs get the source producing readings and the readings are wrong:
**270/271 lines at 60.1..61.5 Hz**, where the Wii in 480i is 259/260 at
59.94 Hz. A divider is installed from one of them, `rateFollowsCount()` then
answers `line rate 0` to every correct reading, and the ladder climbs to
`FullReset` at 28.35 s, which clears it in under a second -- `source acquired:
260 lines` at 30.0 s. So the remaining cost is a sync processor counting the
wrong number, not a setting that was withheld.

**`RestartSamplingClock` at 60 is not it either**, measured again here: it fired
at 19.16 s and the count stayed 270/271. Only the block resets inside
`FullReset` move it, which is the same conclusion the YPbPr first-pass entry
reached by another route.

**Do not re-file this as the grace.**

**THE COUNT IS WRONG BECAUSE THE ADC PLL IS FREE-RUNNING, AND THE GROUP IS NOT
AT FAULT.** Measured across the stall and the healthy state that follows it, on
one acquisition: every field of the PLL group is **byte-identical** --
`PLLAD_MD` 2200, `PLLAD_KS` 2, `PLLAD_CKOS` 0, `PLLAD_ICP` 6, `PLLAD_FS` 1,
`PLLAD_VCORST` 0, `PLLAD_PDZ` 1, both decimators and both clock enables, and
`IF_HSYNC_RST` 1100. The only differences are measurements:

| | stalled | healthy |
|---|---|---|
| `STATUS_MISC_PLLAD_LOCK` | **0 of 12 reads** | 8 of 12 |
| `STATUS_SYNC_PROC_HTOTAL` against the 2200 divider | **3241..3252** | 2200 |
| `STATUS_SYNC_PROC_VTOTAL` | 270/271 | 259/260 |

A count over a thousand out is the honest witness here, and it says the PLL is
free-running rather than mis-set. `VPERIOD_IF` holds a correct **524**
throughout, so the input formatter is counting the source's vertical perfectly
while the sync processor's horizontal is nonsense -- the configuration is right
and the loop has simply not captured.

**NO SINGLE ACT OF `FullReset` RECOVERS IT.** Each applied by hand in the stall,
with the ladder's next disruptive rung still seconds away, and the lock read
back over the following eight seconds:

| applied | lock | `HTOTAL` | count |
|---|---|---|---|
| `SFTRST_SYNC_RSTZ` pulse | 0 throughout | 955..3977 | 265..277 |
| `SFTRST_MODE_RSTZ` pulse | 0 throughout | 1516..3248 | 264..277 |
| `ADC_SOGCTRL` 13 -> 14 | 0 throughout | 1516..3252 | 270/271 |

So the recovery is the SEQUENCE rather than any one of its acts, and the
`SFTRST_SYNC_RSTZ` refutation above now holds in this state as well as in the
97-line one. `ADC_SOGCTRL` moving 13 -> 14 shortly before the count comes right
is a coincidence of the ladder's order, not the cause.

**~~What is left to try: the acts in combination.~~ FOUND, and it was
`SP_H_PULSE_IGNOR` with two writers.** The PLL free-ran because the sync
separator was fed a threshold chosen for another source, so the hsync reaching
the ADC PLL was not a usable reference. Two defects, both fixed:

- `SyncProcessor::applyForSyncType()`'s **csync branch never wrote the field**,
  and the separate-sync branch does -- so a source selected after a
  separate-sync one ran on 0xff, the value that branch wrote.
- `applySeparationThresholds()` **wrote it back from a serration it could not
  know**, the outgoing source's. Measured over three selections from the bench
  mode: 0x6b at 1.6 s on all three, and on one of them 0x02 at 3.3 s, after
  which the lock read 0 and the count went to 270/271 for the rest of the
  window. **That is this fault's intermittency, and it was a second writer
  rather than a flaky part.**

Measured after, six round trips from the bench mode: **6 of 6, 2.9..4.4 s**.

**A SINGLE-FIELD POKE IN THE STALL DOES NOT SHOW THIS, which is why the
refutations above stand and are not evidence against it.** Writing 0x6b into a
live wedge leaves the count at 270/271 -- the threshold has to be right BEFORE
the separator tries to lock, not after. Preventing a wedge and recovering one
are different questions, and only the first is answered here.

**~~WHAT REMAINS IS WHICH csync VALUE A SELECTION WRITES.~~ SETTLED: THERE IS
ONLY ONE.** The threshold no longer asks about serration at all, so a selection
has nothing to read off the source it is leaving. Two measurements settled it,
both frozen with a control and with the value in force before the separator
re-locked: on the RISC PC's unserrated composite sync at 37.9 kHz, 107 and 2
both count 623 against a 1438 divider, and at 15 kHz the same comparison is
equally inert. So `UnserratedPulseIgnore` is deleted and a composite source
takes the serrated value, which is the one the Wii needs.
`investigations/the-pulse-ignore-value-is-measured-not-chosen.md`.

**The sub coast therefore needs no split.** It is the only reader of the
serration left, which is the job it had before one boolean carried two facts.

**A SEARCH VALUE WAS THE THIRD WRITER, and it was the expensive one.**
`applyForSearch()` wrote 2 while nothing was counting. The count that ends the
search is also what stops anything writing the field, so that value is what the
source is then READ with: measured on a selection from a 37.9 kHz predecessor,
the field held 2 from the first count for **thirteen seconds**, the separator
reading 271 lines against the source's 260 and `STATUS_SYNC_PROC_HTOTAL` 3244
against a 2200 divider. The search takes the sync type's value now, and that
state does not occur -- `HTOTAL` stays within about 2% of the divider from the
first count.

**WHAT THE THRESHOLD BOUGHT IS THE RIGHT COUNT, NOT A SHORTER WAIT, AND A TIME
COMPARISON HIDES THAT.** Both builds flashed in turn, the RISC PC held at
800x600@60 on `vga`, six `ypbpr` selections each, timed to `state: acquired`:

| | 5bda6ff7e | with one threshold per sync type |
|---|---|---|
| settled on 271/272 lines | **4 of 6** | **0 of 6** |
| settled on 260/261 | 2 of 6 | 5 of 6, the sixth 264 |
| time | 2.0 .. 46.5 s | 24.1 .. 25.4 s |

**The times above are to `state: acquired` with `/geometry` `cv` inside
500..545, which is LOOSER than the acceptance test's criterion** --
`test_acquisition_time.py` waits for the rate to come within
`RATE_TOLERANCE_HZ` of the rate it learned for that input, and under that one a
leg still exceeds its 60 s limit: run with `--runxfail` the same session gave
`trip 1 ypbpr 27.5s; trip 2 ypbpr never`. So 24.1..25.4 s is the time to a
usable solve, not to a solve that matches the learned rate, and the count
column is what the comparison rests on.

**THE OLD BUILD'S FAST LEGS WERE THE FAULT, NOT A SUCCESS.** 2.0, 2.4 and
2.9 s each acquired on a count of 271 or 272 against the source's 260 --
`/geometry` `cv` 542 and 544, which is twice that count -- so an acceptance
measured on time alone scores the defect as the best result in the run. Judge a
`ypbpr` leg on the COUNT it settles on, and only then on how long it took.

**~~WHAT REMAINS IS THAT THE ADC PLL STILL DOES NOT LOCK UNTIL `FullReset`.~~
FOUND, AND IT WAS `SP_COAST_INV_REG` LEFT STANDING BY A RUNG.** The inverted
coast gate brackets the active line instead of the vertical interval, so the
block counts the serrations as lines and the hsync reaching the ADC PLL is broken
every line. `applyForSearch()` set it and `applyDynamic()` returned before the
line that cleared it -- the same shape, in the same function, as the pulse-ignore
value fixed one session earlier. Writing the one field to 0 in the stall brings
the count from 265 to 259/260, `STATUS_SYNC_PROC_HTOTAL` from 2258 to 2200 and
`STATUS_MISC_PLLAD_LOCK` from 0 to 1 on the following sample, with everything
else the rung wrote still in force.

**NO ACT OF `FullReset` WAS EVER THE ANSWER.** Its prefix re-applies the dynamic
settings, and that is where the clear happened -- 21 s late, because
`applyDynamic()` is reached on the tick only from inside
`if (!Adc::inputIsComponent())`, so on a component source a rung is the only
route to it and no rung sits between 27 and 150. The PLL group is byte-identical
across the stall and the fix.
`investigations/the-search-coast-inversion-outlived-the-search.md`.

**~~WHAT IS LEFT IS THE LADDER FIRING INTO A HEALTHY ACQUISITION.~~ Closed by
the collapse.** Measured on a leg that failed: the engine read 263 lines at
15576 Hz and installed its divider at 2.16 s, `recovery: coast window at pass 8`
reset that window and discarded the placement at 2.18 s, and the count then read
271 with `STATUS_SYNC_PROC_HTOTAL` 3268 against a 2200 divider. The first reading
lands at 1.8..2.7 s and pass 8 at 1.84 s, so which came first was a race decided
by milliseconds. Nothing fires inside the budget now: three acts at 10, 20 and
30 s, and a mode change measured 1.2..2.0 s to `sync pad: driven` either side of
the change.

**A 3 s hold before the first rung does not close it** and makes `vga` slower,
measured 5.0..17.3 s against 4.1..7.8 s. The budget is what replaces it.

**~~THE PASS-450 RUNG PARKS THE SEPARATOR FULLY OPEN AND ONLY AN ESP RESTART
CLIMBS OUT.~~ Gone with the rung.** `ReopenSogSeparator` called `choose(0)`,
which `SyncOnGreen.h` records as "the one value no ratchet can climb back out
of". A leg that failed for 25 s reached it, and every leg after that failed too:
`ADC_SOGCTRL` 0, `STATUS_SYNC_PROC_VTOTAL` 97, `VPERIOD_IF` a correct 524 beside
it. `/sc?~` does not recover it because the held level is what `apply()` writes
back; `/restart` does, detection choosing `ComponentLevel`. The separator walk
inside `Reconfigure` is what recovers it, and nothing parks it any more -- a host
test asserts the level never reaches 0 across six act intervals.

**THE OLD READING, FOR REFERENCE.** The console says where the 24 s goes:
`STATUS_SYNC_PROC_HTOTAL` reads 2230..2392 against a 2200 divider and the duty
line prints `UNLOCKED` on every sample from 5.6 s to 23.2 s, then `recovery:
full reset at pass 150` runs at 23.9 s and the next duty line is `164 pulse /
2200 divider, htotal 2200` with no `UNLOCKED` and a source key of 259@59.94.

**The spread is the finding.** 24.1..25.4 s is 1.3 s across six legs where the
old range spanned 44 s, so the wait is a fixed ladder POSITION rather than a
flaky part -- the acquisition is gated on reaching pass 150. Every rung before
it is measured not to help: `coast window` 8, `sync processor dynamic` 27,
`release capture` 32, `hold clamp` 34, `nudge mode detect` 38, `reprobe sync
type` 44, `hsync overflow protect` 48 and `restart sampling clock` 60 all run
and `HTOTAL` keeps reading 2230..2392 `UNLOCKED` through all of them.

So the question is which act of `FullReset` matters, and the candidates are the
two no earlier rung performs: `reacquireSeparator(false)`, which walks the
separator rather than stepping it once, and `SyncProcessor::reset()`. The
single-field refutations above still stand and do not bear on it -- they were
pokes into a wedge, where this asks what a rung does to one.

`SyncOnGreen::liftOffFloor()` is also recorded as a no-op on this source: it
returns early above `LowestSteppable` 2 and the level is 13, so the `lift SOG
floor` rung the console prints here does nothing.

**The second half is the held rate.** The garbage readings taken while the
source cannot be followed are accepted, a divider is installed from one of them,
and `rateFollowsCount()` then rejects every correct reading against the bad held
one -- `sampling: 319 lines x 50.00 Hz -> line rate 0`, repeatedly, with
`duty: 241 pulse / 2200 divider, htotal 3274, negative, UNLOCKED` beside it. The
divider is another source's and the PLL free-runs under it until
`HeldRateRejectionLimit` lets go.

`tools/gbsc-pro-hwtest/test_acquisition_time.py` carries the wall-clock gate for
this as a strict xfail, so it flips the day it is fixed.
`docs/investigations/the-ladder-never-restarts-the-adc-pll.md` is the teardown
half, which is fixed and is not this.

### FIXED, not yet confirmed at the bench: the settings save left a stale tail

`LittleFS.open(path, "w")` was measured leaving the bytes past what the save
wrote. The file read **51 bytes** where the save made exactly **39 live one-byte
`f.write()` calls**, and bytes 39..50 came back byte-identical across a save
that demonstrably happened -- toggling `preferScalingRgbhv` flipped byte 10 and
the preference survived a restart.

**Why `"w"` left the tail is still not established.** What closes it is not an
explanation: `saveUserPrefs()` calls `f.truncate(0)` after the open, and the
settings file ends with `end`, so a reader stops at the terminator and a tail
beyond it is inert whatever its length. The length no longer carries any
meaning, which is what made a stale tail dangerous in the first place.

**Confirm by reading `/preferences.txt` after a save** and checking nothing
follows the `end` line. `docs/preferences-file.md`.

### A mode change between two rasters sharing a divider loses the lock for seven seconds

`640x480@60 -> 800x600@60` on `vga`, pass-through: the sync processor counts
**1970..1973 against a divider of 2038** and holds there, so
`Adc::dividerLatched()` is false and `SourceMeasurement::takeDuty()` refuses
every reading as `UNLOCKED`. The engine re-measures at 10 Hz for about seven
seconds until the recovery ladder reaches `restart sampling clock` at pass 60,
which clears it in one step: `htotal 2038`, `duty: 243`, solved.

**The source is never in question.** `sampling:` reads a steady
`627 lines x 60.31 Hz -> line rate 37879` on every one of those passes, and
`STATUS_SYNC_PROC_VTOTAL` is 627 throughout. It is the horizontal count alone
that is short, by about 3.3%.

**The count is steady while it is wrong**, which is the trap: 1971 held across
dozens of samples reads like a small latch error rather than an unlocked PLL,
and the usual discriminator -- asking whether `SP_VTOTAL` is counting -- passes.

**Both rasters ask for the same divider**, 2038, so nothing about the sampling
clock has to change; what moves is the line rate under it, 31500 to 37879 Hz.
The ladder's own fix is to restart the sampling clock, which suggests the PLL is
simply never told to re-lock when the divider it would write is the one already
in force.

The other three transitions measured in the same run -- into 640x480@60, into
800x600@56, and 800x600@56 back to @60 -- acquire on the first pass with
`htotal 2038` and never enter the state.

Measured identically on the low-active and high-active retime stops, 55 against
54 `UNLOCKED` readings over the same four mode changes, so it is independent of
`SyncProcessor::retimeStopFor()`.
`investigations/the-sog-polarity-auto-correct-displaced-pass-through.md`.

### A declared shape is unreachable where the capture is wider than the narrowed room

`VDS_?SCALE` cannot minify, so the produced picture is never narrower than the
capture. Where a declared aspect asks for a room narrower than that, the axis
fills instead and the shape is refused -- reported on the console and as
`"shaped":false` in `/geometry`, because no register distinguishes a refusal from
having been given no shape at all.

Measured: 800x600@60 into 1080p. The 60 Hz raster is 1592 units, its room 1389,
and 4:3 asks for 1042 against a capture of 1090. The bench's 50 Hz sources are
unaffected and shape exactly.

**Raising the display clock does not lift it**, and that is measured: at
129.6 MHz the raster widens to 1910 but `VideoPath::dividerCeilingForOutput()`
raises `PLLAD_MD` from 1438 to 1744 with it, so the capture grows in the same
proportion and the ratio of capture to room does not move. The picture filled at
both clocks.

What would lift it is the input formatter's scaling-down block, which sits ahead
of the VDS, is already in circuit and is idle --
`investigations/the-input-formatter-can-scale-down.md` has what the bench
established and what building on it requires. `aspect-ratio.md`.

### A fast source's output mode is bounded by the encoder, and the width bound is unsettled

**The floor has landed and the binding quantity is derivable**, so what is open
here is only how far down the fallback has to reach.
`investigations/the-encoder-ceiling-is-the-raster-floor.md`.

What bounds the output mode is the clock the ENCODER must transmit --
`totalPx x frameLines x fieldRate`, the standard's raster at the source's rate --
against `OutputMode::EncoderCeilingHz`, which is the datasheet's 165 MHz. No
display clock of ours appears in it, so a mode the encoder cannot carry cannot be
bought back with a faster one. `OutputMode::transmittableFor()` falls back to the
tallest mode the encoder can carry, answered per measurement so the chosen
resolution returns when the source does.

**WHETHER THE RASTER WIDTH BINDS AS WELL IS NOT SETTLED, and it decides whether
the fallback is far enough.** The unusable point was 630 lines at 84.68 Hz into
1080p: a 1134 px raster at 95.3 kHz, asking the encoder for 209.6 MHz. The
fallback puts it on 1024p -- 152.4 MHz, in spec -- but only 1196 px, where the
nearest clean point is 1600 and 720p's known-clean answer is 1700.

**1350 px is measured clean**, so the band is 1134..1350 rather than 1134..1600:
640x480@75 falls back to 1024p on the bench, locks, and emits a clean full-screen
frame. Only the 1196 px that 84.68 Hz falls back to is unmeasured.

**And that check is not runnable from this source.** `RetroScaler-Acorn.mdf` tops
out at 75 Hz, so 84.68 Hz takes a definition carrying an 85 Hz entry.

**Do not guess a width floor from that one point, and it is NOT the mode's
`activePx`.** 1080p solves 1600 at 60 Hz and 1916 at 50 Hz, both clean, so a floor
at 1920 would refuse every 60 Hz mode on the bench.

**The smeared overlay is not evidence the board is at fault.** It was read that
way because the STV9426 overlay is generated on the board against
`HS_OUT`/`VS_OUT`, so it reads the output raster directly -- but it is keyed into
the analog video at U13, ahead of the encoder, so it reaches the panel through the
encoder exactly as the picture does. A link 27% outside its rating corrupts both.

**What the ceiling changes on the bench**, since it moves modes that currently
work: nothing below 66.67 Hz at a 1080p preference, which leaves both bench
sources untouched -- the RISC PC at 50.475 Hz and the Wii's 480p at 59.8 Hz. The
monitor definition's 70 Hz and faster DMT modes now run 1024p, and at 70 Hz that
is a WIDER raster than 1080p gave, 1447 against 1371.

### Not every mode is unclipped at its default, and a stored framing hides which

**The requirement is that EVERY mode comes up unclipped at its default framing.**
What is measured is narrower than that: the DMT modes land on their published
active region after
`investigations/the-raster-bound-is-stored-as-a-proportion.md`, and that is one
mechanism of one class of mode.

Two things still clip, and neither is the default placement:

**A STORED FRAMING OVERRIDES THE DEFAULT AND SURVIVES REBOOTS AND REFLASHES.**
`/framing.txt` loads into `FramingTable` at boot and an entry keyed on the source
wins for the life of that source. The bench RISC PC at 320x256@50 runs
`311@50.00/686++ = 2625 6249 1250 8205` and is visibly cropped, where the DMT
modes checked beside it have no entry and show the default.

**`/sc?B` forgets the entry in RAM only.** Measured after an OTA flash: the
320x256@50 entry the framing sweep had forgotten was back in force at boot, and
`/framing.txt` still listed it, so a sweep's forgetting lasts until the next
reset and a real tuning survives it. What the file holds is what the next boot
runs, whatever `/sc?B` did.

**A stored entry and a wrong default are indistinguishable from the picture**,
which is what blocks the requirement rather than any one mode. Only
`VideoPath::step()` writes the table and only when a press moved a window, so an
entry is a real tuning, or pad presses from a `--source` pytest run, or a
solve-narrowed framing that a later press stored -- that last being the bug
above, so **an entry written before it may carry it**. `/sc?B` forgets the entry
for the source in force and is destructive of a real tuning.

What would settle it: a way to see the default with the stored entry set aside,
or a rule that a stored entry may not crop below what the published raster
states.

**AND THE 15 kHz ACORN MODES' DEFAULT IS UNVERIFIED.** `CaptureWindow::place()`
takes the mode's active region only where `SourceTiming` publishes one and falls
back to `Axis::activeStart`/`activeExtent` -- 0.117/0.864 horizontal -- where it
does not. Every mode verified was a DMT mode on the published path. The everyday
source takes the fallback and no unclipped picture has been measured on one.

`CARD` is the instrument: it carries a one-pixel green border flush to all four
edges, so clipped-or-not is a yes/no on one frame. `ANIM OFF` first.

### An output resolution request can be lost, two ways -- the first FIXED

**The handler's branch is gone.** A resolution letter now calls
`changeOutputResolution()` whatever `scalingRgbhv()` reads, and the IR
handlers that re-applied the presets through the same branch call
`applyPresets()` directly. `test_output_request.py` is the reproduction: a
source mode change, then one request. On the previous build the request
answered 200, the console printed nothing for it, and the raster stayed on
1125 lines for the 30 s the test waits; on this one the raster carries the
mode's lines within nine seconds. The IR path has no acceptance test, being
reachable only from the remote. The second way, below, stands.

`/uc?f g h j p s` sets `uopt->presetPreference` and then, in
`handleType2Command()`, applies it through `changeOutputResolution()` only
`if (!scalingRgbhv())`; when that predicate is true it calls
`RgbhvOutput::chooseBypass()` instead and applies nothing, then saves the
preference. `scalingRgbhv()` is `sourceIsRgbhv() && RgbhvOutput::isScaling()`,
and `sourceIsRgbhv()` is "the selected input shares the VGA port", true for
the whole of every bench session -- so on this bench the request is honoured
or discarded by `RgbhvOutput::isScaling()` alone, a flag the engine's own
comments describe as having several writers and reading pass-through on a
unit that is scaling. `UpDisplay()` carries the same branch around
`applyPresets()`.

The branch is upstream's mechanism translated: there, a scaling RGBHV source
(standard 14) was marked bypass (15) so the sync watcher would notice a change
of standard and reload with the new preference. Nothing here consumes the
flip that way: the three other readers of the flag shape the next preset
load's sync-processor preparation and resets, and none re-applies the
resolution. So the letter is saved and not applied until something else loads
a preset, and the flag is left reading bypass on a unit that is scaling,
which is the state the coast-window fault was traced to.

Observed twice by the framing sweep at its 1080p-to-720p transition, the
raster left at 1125 lines with the stored output resolution already 720p: the console printed nothing for the request where an honoured one
prints its new divider within half a second, and the next request landed. It
does not reproduce from a settled unit -- a request that lands leaves the
flag false and every later one lands too -- and a full re-detect (`/sc?~`)
leaves it false as well, since the detection path chooses bypass on its way
out. What leaves it true is the sync watcher's own mode-change handling,
which chooses bypass and then loads presets, and both losses followed a
source mode change.

**The second way is below the handler.** Replayed after a source mode change
with the console attached, the first request got no HTTP answer at all --
status 0 on a ten-second timeout, nothing printed -- and the same request
seventeen seconds later was answered and honoured in a fifth of a second; a
re-detect gave the same once, on the second of two requests. The unit stops
answering HTTP for some seconds after a mode change, from an immediate route
that only stores a byte, and what holds it is not established: the vsync
sampler is bounded at 250 ms.

What would close it: `changeOutputResolution()` unconditionally -- it already
holds the resolution across pass-through itself -- and the same for
`UpDisplay()`, with a bench test that re-detects (`/sc?~`), waits for
acquisition, sends the other of 1080p/720p and asserts `VDS_VSYNC_RST`
carries it within 30 s. Until then `gbs_unit.choose_output()` refuses a
raster that merely held still, re-sends a request that got no answer, and
re-sends once more one that was answered and never applied.

### hdmi_capture.borders() overstates the picture on the bench RISC PC

**An isolated dim blob at columns 1880..1899, peak luma 44.8 with dead black
either side, sits past the end of the picture** and clears the tool's `BLACK`
threshold of 24. On `X320 Y256 C256 F50` at the default framing that puts the
reported right margin at **35** where the card's content ends at column 1588 and
the true margin is **331**; the left margin reads 200 either way.

Thresholded at 60 or 120 the span is 200..1588 both times, so the blob is the
only thing between them. What emits it is not established -- it is one 20-column
group, not a tail off the picture.

**Every margin taken with the default threshold on this source is affected**,
and a measurement that creeps an edge until the picture stops will stop at the
blob rather than at the picture.

### The sub-pixel shimmer is the sync processor's phase, fixed on one source only

**The cause is found and the fix has landed**, so what is open here is how far
it reaches. `Adc::acquirePhase()` chose `PA_SP` by going half a field from the
single worst three-phase window of one sweep, and a maximum is decided by one
outlier: a phase disturbed while the walk is on it scores all 20 of its samples
where a genuine band scores a fraction. The engine held 9 against a band at
10..16. `Adc::middleOfWidestCleanRun()` replaces it, and five consecutive solves
on the bench chose 29 or 30, twelve phases clear of the band.
`investigations/the-sampling-phase-cannot-reach-the-shimmer.md`.

**It is proven on one source, one divider and one band position** -- the RISC PC
on `vga` at `PLLAD_MD` 2200. Whether the band sits elsewhere on a component
source, at a different divider, or splits into two runs is unmeasured, and a
profile with two comparable clean runs is the case the new rule has no evidence
for.

**AND THE NEW RULE HAS SINCE LANDED INSIDE THE BAND.** A solve on that same
source, divider and input chose `PA_SP` **10** -- the first phase of the 10..16
band the rule exists to avoid -- with `STATUS_SYNC_PROC_HTOTAL` reading 2199
twice and 2201 once in 15 samples against a divider of 2200, and the shimmer on
the picture. Writing 29 by hand restores 2200 in 25 of 25. So the five
consecutive solves that chose 29 or 30 are not the whole behaviour, and what
decides a bad landing is unmeasured. **The count dither is the cheap check**:
sample `STATUS_SYNC_PROC_HTOTAL` against `PLLAD_MD` twenty times and a good
phase reads the divider back every time.

**IT IS SEEN ON A SECOND MODE, AND IT CHANGES PER ACQUISITION.** The high
frequency graticule of the card at 640x480@60 shimmers, and differently from one
acquisition of the same source to the next -- which is the signature of the
phase search landing somewhere different each time rather than of a constant
being wrong. That is a second mode and a second divider for a rule proven on one
of each, so it is evidence the band question above is live rather than a new
fault. The console says where each solve landed: `phase dither:` is the profile
the search scored and `sampling phase: chosen, oversample N` is what it took.

**THE CHEAP DISCRIMINATOR IS THE COUNT DITHER**, above: twenty samples of
`STATUS_SYNC_PROC_HTOTAL` against `PLLAD_MD`, read in the acquisition that
shimmers. A good phase reads the divider back every time, so a run that dithers
says the phase landed badly and one that does not says the shimmer is something
else.

It also confounds anything else being judged on the picture at the same time, so
establish the phase is good before collecting verdicts on another artefact.

**A second artefact on the same bench is NOT this one and must not be merged
with it.** The entry below carries it.

### CLOSED: the judder in moving content is the bench television, not the board

A flicker between white and black at the edge of a sharp contrast change, in
MOVING content only. **Swapping the display on the capture dongle's loop-out
settles it**, with the board emitting a byte-identical signal on every roll:

| sink on the loop-out | re-acquisitions | juddering |
|---|---|---|
| the bench television | 6 | **6** |
| a second display | 5 | **0** |

The encoder's link partner is the dongle rather than the display, so the
MS9288A is held constant along with the raster, both scales, the divider and the
sampling phase -- `VDS_HSYNC_RST` 2139, `VDS_VSYNC_RST` 999, `VDS_HSCALE` 475,
`PLLAD_MD` 2200, `PA_SP` 29 and `STATUS_SYNC_PROC_HTOTAL` 2200 read back on all
eleven.

**DO NOT RE-OPEN THIS AGAINST THE BOARD WITHOUT SWAPPING THE SINK FIRST.** It
cost a session, and every upstream measurement is identical across the symptom:
all 608 config registers, the frame time lock's converged phase, clock and
`pin`, and four independent measures over raw captured frames.
`investigations/the-judder-follows-the-display-not-the-signal.md` carries them,
and the refutations of `VDS_HSCALE`, the crossover phase, the clock steering, a
frame-rate beat, a torn frame and a per-column displacement.

**What stays useful is the instrument.** `PAD_SYNC_OUT_ENZ` held away two
seconds and restored re-acquires the link with **every config register
byte-identical either side**, so it isolates a re-acquisition from everything
else a mode change does, at about twenty seconds a roll. Any artefact that
changes state across it is decided at acquisition and is not in a register.

**The capture is structurally blind to output TIMING**, which is why it could
not settle this. A set on the loop-out sees the emitted signal passed through,
while the USB capture is re-clocked to the dongle's own ~59 fps against a
50.47 Hz output -- so frame content survives and frame timing does not.

**It does not say the emitted signal is beyond criticism.** A set that judders
where another does not may be reacting to the 50.475 Hz field rate, the non-CEA
raster, or timing the capture cannot measure. It says only that no register,
solve or firmware change is implicated, because the board emits the same thing
in both verdicts.

**THE SET'S OWN "PC MODE" REDUCES IT**, which places some of the artefact in the
set's processing rather than in what it receives. That is the first thing to try
on any set that shows this, and it costs nothing. It does not remove it.

**WHY THE MODES DIFFER IS NOT ESTABLISHED, AND THE OBVIOUS ANSWER IS REFUTED.**
`OutputMode` states CEA-861 for 1080p/720p/480p/576p and VESA DMT for
1024p/960p, and on this set the CEA modes are clean three for three while both
DMT modes judder -- 480p being the exception and independently broken, its
display window reading `VDS_DIS_VB_SP` 36 .. `VDS_DIS_VB_ST` 37, one line. But a
set that interpolates a broadcast input and leaves a PC one alone would judder
on exactly the modes that are clean here, so **the correlation runs against the
mechanism that would explain it**. The same six points split equally well on
output line rate -- 53.8 and 50.5 kHz juddering between 56.8, 37.9 and 31.6 kHz
clean -- and six points cannot separate the two readings. **1024p and 960p
cannot be moved to CEA** in any case: it does not define 1280x1024 or 1280x960.

**THE RESIDUAL IS THE FIELD RATE, AND IT IS A TRADE RATHER THAN A DEFECT.** PC
mode leaves it a little jerky still. The output follows the source at
**50.475 Hz** rather than 50.000, which is the frame time lock doing its job, so
a set whose panel runs at 50 or 60 Hz has to rate-convert and a 0.475 Hz offset
repeats or drops a frame about every two seconds. Emitting exactly 50.000 Hz
would let such a set show it 1:1 and would move the hitch onto the board
instead, because the source is not at 50.000 either. **Do not reach for that as
a fix** without measuring which end carries it better; it is the trade the lock
already decides.

So the practical configuration on this set is **1080p with PC mode on**. Note
the artefact has been seen at 1080p once and not reproduced since, and **the
odds per mode are unmeasured** -- the pad toggle is cheap enough to count twenty
rolls a mode and nothing has run that.

The following were ruled out while the board was still suspected, each with the
artefact reported present:

- **`PA_ADC`, refuted BLIND.** Presented at 0 and at 16 in an order drawn at
  random and not disclosed, with the artefact reported present at both. Scored
  independently by frame-to-frame change of a static band, phases 0..10 give
  0.73 to 0.93 with no band -- flat, where `PA_SP` peaks at 2.13. Consistent
  with the arithmetic in
  `investigations/the-sampling-phase-cannot-reach-the-shimmer.md`: a doubled
  line keeps 1100 samples against a 512-pixel source line, so the sampler
  already visits 275 sub-pixel positions and a global offset moves all of them
  together.
- **Frame buffer alternation.** `CAP_DOUBLE_BUFFER` and `PB_DB_BUFFER_EN` are
  both 0 and `PB_CAP_BUF_STA_ADDR_A` and `_B` hold the same address, so nothing
  is switching buffers per frame.
- **The frame time lock**, turned off outright.

**THE INSTRUMENT IS PROVEN, so a negative from it means something.** Writing a
phase adjuster over `/setreg` reproduces `Adc::applyPhaseAdc()` -- latch low,
value, latch high, then the `BYPSZ` restart the header says the adjuster takes
the value on -- and the writes land tens of milliseconds apart rather than back
to back. That it reaches the hardware is not an argument but a measurement:
`PA_SP` at 29 counts 2200 in 25 of 25 reads and at 12 counts 2199 five times and
2201 twice, and the bench calls the picture clean at the first and clearly
shimmering at the second.

A per-column temporal measure has since been taken and separates nothing: the
displacement is uniform across twelve column strips and across fourteen row
bands, so the picture moves as a whole where it moves at all.

**A SECOND DISPLAY IS THE NEXT INSTRUMENT**, because the artefact is now known
to be invisible to the board and to the capture, which leaves the encoder and
the television, and only a second sink separates them. Nothing established
licenses naming either -- reaching for the encoder is the standing wrong answer
on the scaling path, and a poke cannot test what is decided at acquisition.

**The odds are unmeasured.** The pad toggle is cheap enough to repeat twenty
times and count, which says whether a re-acquisition is a coin flip or biased
per mode. The engine takes the pad away on every source mode change and every
output change and returns it once the setup has settled -- so it re-rolls
this on every such change already, with nothing able to judge the outcome. A
"re-roll until clean" recovery needs an on-board detector, and no measurement
here is one.

### A black frame with one green line at the top, for several seconds while detecting

Seen across repeated `/sc?~` cycles on a healthy unit, clearing by itself once
the solve lands. `DAC_RGBS_PWDNZ` 1, `PAD_SYNC_OUT_ENZ` 0 and every
`SFTRST_*_RSTZ` 1 throughout, and the capture afterwards is a full picture, so
it is a state the bring-up passes through rather than one it can be left in.
What is not established is which stage emits it, and whether a sink that is
slower to re-lock than this bench's shows it for longer.

### The transmitted window's start is per source, and no constant places it

**MEASURED PER RASTER ACROSS THE AKF50 SET, AND NO TERM PLACES IT.** The sink's
own position -- where it opens the window with the source's black at the
aperture's edge -- is one number per output raster, repeatable to a unit, and
within an output it steps with the field rate in bands rather than following
T, the clock or the time after the pulse: at 1080p 157 to 159 from 56.25 to
60.32 Hz and 171.5 at 50 Hz, at 1024p 318 to 328 for 75 and 72.8 Hz and 331
to 333 for 70 Hz, at 720p 393 to 398 at 108 MHz and 302 to 307 at 81 MHz. The
model's twenty units after the porch is within three of it everywhere except
the 50 Hz 1080p rasters (11.5 early) and the 75 and 72.8 Hz 1024p fallbacks
(7 early). Two sources sit off their raster's band with nothing an instrument
here can see to separate them: 360x480@60 at 60.15 Hz is 14 to 16 units early
at both outputs, and 320x256@50 into 720p is 41 units late against 640x512@50
on the same clock two units of T away. And a transition whose line has PICTURE
at the aperture's edge lands 5 to 9 units before the own position, on 10 of
the 28 stock modes, where a pad toggle on the settled state never does.
`framing_sweep.py --tier S` measures it and `framing_report.py --sink` reports
it. `investigations/the-sinks-own-window-position-is-per-raster.md`.

**SUPERSEDED IN PART: the start is placed per return of the sync pad, not per
state.** The sink chooses it when `PAD_SYNC_OUT_ENZ` is driven again: the first
non-black content at the aperture's edge where that is earlier than the sink's
own position for the raster, else that position, and it remembers nothing
between returns. A source `MODE` round trip never toggles the pad and never
moves it; a pad toggle by hand re-places it every time. The same state read
158.9 in Tier B and 172 in Tier C with every register identical, the first
placed while a stored cropped framing put picture at the aperture's edge. With
black at the edge the position is per raster -- 158.0 to 158.8 on three sources
at 1600 x 1125 @ 60 Hz, 171.0 to 172.2 at 1916 x 1125 @ 50 Hz -- so an aperture
placed AT it frames every source flush, since content cannot be earlier than
the aperture. `investigations/the-encoder-places-its-window-when-the-sync-pad-returns.md`.
The readings below stand as what each mode change placed.

**The window the chain carries is the right WIDTH and starts where the
encoder puts it, which is not where `OutputMode::solve()` puts it and is not
one constant off.** Measured with `framing_sweep.py` on eight sources into
1080p and 720p, and two of them into 960p and 1024p, the link re-locked by a
source `MODE` round trip before every judged clip: the width is `T x
carriedPx / totalPx` to within a unit at 1080p and 1024p and four at 720p, and
the start sits 13.0 to 24.3 units after the scaled sync and porch at 1080p and
-12.7 to 37.8 at 720p, one number per (source, output) state. The two
sources at one raster and one aperture -- 640x480@60 and 800x600@60 into
1080p, both apertures at 160 -- put it at 153.3 and 157.7; the two at T 1920
into 720p put it 45 units apart. The whole window moves, so black at the left
and picture lost at the right come in one number per state: 9 to 10 columns
for 640x480@60 at three outputs, 37 for 640x480@75 at 720p, 1 to 5 for
800x600@60. `TransmittedWindowDelayPx` at 20 is right for none of them, and
where the rate clamp binds -- 640x480 at 72.8 and 75 Hz falling back to 1024p
-- the clamp places the window to within two units at both ends.

**It is not re-rolled between acquisitions.** Across mode-round-trip re-locks
one state repeats to under half a unit: 800x600@60 at 1080p 157.7, 157.6 and
157.4, at 720p 396.2, 395.9 and 395.8; 640x480@60 at 1080p 153.3, 153.7,
153.7, 153.7 and 152.3, three of them identical to the column at every walk
step. The six-pixel difference between acquisitions recorded earlier is not
seen with this re-lock; a `PAD_SYNC_OUT_ENZ` toggle is the re-lock that lands
elsewhere.

**And it is not latched from `VDS_DIS_HB_SP`**, which the aperture at 160 on
both of the 1080p sources above already says. Nor does any one of T, the
display clock, the field rate, the line rate or the scan mode order the
sixteen states. Sources at a standard's exact rate -- 60.00 Hz, and 72.81 and
75.00 at 720p -- read 7 to 13 and -12.7; those at none -- 59.87, 60.32, 56.25
and 50.16 Hz -- read 16.8 to 24.3; 320x256 at 50.08 reads 18.9 at 1080p and
37.8 at 720p. What the encoder keys on is not established.
`investigations/the-encoder-window-start-is-per-source.md` has every row.

**No constant can correct it**: a delay of 13 leaves black at the left of
every state placed later, 20 loses picture off the right of every state placed
earlier, and covering the spread means an aperture wider than the window at
both ends, losing picture off both edges by up to the spread on every state.

The capture window is NOT the fault: measured inside one acquisition with both
of the card's green columns in frame, the source's active video runs 298.9 ..
1403.0 units against the engine's window of 298 .. 1403.

Vertically the picture is flush: 0 or 1 output pixel of the source's own
blanking on screen across six DMT modes.
`docs/investigations/the-transmitted-window-is-latched-from-our-blanking.md`.

### The undoubled capture origin has a term at each end of the cable

**`SyncProcessor::RetimeOriginSamples` is 63 and the measured states want
anything from 60 to 66 once the oversampling step below is taken off.** Two
controls settle that the remainder is not one quantity, and each rules out the
other's explanation:

- **A scaler term.** One mode, one source, only the divider moving: `PLLAD_MD`
  800 / 1400 / 1440 give **+3.2 / +9.4 / +10.1** samples. The source cannot see
  the divider.
- **A source term.** `800x600@60` and `1600x600@60` are the definition's only
  pixel-clock twins -- sync 3.2000 us, line 26.400 us, active starting
  5.4000 us in, same polarity and field rate, same solved `PLLAD_MD` 1438 and
  retime stop 1325, so **nothing the TV5725 can observe differs**. They read
  **+5.1 against +2.9** samples, three interleaved acquisitions each, repeating
  to better than 0.1 source pixels. Both sample at one rate, so a constant count
  and a constant time are one hypothesis here and both are refuted.

**The 108 MHz outliers are solved and fixed: they were the only two states at
oversampling ratio ONE.** CKO above 80 MHz puts `PLLAD_KS` on row 0, which leaves
the post divider no room for a ratio, so both decimators are bypassed and the
captured video lands **15.9 ADC samples** later in the line counter. Measured by
holding the divider across the crossover with the source untouched, on both
modes, and separated from the PLL's own row by forcing ratio one at row one with
`/sc?o` -- one divider, one VCO, one geometry, **+0.7 at ratio two against +16.8
at ratio one**. `SyncProcessor::UndecimatedOriginSamples` is 16, and the two modes
now read within about a sample of their published raster.
Ratio four is not a further step of the same kind.

**WHAT IS LEFT IS ONE LINE IN SAMPLING DENSITY, AND ITS SLOPE IS THE SOURCE'S.**
With the oversampling step and the polarity step both corrected, seven modes
spanning both polarities in one session fit `d = 4.10 x (units per source pixel)
- 5.93` to a residual rms of **0.48 samples**; a polarity term added to that
buys 0.03 and takes a coefficient of -0.37. The slope is about four SOURCE
PIXELS, which is the source's video lagging its own sync -- nothing downstream
of the sync separator can measure or correct it, because the scaler cannot see
the pixel clock. The intercept is already centred for the density range the
bench reaches, so moving `RetimeOriginSamples` trades one end of it for the
other.

**THE `1024x768` PAIR IS NOT AN OUTLIER AND WANTS NO MEASUREMENT.** The 8.5 it
was filed at was fitted while the 108 MHz pair still carried its 16-sample step.
Refitting the same eleven anchor-free states with the step corrected leaves it
at **-0.97 and -0.95** against a residual rms of 1.28, inside the two samples one
mode moves between sessions. A divider hold there has no power either: the
output raster caps its density at 0.71..1.07, which the fitted slope turns into
1.6 samples of travel.

Anchor-free, at the engine's own divider:

| mode | units/px | d samples | d source px |
|---|---|---|---|
| 1024x768@70 | 0.94 | -2.5 | -2.7 |
| 1024x768@60 | 1.07 | -1.9 | -1.8 |
| 1280x480@60 | 0.90 | +0.0 | +0.0 |
| 720x480@60 | 1.68 | +2.0 | +1.2 |
| 640x480@60 | 1.80 | +2.0 | +1.1 |
| 1600x600@60 | 0.68 | +2.9 | +4.2 |
| 800x600@60 | 1.36 | +5.1 | +3.7 |
| 360x480@60 | 2.72 | +7.2 | +2.6 |
| 320x480@60 | 3.60 | +10.1 | +2.8 |

**Take these with `counter_origin.py`, not `full_margins.py`.** The anchored
instrument reads about two samples higher, and that difference belongs in ADC
samples rather than source pixels: over five modes spanning a four-fold range of
density it runs 1.67 to 2.76 samples, a 48% spread against 184% for the same
differences read as source pixels. Converting an anchored reading to source
pixels without taking it off manufactures a constant that is not there.

**The polarity half of the remainder is closed.** The inverted path measures the
pulse wide, `retimeStopFor()` subtracts the reading straight into the stop, and
`SyncProcessor::InvertedPulseWidthSamples` now takes 5 samples off a high-active
source where the reading is made. Seven modes in one session gave a pulse error
of -3.80 against +1.73 either side of the boundary, a split of 5.53, beside an
origin step of 5.27 -- and regressed against density the pulse error carries a
coefficient of 0.85 where the arithmetic wants 1.

The earlier reading of this as 98 ns, tighter as a time than as a count, does
not survive the wider set: at one divider and a 27% range of sample rate the
H-positive error is +1.7, +1.7, +1.8, where a fixed time wants +1.7 to +2.2.

Undoubled sources are the ones whose rasters the engine is entitled to derive a
capture window from, so this bounds how well a published-raster framing can do.
`docs/investigations/the-capture-origin-varies-by-mode-at-one-line-rate.md` has
the controls; `docs/investigations/the-line-doubler-resets-the-fifo-late.md` has
the doubled counterpart.

**One mode's reading moves about 2 samples between sessions** while repeating to
0.2 within one, so compare readings taken in one session or carry the spread.
**Readings at 75 Hz do not repeat at all**: 1125 lines at 75 Hz is not a
standard mode, and `800x600@75` read +8, +29, +17 and +15 samples on four
acquisitions.

**The remainder reaches the default framing, and it costs the right edge.**
Read through the encoder's window by `framing_sweep.py` at the default on the
stock AKF50's 640x480@60 and 800x600@60, into 1080p, 720p, 960p and 1024p: the
capture window sits where the row says to four figures, and the card's border
puts the picture one to three source pixels later than that on every output.
On 800x600@60, whose display is the DMT active area exactly, the left border
is +0.7 to +3.0 raster units inside the picture's start and the right border
is off the frame at three outputs of four; on 640x480@60 the right border
reads +8.4, +8.9 and +14.0 units against the +13.1, +12.0 and +17.4 its own
mode file predicts. Same sign and size as the table above by a second
instrument. A source with no border loses its last pixels at the right at the
engine's own framing, which is the requirement above not being met, and the
correction the plan names for it is the row's own start rather than the
origin constant.
`investigations/the-encoder-window-start-is-per-source.md`.

### A CEA-861 HD source acquires, and its capture origin is the previous source's

**`retimeStopFor()` cannot express an origin behind a pulse narrower than
`RetimeOriginSamples`.** The stop is `PLLAD_MD - pulse + origin`, so a pulse
under 63 samples -- 79 at oversampling ratio one -- puts it past the end of the
line, **where the counter cannot use it at all**. Nothing is written and the
counter keeps whatever the source before it left.

**A STOP ONE UNIT PAST THE LINE TEARS THE CAPTURE, so the refusal is protecting
the picture rather than declining to guess.** Crept a unit at a time on the
bench source at `PLLAD_MD` 2200, frozen: the picture moves 1.000 ADC samples per
unit and holds to 0.01 dongle columns all the way to 2200, and at **2201** every
line lands at its own offset in green. Five captures at one frozen value
disagree by 262 dongle columns where a healthy state repeats to 0.01, and the
lit column count goes 1329 to 1918 -- the picture is smeared across the frame
rather than sitting anywhere. So the choice is not between a wrong origin and no
origin: writing what the rule asks for loses the picture outright.

Measured at 1920x1080@60: `PLLAD_MD` 1440, pulse **31** samples, the rule wanting
`SP_RT_HS_SP` 1472 against a line of 1440, and the register holding **1338** --
640x480@60's value. The picture is there and is misplaced by the difference.

At the divider the engine picks, 63 samples is about 4.4% of the line, so every
CEA-861 HD raster is below it: 720p60 is 2.4%, 720p50 and 1080p60 are 2.0%,
1080p24 is 1.6%. A bigger divider buys a wider pulse, but 720p60 would need
`PLLAD_MD` 2603, which is past the IF line counter's 2047 samples.

**This is what the pulse floor coming down traded for.** `HsyncPulse` refused
those rasters at 4.1%, and a refused duty leaves
`SourceMeasurement::takeDuty()` with `dutyMeasured_` false and the source at
`Settling` for ever -- measured, 1280x720@60 printed `NOT A PULSE` for as long
as it was connected and never showed a picture. The floor is 1.0% now and both
720p and 1080p acquire and paint: 1068 of 1080 rows lit at 720p, 1079 at 1080p.

**So the only shape a fix can take is to place the stop at the largest value the
counter accepts and take the shortfall off the capture window.** At 1920x1080@60
that is an origin of 31 where 63 is wanted, leaving 32 samples for the window to
absorb -- and the doubled path's own origin register cannot carry them, because
`IF_HBIN_SP` is held at `NoHeadBlanking` on a progressive source and the part
blanks the whole line above 18 there.

Untested: whether the largest value the counter accepts is `PLLAD_MD` itself or
wants a guard below it. 2200 is clean and 2201 is not on a source whose
`STATUS_SYNC_PROC_HTOTAL` reads 2200 exactly; a source whose count dithers has
not been tried, and each sample of guard is one more for the window to absorb.

### Composite sync captures the doubled line 24.5 source pixels early

**The line doubler's FIFO reset wants a different value on each sync type**, and
the shipped one is sized for separate sync. Measured on `vga` at
`X320 Y256 C256 F50`, one source and one mode, with the divider, the output raster
and the capturable window unchanged:

| sync type | wants `IF_HBIN_SP` |
|---|---|
| separate, `SP_SOG_MODE` 0, coast 0/0 | 147 |
| composite, `SP_SOG_MODE` 1, coast 7/6 | 55 |

`InputFormatter::LineDoubleReset` is 160, so on composite sync the capture starts
24.5 source pixels into the source's line -- about 8% of a 320-pixel line -- and
every window the solve places follows it. The reset pans at one ADC sample per
register unit, so nothing downstream compensates.

92 samples is nearly three times the 32 the field-rate split spans, so this is the
larger of the two unexplained origin terms and any mechanism for either has to
account for this one first. It is the scaler's and not the source's: `SYNC` changes
only which pin carries sync and leaves VIDC20's horizontal registers alone. The
sync duty is not the explanation, 154 against 156 being noise.

**Do not move the constant on this.** It is one reading; its vertical companion
could not be taken at all, because on composite sync the feature lands 2.8 lines
into the capture and the creep has no run-up. Replicate it on a second source
first. `docs/investigations/the-line-doubler-resets-the-fifo-late.md`.

### A held field rate 1.4% high survives the source returning, and the frame time lock cannot converge on it

**Measured on the bench RISC PC at `X320 Y256 C256 F50`, after a run of mode
changes through 60 Hz modes and a round trip through 720p and 960p.** The engine
held 50.766 Hz against the source's 50.081, and the line rate with it -- 15839
against 15625 -- steadily, with `state: acquired`, `STATUS_SYNC_PROC_VTOTAL` a
clean 311 and `HTOTAL` matching the divider. Nothing in a register dump is
wrong.

**What it costs is the frame time lock.** At the wrong rate the phase never
settles: it slides monotonically and wraps the counter every ~17 s, the error
sweeping its whole range, while the steering has only ~30 mHz of resolution
(50735 / 50766 / 50796 mHz) and cannot close a ~57 mHz gap. The display clock
runs 109.36 MHz against the 108 asked for, and the picture judders vertically.

`/sc?~` clears it at once. Afterwards the same measurement reads 50.081 and the
lock holds a phase of 810674..810712 out of 3194880 across 38 s -- a spread of
0.001%.

The rate is measured and then HELD, and `rateFollowsCount()` judges a new
reading against the held one, so a rate accepted once outlives the source that
produced it. The count is the thing that did not move here: 311 lines is right
for both the rate held and the rate true, so nothing in the pair disagreed.
**A count that fits cannot arbitrate a rate**, and there is no second witness.

### The display window closes after the last written pixel, and the gap shows unwritten memory

**Measured on the bench RISC PC, `X320 Y256 C256 F50` into 1080p, default
framing.** Three columns at the right of the emitted frame and one row at the
bottom carry content the capture never wrote, and what they carry CHANGES
between acquisitions with every register identical -- columns 1884..1886 read a
mean luma of 45 / 90 / 62 on one lock and 5.7 / 12.6 / 5.7 on the next.

The arithmetic accounts for both:

| | produced | write starts | picture ends | window closes | open |
|---|---|---|---|---|---|
| h | 998 x 1024/611 = 1672.6 | `VDS_HB_SP` 44 + 55 + 25 x 1.676 = 140.9 | 1813.5 | `VDS_DIS_HB_ST` 1815 | 1.5 px |
| v | 621 x 1024/589 = 1079.6 | `VDS_VB_SP` 36 + 0.2 + 0.8 x 1.739 = 37.6 | 1117.2 | `VDS_DIS_VB_ST` 1120 | 2.8 lines |

1.5 output pixels is 2.9 dongle columns, and three are lit. The memory window IS
the display window here, `VDS_?B_ST == VDS_DIS_?B_ST` on both axes, so nothing
blanks the remainder.

**The window's end does not follow the write.** It is placed from the memory
window rather than from where the produced picture actually stops, which is the
write start plus `capture x 1024 / scale` -- and the write start carries the
magnification term, so the gap moves with the scale rather than being constant.

Insetting the window is not the fix: a window pulled inside the picture loses
real rows. `docs/scaler-geometry-model.md` has the write-start model.

### A line-doubled source's origin is 15 ADC samples out at one field rate or the other

**The line doubler's FIFO reset is the doubled path's whole origin term**, one
ADC sample of picture per register unit, and what each bench mode wants was
measured by clipping the card's frame out of the capture:

| field rate | modes | wants |
|---|---|---|
| 50.08 Hz | X640 Y256 141, X768 Y288 145, X320 Y256 147, and 152 at `PLLAD_MD` 1800 | 141..152 |
| 60 Hz | X640 Y240 175, X640 Y200 175 | 175 |

`InputFormatter::LineDoubleReset` is 160, which takes every one of them within
15 samples where the inherited 272 was out by 127 -- the source's active start
now lands −9.8 to +7.2 source pixels from the mode file against −28.5 to −60.5
before.

**No mechanism accounts for the split and one value is deliberate.** Every
input-side register reads identical across it: the divider, the whole PLL group,
both decimators, `IF_HBIN_SP`/`ST`, `IF_HS_DEC_FACTOR`, both coasts, `SP_DLT_REG`
and `HLOW_LEN` within a sample. The two field rates do solve different output
rasters, but the instrument cannot see the output. The 60 Hz shortfall is the
same TIME at two different pixel clocks -- 0.89 us at 13.5 MHz, 0.87 at 16 --
which points at the source and is not confirmed.

What would settle it: a doubled source at 60 Hz that is not one of the RISC PC's
two game modes, or the same mode's raster measured off the wire.
`investigations/the-line-doubler-resets-the-fifo-late.md`.

### The vertical origin's sync arrangement is unmeasured

**The scan-mode half is closed.** The frame counter's origin sits a fixed
distance after the vsync pulse's LEADING edge, measured by clipping the card's
frame out of the capture, and it is a count of COUNTER UNITS rather than of the
source's lines:

| scan | sources | origin | in counter units |
|---|---|---|---|
| doubled | X320 Y256 F50, X640 Y240 F60, X1056 Y256 F50 | 5.0..5.1 lines | 10.0..10.2 |
| undoubled | 640x480@60 | 8.7 lines | 8.7 |

Three doubled sources agree and their pulses differ by a factor of two, which is
what says it is the counter's property rather than the source's.
`VideoSourceLine::DoubledFrameOriginUnits` is 10 and `FrameOriginUnits` 7, and
`InputFormatter::capturableFrame()` carries whichever the scan mode wants -- so
it reaches every source rather than only a published raster. Measured with
`card_edges.py` afterwards, `800x600@60` and `640x480@60` are flush at both
vertical edges and `X320 Y256 C256 F50` is two rows off at each.

**What is still unmeasured is the sync arrangement.** `capturableFrame()` adds
`SeparatorFrameLeadLines` to the origin on a separated source, which assumes the
separator's delay is on top of the counter's own rather than replacing it.
`SYNC 1` on the RISC PC makes that one run and nobody has taken it.

### The capture window's far edge falls short of the source's last drawn pixel

**Measured with `card_edges.py`, three modes, default framing, after a source
mode round trip re-locked the link.** The card's one-pixel frame reaches the
emitted frame's first column on every one of them and its last column on none:

| mode | left edge | right edge | emitted picture |
|---|---|---|---|
| X320 Y256 C256 F50 | column 0 | off the panel | 1895 x 1076 of 1920 x 1080 |
| X800 Y600 C256 F60 | column 0 | off the panel | 1915 x 1080 |
| X640 Y480 C256 F60 | column 0 | off the panel | -- |

Two of the three take their window from the DMT rows, so it is not a property of
one tier.

**MOST OF IT WAS THE OUTPUT, and that half is fixed.** With
`TransmittedWindowDelayPx` at 3 the emitted picture ran to column 1894 and the
source's leftmost drawn pixel was clipped at column 0; restored to 20 it runs
5..1904 with that pixel whole. The remaining right-hand margin is 15 columns of
1920.

**IT IS NOT THE DOUBLED PATH'S ORIGIN, and `InputFormatter::LineDoubleReset` is
not the knob.** That reading rested on the Acorn mode alone. Measured on all
three with the capture's near edge walked through the pads and the card's frame
read off the capture at each step, the two UNDOUBLED VESA modes lose the right
border in the same way and by a comparable amount -- and the line doubler's FIFO
reset is out of the path on both:

| mode | default `oh` | left frame at | right frame enters at `oh` |
|---|---|---|---|
| X320 Y256 C256 F50 | 237 | columns 6..17 | 243, and the left is gone by 241 |
| X800 Y600 C256 F60 | 294 | columns 4..11 | 296, flush at 298 |
| X640 Y480 C256 F60 | 260 | columns 8..13 | -- |

`X640 Y200 C256 F60` joins them at 23 columns, measured at its engine default
once the AKF50 row placed it -- green at columns 23..31 and no right edge, with
the vertical flush at the bottom and two rows at the top. **THE OFFSET IS NOT A
CONSTANT IN ANY UNIT**: against `X320 Y256 C256 F50` it is 8.3 capture units
where that is 2.2, 16.6 ADC samples against 4.4, 7.7 source pixels against 1.0
and 481 ns against 125. Both are doubled 15.6 kHz modes on the same input, and
what differs between them is the pixel clock and the border the mode file states
-- 16 pixels against 44.

So the picture sits 4..8 emitted columns to the right of where it should, on
every mode, and its far end falls off by about as much. **What that is in is
undecided**: 4..8 columns is 2.2 / 2.3 / 4.8 capture units and 5.2 / 2.9 / 6.9
output raster pixels, and neither is constant across the three -- while the
reading itself is worth a couple of columns either way, because the frame is one
source pixel wide and its captured edge smears into the flashing ring beside it.

### The picture is short of the transmitted window, and the strip past it is not picture

**Measured by walking the aperture until the emitted frame's last column changes
state**, which asks only where the encoder stops carrying our line. At
`X320 Y256 C256 F50` the last 14 of 1920 emitted columns are black at the
engine's own framing, and opening `VDS_DIS_HB_ST` from 1831 to 1845 fills them.

**WHAT FILLS THEM IS NOT PICTURE, AND THAT IS MEASURED.** With the aperture held
open, the capture was panned 4 units -- 11.1 emitted columns -- and each region
of the frame asked which shift best explains it:

| region | best shift | runner-up |
|---|---|---|
| picture, columns 1500..1900 | **11** (err 7.8) | 12 (err 112.6) |
| picture, columns 1800..1900 | **11** (err 14.7) | 12 (err 150.6) |
| the strip, columns 1906..1919 | **0** (err 1.7) | 1 (err 870.0) |

The picture moved exactly as far as the pan; the strip did not move at all, and
columns 1908..1919 read byte-identical at both pans. **So the aperture closes
where it should and the PICTURE is what falls short** -- there is nothing to be
recovered by opening the window, and the encoder is not latching onto content.
The strip is a fixed ramp, 235 falling to about 75, which follows the last
written column rather than the source.

**THE NEAR EDGE IS NOT OUT.** Solved from two independent anchors in one frame --
the card's leftmost drawn pixel at raster 165.4 reading column 6, and the
picture's end at 1829.5 reading column 1905 -- the window is 160.2 .. 1842.7 at
1.1411 columns per unit. Our aperture opens at 160. An earlier reading of 156.6
took the black at the LEFT for our own blanking when it is captured SOURCE
blanking, which is the confound
`investigations/the-transmitted-window-is-latched-from-our-blanking.md` names.

**IT IS REPEATABLE ACROSS ACQUISITIONS.** Six re-locks per mode through a
`PAD_SYNC_OUT_ENZ` toggle, framing untouched and `VDS_DIS_HB_ST` unchanged at
1831 throughout, give 14/14/14/15/14/15 black columns at `X320 Y256 C256 F50`
and 0 six times at `X640 Y480 C256 F60`. **The left edge does not move either**,
which is what settles it: a window placed differently at each lock would move
both ends.

**IT FOLLOWS THE FIELD RATE AND NOT THE SCAN MODE.** An earlier reading here said
the opposite and rested on one unsound measurement -- `X640 Y200 C256 F60` walked
downward from an aperture where the fetch returns black, which reads as a window
edge and is not one. At the engine's own framing, with no register touched:

| mode | lines | scan | raster | black at the right |
|---|---|---|---|---|
| X640 Y512 C256 F50 | 534 | flat | 1914 | 6 |
| X320 Y256 C256 F50 | 312 | doubled | 1916 | 15 |
| X640 Y480 C256 F60 | 525 | flat | 1600 | 0 |
| X800 Y600 C256 F60 | 628 | flat | 1592 | 0 |
| X640 Y200 C256 F60 | 262 | doubled | 1604 | 0 |

Both 50 Hz sources are short and no 60 Hz one is, on either scan mode. **The
magnitudes do not yet fit one model**: 6 columns against 15 on two 50 Hz
rasters two units apart is more than the arithmetic accounts for, and
`X768 Y288 C256 F50` reads 34 but is one of the rows that takes another mode's
framing, so its picture is mis-sized before the window is reached.

What `OutputMode::solve()` computes is `activeStop = activeStart +
horizontalTotal x 1920 / 2200`, which is CEA's 60 Hz total. A 50 Hz source emits
into 1080p50, whose CEA total is 2640 -- so the one input the model has that
changes with the field rate is the one it does not use. **The naive substitution
goes the wrong way**, 2640 giving 72.7% where the measurement wants more than
87.27%, so the encoder is not switching to the 50 Hz standard's blanking either.
`investigations/the-picture-falls-short-of-the-transmitted-window.md` carries the
whole of it, including what a second output resolution would say.

### The sync polarity separates no two AKF50 rasters sharing a key

`SourceTiming::lookUp()` reads the polarity pair -- the earliest row matching
frame, rate, sync width AND polarity, falling back to the earliest matching the
first three -- and on the AKF50 set that changes nothing. **Every group that
collides on (frame, field rate, sync duty) is uniform in polarity**: the AKF50
rows resolve to the same keys with polarity as without. The seven 15.6 kHz PAL
modes are all `+/+`, the five 525-line ones all `-/-`. What the pair does
separate is a mode file that states different polarities for two layouts on one
key, and DMT's own 640x350@85 (+H -V) / 640x400@85 (-H +V).

**A MONITOR DEFINITION'S `sync_pol` IS A BITFIELD OVER AN ACTIVE-HIGH DEFAULT**,
bit 0 inverting hsync and bit 1 inverting vsync. Documented in two places and
confirmed on the bench:

  RISC OS 3 PRM volume 5a states the field for `MDF` and for
  `Service_ModeExtension` offset 60 -- *"bit 0 set => Hsync inverted, bit 1 set
  => Vsync inverted"*, with 0 reading *"hsync normal, vsync normal"*.

  The VIDC20 data sheet §4.1.24 says what normal IS. The Ext Register's `syn-HS`
  and `syn-VS` select `00 HSYNC / 01 nHSYNC` and `00 VSYNC / 01 nVSYNC`, so the
  un-inverted form is the active-HIGH one and `sync_pol 0` emits positive-going
  pulses on both axes.

Read off the bench against `STATUS_SYNC_PROC_HSPOL` and `_VSPOL`, which report
the pin rather than the path -- `SP_HS_INV_REG` is 1 on both `+` readings below
and the status bit is unmoved by it:

| `sync_pol` | mode | HSPOL / VSPOL | | against the standard |
|---|---|---|---|---|
| 0 | X320 Y256 C256 F50 | 1 / 1 | H+ V+ | -- |
| 0 | X800 Y600 C256 F60 | 1 / 1 | H+ V+ | DMT states +/+ |
| 2 | X640 Y352 C256 F60 | 1 / 0 | H+ V- | -- |
| 3 | X640 Y480 C256 F60 | 0 / 0 | H- V- | DMT states -/- |

**It does not follow the standard everywhere**, so it cannot be substituted for
one: AKF60's 1024x768@60 states `sync_pol 0`, which is H+ V+, where DMT states
H- V-.

### What a raster collision actually costs

**A key two rasters share is free where they want the same framing**, and what
matters is only the pairs whose normalised windows differ. Over the 41 rows,
resolved through the CEA -> DMT -> Acorn priority, 18 get a framing that is not
their own -- but most of them by under two points of the line or frame:

| the row that answers | is used for | worst edge, % of line or frame |
|---|---|---|
| 640x480@60 (DMT) | 320x480, 640x480, 1280x480 (AKF50) | 0.8 .. 1.0 |
| 640x480@60 (DMT) | 360x480 (AKF50) | **6.6** |
| 640x480@72 (DMT) | 320x480, 640x480, 1280x480 (AKF50) | 0.6 .. 1.0 |
| 640x480@75 (DMT) | 640x480, 1280x480 (AKF50) | 1.5 .. 1.7 |
| 800x600@56 (DMT) | 800x600, 1600x600 (AKF50) | 1.0 |
| 800x600@60 (DMT) | 800x600, 1600x600 (AKF50) | none -- the framings agree |
| 320x256 (AKF50) | 320x250, 640x250 | 1.0 |
| 320x256 (AKF50) | 1056x250, 1056x256 | 3.4 |
| 320x256 (AKF50) | 768x288 | **6.4** |
| 896x352 (AKF50) | 640x352 | 1.4 |
| 384x288 (AKF50) | 480x352 | **8.0** |

**THE 480x352 COLLISION IS NOW ISOLATED, BY A CHANGE THAT FIXED ITS NEIGHBOUR
AND LEFT IT ALONE.** It captures 288 lines for a 352-line picture, so raising the
vertical magnification limit took `X384 Y288 F70` from 218 black rows to 1 and
moved `X480 Y352 F70` from letterboxed to filling the frame while OVERRUNNING it
-- the card's border off every edge, the black-to-black margins reading 4/0/0/0
and the border search finding neither edge on either axis. A wrong capture
height magnified further crops instead of letterboxing; both are wrong and the
clamp was hiding which.

`640x256` and `800x600@60`'s Acorn twins cost nothing at all: their borders sit
in the porches, so the normalised window is the same one. **The priority is a
best effort rather than a correct answer** -- it exists to make the engine choose
a published raster over the envelope, and a row that is 1% out is still far
closer than the envelope's guess.

**Nor can the sync duty be sharpened enough.** 320x250 and 320x256 state the
same 36 pixels of 512, as do 640x250 and 640x256 against 72 of 1024 -- identical
in every quantity this chip can measure, differing only in active lines and in a
pixel clock nothing here can see. `SyncDutyTolerance` could separate 768x288 at
7.42% from 320x256 at 7.03% in principle, 0.39 points apart, but the instrument
reads 0.43 points off DMT on the bench, so the two are inside its own error.

What that costs is a crop on the modes the leading row does not describe:
1056x256 draws from 18.6% of the line where 320x256's row opens at 21.5%, so
about 3% of the line is lost at each end, and 768x288 loses 6%. Widening the
Acorn row to the union of its group would cost the bench mode its flush framing
instead, the union being 768x288's own row.

### The display window closes after the last written pixel, and the gap shows unwritten memory

**Measured on the bench RISC PC, `X320 Y256 C256 F50` into 1080p, default
framing.** Three columns at the right of the emitted frame and one row at the
bottom carry content the capture never wrote, and what they carry CHANGES
between acquisitions with every register identical -- columns 1884..1886 read a
mean luma of 45 / 90 / 62 on one lock and 5.7 / 12.6 / 5.7 on the next.

The arithmetic accounts for both:

| | produced | write starts | picture ends | window closes | open |
|---|---|---|---|---|---|
| h | 998 x 1024/611 = 1672.6 | `VDS_HB_SP` 44 + 55 + 25 x 1.676 = 140.9 | 1813.5 | `VDS_DIS_HB_ST` 1815 | 1.5 px |
| v | 621 x 1024/589 = 1079.6 | `VDS_VB_SP` 36 + 0.2 + 0.8 x 1.739 = 37.6 | 1117.2 | `VDS_DIS_VB_ST` 1120 | 2.8 lines |

1.5 output pixels is 2.9 dongle columns, and three are lit. The memory window IS
the display window here, `VDS_?B_ST == VDS_DIS_?B_ST` on both axes, so nothing
blanks the remainder.

**The window's end does not follow the write.** It is placed from the memory
window rather than from where the produced picture actually stops, which is the
write start plus `capture x 1024 / scale` -- and the write start carries the
magnification term, so the gap moves with the scale rather than being constant.

Insetting the window is not the fix: a window pulled inside the picture loses
real rows. `docs/scaler-geometry-model.md` has the write-start model.

### The retime stop is wrong on every line-doubled source

**Measured at full framing against the mode file.** The rule
`SP_RT_HS_SP = PLLAD_MD - STATUS_SYNC_PROC_HLOW_LEN + 63` was fitted on two
UNDOUBLED sources. On the bench's doubled ones the counter's origin lands late,
so the source's picture is captured earlier on the line than it really is: 28.5
source pixels at `X320 Y256 F50`, 60 at `X640 Y256 F50`, 59 at `X768 Y288 F50`.
Every duration is right -- line total, sync width, both actives, the frame -- so
only the placement is wrong and no register dump can see it.

The stop that frames the raster is about 125 ADC samples below what the engine
writes at 50 Hz and 97 below at 60 Hz. **Three source rasters in one output
state agree within 7 samples**, which is the part that carries no instrument
assumption. The absolute figure and the split between the two field rates both
rest on the dongle-to-capture-unit anchor and are not settled.

What blocks the form: every 15 kHz mode this source offers has a sync duty of
7.0 to 7.4 per cent, so a term that scales with the sync width and one that
scales with the divider cannot be told apart -- the same degeneracy that made
`0.93 x PLLAD_MD` look right.
`investigations/the-source-raster-measured-against-the-mode-file.md`.

### A perfect signal can sit at `state: absent`, and freezing is what tells it from the HC32 fault

**Measured on `vga` with the RISC PC at 320x256@50, separate sync.** The console
reads `sampling: 1235 lines x 20.7 Hz -> line rate 0` against the 311 that mode
gives, the recovery ladder cycles its whole length -- SOG separator, SOG floor,
coast window, sync processor dynamic, capture release, clamp, mode detect, sync
type reprobe, hsync overflow, sampling clock restart, full reset, input toggle --
about every 28 s for ever, and `PAD_SYNC_OUT_ENZ` stays 1, so nothing is emitted
and the television says no signal. `own V sync: yes` probes to separate H/V,
`SP_SOG_MODE` 0, `SP_EXT_SYNC_SEL` 0, `SP_H_PULSE_IGNOR` 255. **That is the HC32
signature below, and it is not the HC32.**

**Freeze, and the registers read the source exactly.** `/freeze?on=1` stops the
ladder, and `STATUS_SYNC_PROC_VTOTAL` then reads **311 in 14 of 14 samples, one
distinct value**, with `STATUS_SYNC_PROC_HTOTAL` 2200 equal to `PLLAD_MD` 2200 on
every one. A signal that counts the source exactly and holds the PLL against its
divider is not half a signal, and no reading taken with the ladder running can
show that -- every one of those is taken through a configuration a recovery step
has just moved.

**The measurement and the recovery are a closed loop.** A reading taken during a
rewrite fails, the failure escalates the ladder, and the next reading is taken
during the next rewrite. Freezing opens the loop. **The divider is not the
discriminator**: 1400 is the reference divider a clean detection counts through
and it gives 311, so a divider equal to the failing state's is not evidence of
anything.

**The recovery is remote and costs no bench trip**: `/freeze?on=1`,
`/freeze?on=0`, then `/input?src=vga`. It acquires in 6 s -- `DETECT` reporting
`VT=311 HT=1400`, `sampling: 311 lines x 50.08 Hz -> line rate 15625`,
`sync pad: driven`, `frame time lock: running` -- and holds `acquired` with the
card emitted. **A `ypbpr` -> `vga` round trip on its own does not do it**, which
is what leaves the freeze as the acting part. The entry condition is not known,
so the recovery stands on one occasion and is not proven reproducible.

**So freeze before going to the bench, because the two separate there and
nowhere else.** Both have now been read frozen on this unit, on `vga` with the
RISC PC at 320x256@50:

| frozen | `STATUS_SYNC_PROC_VTOTAL` over ten samples | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|
| this fault | **311, one distinct value in 14 of 14** | 2200, equal to `PLLAD_MD` |
| the HC32 fault below | 9, 10, 11, 1257, 1650 | 2, 12, 94, 693, 1127, 1270, 2016 |

**The HC32 row was taken with the sync arrangement FORCED** -- `SP_SOG_MODE` and
`SP_EXT_SYNC_SEL` both written to 0 -- so the garbage is not the chip sitting on
the wrong path. Asked to count a separate-sync source as one, it still cannot,
and a count of nine lines in a field is no horizontal sync rather than a bad
measurement of it. A count that matches the source says the arriving signal is
intact; one that cannot be made to settle on the right path says it is not, and
that is the one that needs power.

### FIXED: an acquisition that completes without a preset load emits a flat field

**Reproduced on demand, 4 of 4 boots.** Which input the settings file names at
power-up decides it: stored as `ypbpr` the picture is a dark, flat or banded
green field; stored as `vga` it is clean, and selecting `ypbpr` afterwards is
clean too. Same input, same cable, same source.

**AND IT IS NOT A YPbPr FAULT.** `vga` on composite sync does the same thing --
`SYNC 1` on the RISC PC leaves the output black with `VTOTAL` 308 and `HTOTAL`
2200 against the divider, and `/sc?#` restores it. The condition is an
acquisition that completes without a preset load; separate-sync `vga` is clean
only because detection's RGB branch is the one that loads one.

**Detection's YPbPr branch claims the source and returns without a preset
load.** `detectAndSwitchToActiveInput()`'s RGB branch calls `applyPresets()`
before it returns and the YPbPr branch does not, so `doPostPresetLoadSteps()`
never runs on a boot that lands there. Nothing else on the boot path loads one.

**Every instrument reads healthy, which is why a dump finds nothing.** The
engine acquires the source and solves every geometry register from the
measurement, so the sync front end, `PLLAD_MD`, `STATUS_SYNC_PROC_HTOTAL` and
the framing are all correct. What is missing is `applyClockGroup()`,
`applyFrameBufferRequests()` and `applyPictureFilters()` — sixteen fields left
at reset defaults, `DEC_TEST_ENABLE` among them, which `calibrateAdcOffset()`
sets and only `applyStoredAdcGain()` clears.

**`/sc?#` cures it**, force-calling `applyPresets()` and touching nothing else —
no input, no output, no AV module frame, no hand-written register. So does an
output resolution change. So does `/input?src=vga` and back, because `vga` fails
detection's first pass and reaches the RGB branch's preset load.

| tried | reaches | clears it |
|---|---|---|
| `/sc?#`, a bare `applyPresets()` | scaler only | **yes** |
| output resolution 1080p -> 720p | scaler only | **yes** |
| `/input?src=vga` then back | HC32 and scaler | **yes**, 3 of 3 |
| `/input?src=rgbs` then back, which bounces `ADC_INPUT_SEL` | HC32 and scaler | no |
| `/input?src=sv` then back | HC32 and scaler | no |
| `/avframe` `vga` then `ypbpr` | HC32 only | no |
| `/input?src=ypbpr`, the input already selected | HC32 and scaler | no |
| `/sc?~`, a full detection pass | scaler | no |
| the sixteen fields written to their cured values | scaler | no |

**THE HC32 IS EXCLUDED, AND `rgbs` IS WHAT EXCLUDES IT.** `rgbs` shares the RGB
connector with `vga`, so it moves `ADC_INPUT_SEL` 0 -> 1 and back exactly as
`vga` does, and it carries no sync a separate-sync source can be claimed on, so
it never acquires — and it does not cure. Neither the frame, nor the switch
state it sets, nor the ADC input transition is the cure.

**A write-back is negative because the setup is a sequence**, not a set of final
values: the phases run block resets and re-seed the display clock. That negative
is what kept the sixteen fields filed as a defect of their own; they are the
symptom.

**THE BRING-UP NOW ESTABLISHES NINE OF THE FIFTEEN AND THE PICTURE IS
UNCHANGED**, so what `applyPresets()` supplies is a transition and not a value.
The display PLL's skew is deliberately not among them: establishing `PLL_R` and
`PLL_S` there blanks a `vga` boot outright, which is a separate fault wearing
the same signature.
Also refuted on a faulted boot: `DEC_TEST_ENABLE` cleared, the frame-sequencing
trio written, and the memory blocks pulsed through `resetVideoBlocks()`'s own
sequence by hand. Every block reset reads released, both pads enabled, both
scales solved.

**The boot spends its early life with the ADC PLL free-running** -- `htotal`
3258 against a divider of 2200, 51.0 MHz, for the whole of an 8 KB boot log,
with the count steady at 270 where the source is 260. A steady count is not a
locked one, and because it is steady the escalation ladder never fires.

**FIXED** by `Tv5725::MemoryBus::restart()` on the engine's mode change. The
SDRAM controller and both FIFOs are laid out for the capture geometry a mode
change computes, `ResetSDRAM()` was a free function in the sketch that only a
preset load reached, and a boot whose detection claims the source on its first
pass loads none. A `ypbpr` boot was 6 of 6 faulted and is 4 of 4 clean;
composite sync on `vga` comes up with a picture.
`docs/investigations/an-acquisition-without-a-preset-load-emits-a-flat-field.md`.

### The boot selects an input differently from every other caller

`/input`, the OLED and the IR handler all reach `applyInputSelection()`. The boot
reaches `applySavedInputSource()`, which sends the same frame and writes the same
input registers but omits the rest of it: `installReferenceSamplingClock()`,
`inputFormatter.applyScan()`, `resetSyncProcessor()` and
`sourceAbsence.selectionChanged()`.

The consequence that is measured is the first one: the boot takes its opening
measurement of the arriving source through whatever divider the chip was left
holding rather than through a reference one, and a faulted boot's early samples
read `270 lines x 121.42 Hz` against a source at 60. The
`input selected: …, reference divider 2506` line is only ever emitted on the
selection path.

Nothing says why the two differ, which by the conventions is what means collapse
them: the boot should select an input the way every other caller does.

### A boot that only ever detects `ypbpr` never runs `applyPresets()`

`detectAndSwitchToActiveInput()`'s YPbPr branch sets the sync-on-green level and
returns, where its RGB branch calls `applyPresets()`. That is the only caller of
`doPostPresetLoadSteps()` a boot reaches, so a unit that comes up on `ypbpr` and
stays there leaves its static configuration at reset defaults:

| field | left at | `doPostPresetLoadSteps()` writes |
|---|---|---|
| `VDS_PK_LB_GAIN`, `VDS_PK_LH_GAIN` | 0 | 22, 10 |
| `VDS_D_RAM_BYPS`, `VDS_UV_STEP_BYPS` | 0 | 1, 1 |
| `VDS_FRAME_RST`, `VDS_FRAME_NO`, `VDS_FR_SELECT` | 0 | 4, 1, 1 |
| `PB_CUT_REFRESH`, `PB_REQ_SEL`, `CAP_STATUS_SEL` | 0 | 1, 3, 1 |
| `DEC_IDREG_EN`, `DEC_WEN_MODE` | 0 | 1, 1 |
| `DEC_TEST_ENABLE` | 1 | 0 |
| `PLL_R`, `PLL_S` | 0 | 1, 2 |

The same set on two independently produced boots. Selecting any input repairs it
for the life of the boot, because the chip keeps its registers.

**This is not the green screen above**: writing the set either way moves no
picture. What it costs is the peaking, the line filter and the frame sequencing.

`PLL_R`, `PLL_S`, `DEC_IDREG_EN` and `DEC_WEN_MODE` are raw `GBS::` writes in
the sketch; the rest already go through `FrameBuffer`, `VideoProcessor` and
`Adc`, orchestrated from the sketch. Absorbing `doPostPresetLoadSteps()` into
the engine is what removes the asymmetry, because the engine's half of the solve
is input-symmetric already and only the sketch's half is not.


### The HC32 stops following input selections, and only a true power cycle returns it

**Measured on `vga` with the RISC PC at 800x600@60.** The sync processor reports
a correct field rate and no horizontal edges: `sampling: 1222 lines x 60.31 Hz`
against the 628 that mode gives, `duty:` lines reading `NO EDGE` and totals
thrashing 881..1995, never leaving `UNLOCKED`. The count is what doubling gives
-- 628 x 2 = 1256, less the ~34 lines of vertical interval carrying no pulses.

**`asw_01` is the mechanism and no instrument on the board can see it.** It
selects the dedicated HSync pin over `SOGIN`, and VGA is the only input that
raises it. Low, `HS_IN` is sync on green, which an RGB source with separate sync
does not provide -- so vertical arrives on its own pin and horizontal does not.
It lives on the HC32F460, which is write-only, keeps `asw_01..04` in its own
flash, and restores them at boot from `Video_ReadNot2()`.

**Every scaler-side recovery was tried and none of them reach it**, each
confirmed against the live fault:

| tried | result |
|---|---|
| `/sc?~` | no change |
| `/input?src=vga`, re-sending the HC32 frame | no change; `InputVGA()` sends it unconditionally with the right mode byte |
| `/restart` | no change, and the fault survives the boot |
| `/sampleclock?md=1438`, which restarts the ADC PLL | no change, still `lock 0` |
| a source mode change, and a `SYNC 1`/`SYNC 0` round trip | restores the field rate reading, not the lock |
| **mains *and* USB power cycle** | **recovered at once** |

USB backfeeds the rails, so mains alone leaves the HC32 powered and is not a
power cycle.

**Nothing in a register dump distinguishes it.** The sync processor, the clock
group and the sync-type decision read what a healthy unit reads --
`SP_SOG_MODE` 0, coast 0/0, `SP_H_PULSE_IGNOR` 255, divider correct and latched.
The engine is measuring faithfully; what it is measuring is half a signal.

**THE SYNC-TYPE PROBE DOES NOT ALWAYS STAY HEALTHY THROUGH IT, AND THAT MISLEADS
BADLY.** The reading above of `own V sync: yes` probing to separate H/V is one
outcome, not the rule. Measured with `BOOTLOG_BYTES=2048` on the same board, the
same input and the same RISC PC in `SYNC 0`, the probe answers differently on a
cold boot and on a warm one -- at the identical point in the sequence, right
after `INPUT:` and before `DETECT: enter`:

| | cold boot (`External System`) | warm restart (`Software/System restart`) |
|---|---|---|
| `INPUT: vga frame=0x61` | t=3896ms | t=9110ms |
| the probe | `own V sync: yes after 2ms` | `own V sync: no after 1000ms` |
| what it chose | `separate for input 3` | `composite or SOG for input 3` |
| `DETECT: enter` | `S16=0x0f HPERIOD=66` | `S16=0x04 HPERIOD=465` |
| outcome | `VT=311 SOG=0`, acquired in 2543 ms | the recovery ladder, indefinitely |

**The probe is not wrong and its timing is not the bug.** With no horizontal
reaching the sync processor there is no V to find, so the timeout is the honest
answer to the question asked. But `SyncMeasurement::probe()` sets `set_`
unconditionally, so the answer latches; `SyncOnGreen::inSyncPath()` is
`isCsync()`, so the whole sync-on-green recovery ladder then goes live on a
source that has none and walks a control nothing is reading.

**So `SP_SOG_MODE` 1 on a separate-sync source is a SYMPTOM of this fault, not a
second fault.** Chasing the probe from that reading costs a session: the
discriminating measurement is whether the sync processor is counting at all, and
the recovery is still a mains and USB power cycle.

Measured over 170 s in the stuck state, the probe answered three times positive
and five times negative on an unmoving source, each positive overturned by a
timeout 11.1 s later.

**What puts it in is the low-power teardown, measured once.**
`test_acquisition_time.py::test_a_torn_down_chip_is_built_back_up_by_the_next_pass`
tears the chip down on each input in turn and times the rebuild. On a unit
acquired and emitting on `vga` a minute earlier it reported **ypbpr 0.6 s, vga
30.0 s** -- the watch limit -- and `vga` has not acquired since. So the trigger
is narrower than a whole suite run: it is a teardown taken on this input, and
the handover-era note that it arrived somewhere inside a run carrying four
disruptive flags can be replaced by the module that does it.

**The teardown is undone and that is not the fault.** Afterwards `s0_46` reads
0x7f with every block released, `PLLAD_PDZ`, `ADC_POWDZ` and `DAC_RGBS_PWDNZ`
are all 1 and `PLLAD_VCORST` is 0, while `STATUS_SYNC_PROC_HTOTAL` stays 0. The
rebuild the test is named for happens; what does not come back is the horizontal
sync it has nothing to do with.

**The discriminator is the other input.** `ypbpr` acquiring on the same board at
the same moment -- 525 lines at 31468 Hz, held over five samples -- is what
separates a board fault from a signal-path one, and it costs one `/input`
request. Reach for it before any firmware hypothesis.

**It does not separate this from the livelock above, and `/freeze?on=1` does.**
`ypbpr` acquires in both, so a board proven good still leaves the two open.
Frozen, this fault has no horizontal edges and the livelock counts the source
exactly -- so take that reading before concluding the HC32 needs power.


### A positive `own V sync` can be thrown away by one read of a flickering bit

`SyncMeasurement::hasOwnVsync()` polls `STATUS_SYNC_PROC_VSACT` up to 500 times
over `OwnVsyncWindowMs`, and then re-confirms with a SINGLE read 10 ms later:

```cpp
if (active) { // confirm it: the bit flickers while the processor settles
    delay(10);
    active = GBS::STATUS_SYNC_PROC_VSACT::read() == 1;
}
```

`rose` is already recorded by then, so a discarded detection prints as
**`own V sync: no after 2ms`** -- a line that says the bit rose at 2 ms and the
answer is no. Observed once in eight probes.

**The confirm is weaker evidence than the detection it overturns.** The comment
states the bit flickers, and this page already carries `STATUS_SYNC_PROC_VSACT`
among the three status bits that read like verdicts and are none of them a
verdict. One sample of a flickering bit cannot overrule 500.

Not fixed, and NOT the cause of the sync-type flip above -- that one's negatives
are genuine, the bit never rising at all in the full window. A duty cycle over
the confirm window, as `Tv5725::SamplingLog` reports for the ADC PLL lock, is
what would settle it.

### About half of boots shake, and the rate was only part of it

**The shake is a property of the BOOT.** Surveyed with
`tools/gbsc-pro-hwtest/shake_survey.py` over restarts of the RISC PC at
800x600@60 on `vga`, scored as the standard deviation of a luma-gradient
centroid with the horizontal axis as the control:

| state | shaking boots | vertical sd when it shakes | clean |
|---|---|---|---|
| before, frame lock off | 3 of 6 | 0.126 - 0.263 | 0.005 |
| the display clock's rate taken from the engine | 4 of 8 | 0.205 - 0.288 | 0.003 |
| that, plus frame lock armed, 60 s settle | 1 of 6 | 0.203 | 0.003 |

A single observation says nothing, which is why "it looked fine when I checked"
has twice been taken as evidence that something fixed it.

**What is fixed: the rate.** The one-shot measured the source's field rate off
the test bus for itself, and a reading taken just after the divider latches is
repeatably wrong -- the boot log caught two samples BOTH reading 60529 mHz
against a source running 60317, so no agreement rule between a pair of them can
reject it. Eight boots before gave 60997, 59558 and 61194; eight after gave
60316 every time. The engine's settled rate is asked for instead.

**The engine's settled rate was itself boot-dependent when that survey was
taken**, and is not any more:
`investigations/the-first-solve-of-a-boot-cannot-be-corroborated.md`. Whether
that moves the shake count is unmeasured -- `shake_survey.py` is what would say.

**What is NOT fixed: the phase.** Boots 2 and 3 of the survey after that change
set the display clock to the same 108022960 Hz from the same 60316 mHz, and one
shook while the other did not. Rate alone cannot explain that. What differs is
where the read pointer sits relative to the write pointer when the buffer starts
-- which is what `FrameSync`'s `syncTargetPhase` exists to park at 90 degrees,
and what the frame time lock does beyond matching rates.

Armed, four boots in six converge to two parts per million and hold. **The other
two saturate**: the rate correction sits at its +-0.06% clamp and changes sign
for as long as the lock is armed, and the picture shakes throughout. Nothing in
the rate distinguishes them -- all six matched the same 60316 mHz and landed on
the same display clock to within 88 Hz.

**WHAT CAUSES THE SATURATION IS NOT KNOWN, AND THE PHASE IS NOT LOGGED.** Two
models have been tried on the bench and one is refuted; the display clock is two
steps downstream of the phase, so a noisy phase and an oscillating one reach it
looking alike. `docs/investigations/the-frame-time-lock-saturates.md` has the
measurements, the loop's arithmetic, and what to instrument before proposing a
third model. The lock is still off by default.

**The cadence is fixed and was not the cause.** `runFrequency()` measured the
source's field rate itself and refused to correct unless two readings agreed to
`Clock::RateAgreement::RelativeTolerance` -- 0.05%, which is 0.03 Hz at 60 Hz --
while those readings spread over a whole hertz against an engine holding
60.317 Hz. Measured: **five corrections in 95 s against about fifty refusals**.
It asks the engine now and makes all fifty-seven the interval intends. That made
the saturation legible rather than curing it: the shake rate either side is two
boots in six against one, which at six boots distinguishes nothing.

### An alternating count latched the scan type -- FIXED

**`SteadyRun` narrows the pair now.** A run of `CollapseSamples` identical
samples collapses it onto the value that ran, so a source that stops
alternating stops reporting `ScanInterlaced` and `steer()` reaches
`disableMotionAdapt()`.

The threshold is measured rather than chosen: RISC PC at 800x600@60 under
ModeServ's `INTERLACE ON`, 1873 samples at the engine's own 20 ms detection
interval, 953 of 628 against 920 of 627, and **the longest run of either value
is five**. A second window at 25 ms agrees. `CollapseSamples` is 16, and
`Deinterlacer::FilteredPasses` is a second filter behind it.

Verified on the bench in both directions: interlaced, motion adapt still
engages; returned to progressive with the source otherwise untouched, the latch
releases on its own and the picture comes good with no `/sc?~`.

**A widened pair is earned, not taken.** Collapsing one leaves the widening
unguarded, and the thresholds race: a source wobbles by one as it is acquired,
and `Deinterlacer::FilteredPasses` is TWO against sixteen samples of collapse.
Motion adapt engaged on every ESP reset, no flash involved, and the picture came
up green and comb-torn for the life of the boot. A second value is a candidate
until the count has RETURNED to it `CrossingsForInterlace` times.

**The scan decision holds its own steadiness run**, sampled by
`measureScanType()` on the maintenance cadence. The solve's run stops being fed
once a source settles, and a source going interlaced moves the count by one,
which `SteadyRun::agree()` calls the same measurement -- so nothing re-measures
and nothing samples the alternation.

Verified 2026-09-24 on the bench in both directions: five consecutive restarts
on the progressive source leave `s2_00` at `0xff` throughout, and `INTERLACE ON`
reaches `0x19` within a second or two, `INTERLACE OFF` back to `0xff`
immediately.

Two things worth keeping from it:

- **`MAPDT_VT_SEL_PRGV` is not a detection read-out.** Four functions write it
  -- `enableScanlines()`/`disableScanlines()` and
  `enableMotionAdapt()`/`disableMotionAdapt()` -- so it is 1 on a correctly
  detected interlaced source whenever bob is preferred and 0 on a progressive
  one whenever scanlines are on. `MADPT_EN_UV_DEINT` and `RFF_LINE_FLIP`
  separate the two features.
- **Setting `DIAG_BOB_PLDY_RAM_BYPS` back to 1 alone restores a clean picture**
  while motion adapt stays engaged, so a clean screen was never evidence the
  latch had cleared. Read the field table.

**There is still a second owner of the same registers.**
`enableMotionAdaptDeinterlace()` in the sketch calls
`Deinterlacer::enableMotionAdapt()` directly from the `p` serial command, with
no steering and no filtering, and picks its vertical tap from the same
`InputFormatter::verticalPeriod()` -- so on separate sync it is handed 0.

`docs/investigations/an-alternating-count-latches-the-scan-type.md`

### The composite post coast decides whether the vertical blanking reaches the pin

**FIXED.** `SyncProcessor::CompositePostCoastLines` is 6. It was 3, and 3 is the
one value that stops the input formatter's vertical reaching `DEBUG_IN_PIN`.

Measured on the Wii on `ypbpr` at 480p, sync on green, at the `IF_VB_ST` 512 its
framing lands on, scored on `/testbus?ms=150&if=3` with the engine solving
normally:

| `SP_POST_COAST` | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 12 |
|---|---|---|---|---|---|---|---|---|
| `tb,0` transitions | **0** | 18 | 18 | 18 | 18 | 18 | 18 | 18 |

`tb,2` is 17..18 throughout as the control, `STATUS_SYNC_PROC_VTOTAL` is 524 at
every one, and the engine stays `acquired`. The same edge holds at every
`SP_PRE_COAST` from 4 to 13.

**THE UNUSABLE `IF_VB_ST` VALUES ARE NOT A PROPERTY OF `IF_VB_ST`.** 4/3 and 4/6
are the same 512 with opposite outcomes. The old framing -- a small set of values
composite sync refuses, moving with the coast -- was reading the coast's effect
off the axis it was varying. So there is no bad set to keep a solve away from,
and `sweep_vb_st.py` measures a real signal against the wrong variable.

**Only the test bus sees it.** `STATUS_IF_VT_OK` reads 1, `VPERIOD_IF` and
`STATUS_SYNC_PROC_VTOTAL` both read a correct 524, and a raw dump shows only the
field's two bytes moving. `sweep_coast.py` drives the pair and scores the count
and the blanking together.

**The roll is a consequence, not a second fault.** `TestBus::selectInputVsync()`
selects that same signal, so `FrameSync` cannot read an input period, never
arms, and the output free-runs -- 60.027 Hz against 59.940, lapping every 11.5 s.
`/framesync`'s `ready` latches once armed and is not an oracle for this.

**The pre coast holds the COUNT, and that is the other axis.** At `SP_PRE_COAST`
0 the Wii's count dithers across 8 to 14 distinct values in a 14 s window at
every post coast, the scan decision follows it and the engine drops to `absent`.
That is why 0/0 was refuted, and it is not what the post coast does.

**A correction of seven units on both window edges is what the deleted
`FrameLagUnits` did. Do not reinstate it** -- it worked by stepping the end off
512, which is now understood to be the coast rather than the value.

`investigations/the-vertical-origin-follows-the-sync-type.md`

### Composite sync at 640x480@60 loses the source at every coast pair

The RISC PC on `vga` at 640x480@60, `SYNC 1`: the engine cycles
`source acquired: 524 lines` and `source absent: ~490 lines` about ten times a
second, with `scan:` alternating interlaced 525 / progressive 524 and the
deinterlacer engaging and releasing under it. The picture alternates between
clean frames and sheared ones with a duplicated right portion.

**No coast pair helps, and the shipped one is not special.** Twenty pairs swept
over `SP_PRE_COAST` 4..12 and `SP_POST_COAST` 0..12: every one loses the source
between 19 and 42 times in a 14 s window, 7/3 among them at 27.

**A point read agrees with whichever phase it catches.** `/geometry` reads
`acquired` from this state as often as not, and three reads in a row at six
second spacing all read `acquired` while the console showed the churn. Score it
off `source absent:` on the console, which is what `sweep_coast.py` counts.

The same machine at 320x256@50 on `SYNC 1` holds perfectly -- 624 lines, vt 308,
0 to 3 losses at every pair -- so this is the mode and not composite sync.
800x600@60 on `SYNC 1` is between the two and does not hold acquisition either.

### A sync-type round trip strands the engine at `absent` with the count correct

RISC PC on `vga` at 320x256@50: `SYNC 1` acquires and holds, and `SYNC 0` after
it sits at `state: absent` indefinitely with `STATUS_SYNC_PROC_VTOTAL` reading
311 -- the source's true separate-sync count -- beside a held `cv` of 624 from
the composite solve. `/sc?~` recovers it in under a minute.

**It is not the coast.** The same round trip strands identically with the coast
overridden to the old 7/3 and to the current 7/6, and the separate-sync branch
writes 0/0 either way.

This is the held-rate stranding `HeldRateRejectionLimit` exists for, reached by
a count that moves 624 -> 311 across one sync change.


### The composite capture window opens a whole pulse from the wrong end, and neither end is right

**FIXED, in three parts.** The whole pulse was `HsyncPulse` carrying the
measured polarity into `syncAtHead`, and that went with the polarity. What was
left is the sync separator's own delay, on both axes: its output reaches the
input formatter's counters late, so a source through it -- composite sync and
sync on green both -- lands earlier in them than its published raster states.

| axis | constant | value | shape |
|---|---|---|---|
| horizontal | `VideoSourceLine::SeparatorPulseEndLeadSamples` | 66 | subtracted from the sync width, in ADC SAMPLES |
| vertical | `VideoSourceLine::SeparatorFrameLeadLines` | 16 | a count of the source's LINES |

Measured on one cable and one raster with the sync type the only variable:

| mode | line | separate - composite | fraction | frame rows | source lines |
|---|---|---|---|---|---|
| 320x256@50, doubled | 1100 | 76.5 | **6.96%** | 61 | **16.6** |
| 640x480@60, undoubled | 1446 | 103, 109 | **7.1%, 7.5%** | 34 | **15.2** |

Vertically a fraction is 80% wrong, so the vertical stays a count of lines. The
horizontal readings above were taken against the published raster on one sync
type and do not stand; the delay is the measured sync width less 66 ADC samples,
over six modes.
[investigations/the-separator-lead-is-not-a-fraction-of-the-line.md](investigations/the-separator-lead-is-not-a-fraction-of-the-line.md)

Solved at 640x480@60 the emitted frame's black margins:

| | left | right | top | bottom |
|---|---|---|---|---|
| separate | 5 | 0 | 0..3 | 5..6 |
| composite | 14 | 15 | 5 | 5 |

against 128 px of black down the left and 34 rows up before. The Wii on `ypbpr`
takes both leads too and acquires clean and full screen.

`investigations/the-separator-moves-the-counters-origin.md`

### The separator's path writes head blanking a doubled line cannot capture past

On the sync separator's path a doubled line carries saturated green to unit 88
of its 1101-unit counter, and separate sync carries none measurable at the same
divider, the same mode and the same register. Measured at 320x256@50 with
automation frozen, counting columns of green at the head of the emitted line:

| `IF_HB_SP2` | 52 | 60 | 70 | 80 | 88 | 90 | 100 |
|---|---|---|---|---|---|---|---|
| composite | 74 | 58 | 38 | 17 | **0** | 0 | 0 |
| separate | -- | -- | **0** | 0 | -- | 0 | 0 |

So the capture floor cannot come down by the separator's lead, and a doubled
line therefore spends only 28 of its 77 units of it -- the picture on composite
sits about 97 output pixels left of where separate puts it at that mode. An
undoubled line has no head blanking and reaches within 12 units, which costs 24
px of width at 640x480@60.

**Why the blanking is on one arrangement and not the other is not known.** It is
not the lead moving a fixed feature: that would put it EARLIER in the counter on
the separator's path and it reads later.

What would settle it: the same creep at a second divider, which needs a doubled
source that is not 15 kHz, or `IF_HBIN_SP` swept against it.

### `Memory::FetchFloor` drives the playback ratio off the bottom of its band

`Memory::fetchFor()` is `max(FetchFloor, ceil(captureWidth / RequestsPerLine))`
with `FetchFloor` 150, and the header says the division is there so "the
capture/fetch ratio [is held] fixed as the picture zooms, so no framing can walk
into a tearing band". **The floor breaks exactly that.** Once `capture / 4`
falls below 150 the fetch stops following the capture and the ratio falls with
every further zoom step.

`PB_FETCH_NUM` swept by hand at `VDS_HSCALE` 275 on `X720 Y576 C256 F50`,
capture 456, `PB_CAP_OFFSET` 442, automation frozen:

| `PB_FETCH_NUM` | capture/fetch | picture |
|---|---|---|
| 114 = `ceil(456 / 4)` | 4.00 | clean |
| 128 = `FetchMin` | 3.56 | clean |
| 140 | 3.26 | breaking up |
| **150 = `FetchFloor`** | 3.04 | **blocks of other content through flat colour** |
| 170 .. 320 | 2.68 .. 1.43 | broken, no better |

So the band has a floor as well as the ceiling already measured -- clean to
4.04, tearing from 4.28 in
`investigations/horizontal-scale-corruption.md` -- and it is roughly
**3.4 to 4.04**. The low side had never been measured, which the constant's own
comment says: it stops "where the measurement stops".

**The artefact is a different class from the write floor's.** It puts blocks of
other content into saturated flat colour, which a resampling slip cannot do,
and it is gross enough for a camera to score where the wedge artefacts are not.
At capture 456 the rule's own value of 114 is clean and the floor's 150 is not,
so the floor is not a safe clamp.

**Where the floor came from, and why it is the wrong shape.** The rule was
fitted over capture **737..1185** only, so below about 600 it extrapolates, and
the floor was put there to stop it doing so. It was not idle caution: an
earlier linear fit put 236 at capture 1009 and **wrapped the picture**, the
frame's start reappearing down the right, so a fetch too small to cover the
line is a real failure and there is a real floor.

But that floor was measured to **rise with the capture width** -- between 236
and 256 at capture 1009, which brackets `1009 / 4 = 252`. It is proportional,
and `ceil(capture / 4)` already expresses it. A constant of 150 therefore sits
below the proportional value above capture 600, where it does nothing, and
above it below capture 600, where it is the fault measured here. The guard was
right and the shape was not.

**Fixed, and verified from `VDS_HSCALE` 275 down to the scale clamp.** The
constant floors are gone and `ceil(capture / 4)` governs throughout. Measured
on the flashed build: the engine writes 114 at capture 456 by itself, where it
wrote 150 before, and the ratio holds 3.98..4.00 at every step down to
the horizontal floor -- capture 396, fetch **99**, below both constants that were
removed -- with the colour bars, the grey bands and the circle arcs clean at
all of them.

**So every write-floor mark below about `VDS_HSCALE` 308 was taken with this
artefact present.** Capture runs about `2.02 x scale - 111` on the bench
sources, so the ratio crosses 3.4 at capture 510, which is scale 308: below
that the fetch was pinned at 150 and the playback artefact was in the picture
alongside whatever else was. **The marks in that band cannot separate the two
classes and the band wants re-sweeping on the fixed build.** That covers the
whole of the 257..284 sweep and the lower half of 285..334.

**`FetchMin` 128 is unmeasured too, and 114 works.** Nothing in the header
justifies it, and the sweep is clean below it. That matters because clamping at
128 instead only moves the problem: `capture / 4 < 128` from capture 512 down,
so the ratio still falls, reaching 3.4 at about capture 435.

### A framing can reach a state the zoom cannot leave

**The inward half of this is fixed.** `VideoPath::zoom()` stops the capture at
`Axis::minimumCapture()`, where the scale reaches its floor, so pressing zoom-in
can no longer walk a framing below what the running build allows -- measured on
the bench, the capture parks at 574 units horizontally and 361 vertically and 20
further presses move nothing. What follows is the OUTWARD half, which is
unchanged and was measured under the old inward behaviour.

At the horizontal floor with a small capture the outward zoom stops being applied:
measured at `VDS_HSCALE` 256, `/sc?O` grew the capture 396 -> 406, two units a
press, and then moved nothing for as long as it was pressed. The scale is
pinned there so only the capture can answer, and when it stops the control is
inert -- the picture does not move and nothing says why. `/sc?B` recovers it
and is the only thing found that does.

The state was reached by restoring a framing saved under a build with a
different magnification floor: a capture of 396 against a floor of 480, so the
framing was outside its own bounds before any press arrived. `Axis::minimum
Capture()` is the room the raster offers over the magnification now -- 594 at
the bench raster -- and a framing arriving below it is left where it is rather
than widened, so zoom-in is a no-op there and zoom-out still answers. Whether a
framing solved by the running build can reach the same place is not
established.

**A framing outlives a flash.** It is held state and it is restored on boot, so
a build whose floor differs from the one that saved it starts out of range.

### The stride is clamped by a bound belonging to the fetch

`Memory::offsetFor()` delegates to `fetchFor()`, which clamps at
`FetchMax` 512. But the stride's own bound is `OffsetMax` 1023 -- both
registers are ten bits -- so a line long enough to ask for more than 512 gets a
stride below what it needs, and a stride below the fetch overlaps successive
lines.

The clamp bites from `lineUnits` 2048 up. The bench sources do not reach it --
`X720 Y576` gives 1766 and so a stride of 442, the 320x256 mode's doubled line
gives 1100 -- so this is arithmetic rather than an observed fault, and which
source mode reaches a line that long is not established. `fetchFor()` is the
right shape for the fetch; the stride wants the bound that belongs to it.

### How far the scaler magnifies is a picture-quality choice, and 4.25x works

**The floor is the AXIS's, and the two axes carry different ones**:
`Axis::magnificationFloor()` is 342 horizontally, where the scaler corrupts, and
205 vertically, which is 5.0x and has no measurement against it. `Scale` keeps
`Unity` and `Max = 1023`, the 10-bit field and the only bound the part states.

A shared constant was carried before that, on the grounds that there is no
register limit at the bottom to name -- RD-5725-1.1 gives only
`HSCALE = 1024 x in / out` -- so the floor was one fact described twice. That is
right about the register and wrong about the axes: everything measured behind
342 is horizontal, so the shared value put a horizontal finding on `VDS_VSCALE`
as well and letterboxed every source too short to reach 3.0x.

What remains open is WHERE either floor should sit, which no measurement settles.

**4.0x is not a wall: 4.25x works.** Built with the floor passed to the two
`Axis` constructors lowered to 128 and flashed, the engine solves `VDS_HSCALE`
241 at capture 396 and the picture holds -- colour bars, grey bands and the
circle's arcs all clean, with no data corruption. What degrades is sharpness:
the wedge aliases and the label text smears, which is the source running out of
detail to magnify rather than the part failing. So the ceiling is the
picture-quality judgement the entry says it is, and where it sits is the user's
to find by zooming. How far past 4.25x it stays acceptable is unmeasured.

**THE MECHANISM IS NOT ESTABLISHED, AND THE WRITE FLOOR IS A CORRELATE.** What
is known is that the horizontal scaler corrupts the picture below about
`VDS_HSCALE` 334 and that no fix was found. The centring account -- that the
solve can no longer place the picture and pins the memory window at the write
floor, and that the scaler picks wrong samples there -- states a causal chain
the measurements do not carry; both sources entered the floor at the same
`VDS_HSCALE`, which is where the correlation comes from. Do not quote it as the
reason.

**IT WAS MEASURED ON `VDS_HSCALE` AND APPLIED TO BOTH AXES, AND THE AXES ARE
SEPARATE NOW.** Every reading behind 342 is horizontal -- the register, the two
rasters, and the artefact itself, which is bars splitting across a line.
`VDS_VSCALE` is a different register in a different stage and is untested at any
magnification, so `AxisVertical` carries 205 -- 5.0x -- and `AxisHorizontal`
keeps 342. **The vertical value is a choice with no measurement against it**, and
the register clamp is what enforces it.

Measured on the AKF50 set, at the engine's own framing, black rows top and
bottom of the emitted frame:

| mode | needs | before | after |
|---|---|---|---|
| X384 Y288 F70 | 3.75x | 89 + 129 | 0 + 1 |
| X240 Y352 F70 | 3.07x | 0 + 27 | 0 + 1 |
| X480 Y352 F70 | 3.75x | 89 + 129 | fills, and OVERRUNS |

The other 25 modes are unchanged on both axes, which is what the arithmetic
predicted: none of them magnifies past 2.99x vertically. What the zoom loses is
crop depth -- the vertical extent at the stop went from 361 to 217 on one host
raster.

**Whether the onset depends on the source is open.** The floor was found by a
zoom sweep, where the capture shrinks as the magnification rises, so the two
move together and no reading separates them. The AKF50 set spans captures of 688
to 1228 IF units at magnifications of 1.21x to 2.43x, which is the pairing the
sweep never produced.

**The scale-clamped zone is no longer reachable by the control.** It was clean
only because 1024/256 is exact, and 1024/342 is 2.994. That costs nothing,
because `Axis::minimumCapture()` stops the zoom where the scale REACHES its
floor, so no press can now solve a framing inside the clamped zone at all. It is
reachable only by a framing restored from the table -- the entry two above.

### Why an even memory window shears is not known

The zoom shear itself is fixed: `Axis::solve()` biases the memory window to an
odd width, because an EVEN `VDS_HB_ST - VDS_HB_SP` shears the picture and an odd
one is clean. That width is `floor(originOffset + produced)`, so what reaches the
picture is the produced width's parity rather than any register.
`investigations/horizontal-scale-corruption.md` has the
measurements and the two refuted rules, which must not be reinstated.

**The bias is a bias, not a cure.** Nothing explains why an even width shears, so
anything that later does should be expected to replace it rather than build on
it. Two things are open and each is one bench session:

**And an odd width is not sufficient.** The output raster total carries a parity
of its own: measured at 1024p with the capture, the scale, both windows and the
divider held, `VDS_HSYNC_RST` alternates the picture clean/corrupt on six
consecutive values, and a state with an odd memory window is corrupt at every
even one. `OutputMode::horizontalTotalFor()` therefore rounds the total up to
even, so the register lands odd.
`investigations/horizontal-scale-corruption.md` -- and it is
measured at one framing on one mode, so the sense is not established elsewhere.

- **One input and one axis.** The bias holds on every source mode tried -- 640
  solves swept across eight, 419 to 768 lines at 50, 60 and 70 Hz, from 320x250
  to 1024x768, with no even width -- but all of them are the RiscPC on `vga`.
  The Wii on `ypbpr` has not been zoomed against it, and `VDS_VB_SP` has never
  been crept, so the vertical axis is unmeasured rather than unaffected.
- **A register write flashes the picture.** Every write during a jog gives a
  visible flash before the picture settles. The camera cannot see it -- a burst
  after a write differs frame to frame by the same 2.2 grey levels as a burst
  with no write, which is the camera's noise floor -- so it is brief. It may be
  the two-byte fields being written a byte at a time.

### The default capture is 1.7% narrower than the mode's active region

**Closed by measurement, 2026-09-24.** It was the capture lag, and the lag is
gone -- the whole of it was `SP_HS_LOOP_SEL` taking the retiming module out of
circuit, so `CaptureLagFraction` and `FrameLagUnits` no longer exist.
`investigations/the-capture-lag-was-the-retiming-bypassed.md`.

On the same mode, 800x600@60 on `vga`, `/geometry` now reports:

| | value | the mode's |
|---|---|---|
| `poh` | 0.2043 | `(128+88)/1056` = 0.2045 |
| `peh` | **0.7575** | `800/1056` = **0.7576** |

against the **0.7405** this entry was opened on.

**The asymmetry went with it.** The entry's evidence was that a 100% framing
showed the source's blanking on one side only, because the far bound was
`lastCapture - lag`. Forced full on the same mode, `IF_HB_ST2` is **1438 on a
1439-unit line**, which is `lastCapture` exactly -- nothing is subtracted at the
far end. The near end is the sync interval and nothing else. Photographed, the
panel shows the source's blanking on all four sides.

**WHAT THIS DOES NOT CLOSE is the 2.3% size difference against bypass**, and
the temptation to join them is why this says so. The two were never the same
measurement: the bypass difference is SYMMETRIC about the centre and a capture
shortfall is not, so agreement between `0.7576/0.7405` and 2.29% was a
coincidence of magnitudes. See the bypass entries.

### The capture origin's scan-mode offset was measured against one source

**Closed, and the constant went with it.** The same framing took different
picture where the line doubler was bypassed -- the source's flashing border down
the right and across the bottom at 480p and 576p, none at 1080p. It was first
fixed by tuning a lag, `CaptureLagFraction` 0.0539 with `FrameLagUnits` as its
vertical half, which took the flashing to zero columns and rows at all three
modes against 26 to 28 and 14 to 16 before.

**Both constants are now deleted**: the displacement was `SP_HS_LOOP_SEL`
taking the sync retiming out of circuit, and engaging the retiming accounts for
the whole of it -- 77.4 counter units measured against the 77.6 the correction
was applying. What is below is the reasoning about the constant's FORM, kept
because it says what a future displacement would have to be measured as.
`investigations/the-capture-lag-was-the-retiming-bypassed.md`.

The frame's form is settled -- a count of counter units, see the entry on it
below -- and the line's is not:

- **`CaptureLagFraction` now disagrees with the four-mode table it came from.**
  Those readings were absolute -- a knee against each mode's stated timings --
  and they are used as the difference between the scan modes, which is what is
  measured now. Read against the mode file, each counter is out by a further
  0.010 to 0.020 of a line; that is the knee's own bias, shared by both modes
  and cancelling out of the difference, rather than a second finding. It does
  mean 800x600@60 and the other undoubled-only sources move by about 11 units,
  and nothing has judged those since.

`docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md`.

### `IF_VB_ST` does not bound the capture on the scaling path

**Measured on both scan modes, and the register is inert in both.** With
automation frozen at 1080p on the bench source, `IF_VB_ST` was taken from 575 to
515 and then to 300 -- a capture less than half the height the engine believes
-- and the picture is unchanged, every row within 0.8 grey levels of the full
capture over a thousand columns. At 576p, undoubled, 120 units off the stop
leaves the card complete with its corners in the panel's corners.

So the vertical capture's far end reaches the picture through nothing. What
bounds the picture vertically is the scale and the two output windows:
`produced` is `capture x 1024 / VDS_VSCALE`, and the playback fetch reads that
many rows from the write origin, so a write that runs longer than the fetch is
simply not read. `IF_VB_SP` IS live -- the vertical creeps in
`the-transmitted-window-is-a-per-mode-fraction.md` move the picture one for one
off it.

**What this invalidates:** any reasoning that treats the vertical capture as
bounded at `IF_VB_ST`, including reading `ev` off `/geometry` as the number of
source lines the chip is writing. The engine's `ev` is what it SOLVED for, and
the scale is derived from it, so the picture is the right height -- but the
write is not being stopped where the register says.

**What is not established:** what does stop it, and whether the rows written
past the fetch cost anything. The horizontal equivalent is live
(`IF_HB_ST2` bounds the line), so this is not a property the two axes share.

### The vertical aperture's far end took two rows nothing could reproduce

**Closed.** `Axis::solve()` subtracted the magnification and a further output
row from the vertical write end. The magnification is the interpolator reading
one capture unit past the last one written; the extra row came from a
castellation ladder at 1080p. Together they blanked 2.6 output rows at the bench
800x600@60 framing, which there is the source's last picture line.

Crept with `PATTERN CARD`, whose green frame line is the source's last row:

| framing | ladder | what moved |
|---|---|---|
| 800x600@60 into 960p, magnification 2.50, the source's last picture line as the capture's last unit | solved 994 out to the bound 998 | a row of picture per step, the falloff keeping its shape, no row of stale memory at any of them |
| 320x256@50 into 1080p, capture 582 at `VDS_VSCALE` 552 | 1112 to 1121 | **nothing at all**, ten values, profiles identical to a grey level |

The second is the framing BOTH measurements behind those terms were taken at, so
neither can be reproduced: the register is not what ends the picture there. And
the interpolation reading measured the CAPTURE rather than the aperture -- a
stale line that cleared when the capture took one more line -- so the aperture
correction was the modelled response to it rather than a reading of it.

**Horizontally the term stays and the axis is untested.** At 960p the
transmitted window ends before the aperture does, so creeping `VDS_DIS_HB_ST`
over the same range moves nothing and the panel cannot see the far end at all.
The parity bias and the memory window are untouched and `VDS_HB_ST` solves to
the same value it did.

**What is left is sub-row granularity.** The write ends at a fraction and the
aperture closes on the floor of it, so up to a row is still blanked: at the
800x600@60 default the write ends 997.98 rows in and the aperture closes at 997,
losing a row that is 98% written. Whether that should round rather than floor is
untried, and the guard against it is `blanking starts no later than the write
ends`.

### Bypass shows the source's border, and nothing on the board can hide it

**The constant is gone and the symptom is not.** `HdBypass` wrote `0x90` into
`HD_HB_SP` whatever the source was; it is the engine's own envelope now,
`AxisHorizontal::activeStart()` of the played-out line. That removed a magic
number and changed no picture, because the constant was INERT.

Measured in bypass with the RiscPC at `MODE X800 Y600 C256 F60`, whose mode file
states `h_timings:128,48,40,800,40,0` on a 1056 pixel line, stepping the
register against the panel on a 2048 sample line:

| `HD_HB_SP` | 144 | 240 | 320 | 400 | 480 | 560 |
|---|---|---|---|---|---|---|
| blanked columns | 0 | 0 | 0 | 36 | 117 | 198 |
| border columns | 78 | 78 | 78 | 40 | 0 | 0 |

The panel's own left edge falls at sample **364**, so everything below about 400
blanks nothing that was visible anyway. The border is in the SIGNAL: bypass
passes the source's raster through, and this source spends 40 pixels a side on
border.

**Hiding it would crop real picture.** The border clears at about sample 441,
which is 0.215 of the line, against the envelope's 0.117. Common PC modes spend
less than that on sync and back porch -- 640x480 is 18.0% and 800x600@60 is
20.5% -- so a bypass blanking tuned to this source eats their picture, and
bypass has no framing control to give it back with. **Blanking cannot be
auto-detected**, because a border is black active video electrically identical
to back porch, so there is nothing to measure per source either.

**What can actually remove it is the SOURCE.** `RetroScaler-Acorn.mdf` states
the borders -- 44 pixels a side at 320x256, 40 at 800x600 -- and it is ours. A
mode file entry with zero borders sends no border to hide.

**The asymmetry is the panel's.** The band appears at the left and not at the
right because the painted area starts after the line does and ends before it
does; the right border falls off the end. Do not read it as the scaler placing
the picture wrongly.

### 384x288@70 into 720p is shown small, black on all four sides

One state, one run: the default framing of 384x288@70 into 720p puts the card
at about two thirds of the frame with black on every side, where 240x352@70
on the same raster fills it and 384x288@70 into 1080p fills it with 11
columns of black at the left. The report names the row that answered as
240x352@70 at 720p and 384x288@70 at 1080p, so the capture appears to have
taken a different row's active region on the second output. Not pursued.
`sweeps/framing-20260930T101647Z.jsonl.gz`.

### The output sync pad lowered outside the engine stays down until the next solve

Measured on the bench after an OTA flash: no picture at all, with
`PAD_SYNC_OUT_ENZ` **1 in 728 of 728 samples over 12 s** and every other
register perfect -- `DAC_RGBS_PWDNZ` 1, `PLLAD_MD` 1446 against
`STATUS_SYNC_PROC_HTOTAL` 1446, `STATUS_MISC_PLLAD_LOCK` 1, `HPERIOD_IF` 213 at
the 524/60 that mode is due, the raster steady at 1599 x 1124, and the engine
still solving. Writing the bit to 0 by hand restored the picture at once and it
stayed 0 for 639 of 639 samples, so nothing was re-arming it: the hold was
issued once and never released.

The pad is raised by `VideoSourceAcquisition::presentWhenSettled()`, which
asks on every detection pass whether the source is acquired with the output
away, and by every solve made while the output is shown -- so a drop the
engine made itself is always answered. What is not is a writer outside it,
`Chip::outputDown()` or the bring-up's own static registers, lowering the pad
while the engine still holds it as driven: `outputShown()` then reads true
and nothing re-asserts it until the next solve or transition, and a register
dump cannot tell that state from a healthy one. Which writer lowered it here
is not established; the relook release that was the third candidate no longer
exists.

The one-line recovery, which needs no reflash and no bench trip:

    python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set PAD_SYNC_OUT_ENZ=0### `STATUS_SYNC_PROC_HSACT` saturates in BOTH directions, and five decisions hang off it

It reports the sync processor's own state rather than whether a signal is
arriving, and both rails have now been measured on this bench within one day:

| state | reading |
|---|---|
| a YPbPr source arriving, which then acquired | **0** across a 450 ms window, some 45 samples |
| nothing counted at all, `VTOTAL` 0, separator swept 1..20 | **1** at every one of the twenty levels |

The second is the new one and it is the more misleading, because the bit reads
TRUE while the sync processor counts nothing whatsoever. It is not a duty cycle
like `STATUS_MISC_PLLAD_LOCK`: it does not flicker, it rails.

**Five decisions key on it**, and each rail breaks a different one:

| site | what it gates | broken by |
|---|---|---|
| `SyncProcessor::acquireClampWindow()` | returns false, so the clamp window is NOT PLACED | stuck 0 |
| `SyncProcessor::acquireCoastWindow()` | returns false, so the coast window is NOT PLACED | stuck 0 |
| `SyncOnGreen::edgesHeld()` | the separator walk's success test, 60 reads in 60 ms | stuck 1 |
| `SyncOnGreen::separatorHolds()` | any 0 in the run means not held | stuck 1 |
| `detectAndSwitchToActiveInput()` | whether detection looks at all | both |

**Stuck 1 makes the separator walk a no-op.** `edgesHeld()` passes at the first
level tried, so `SyncOnGreen::acquire()` returns without searching and the
discrimination rests entirely on `separatorHolds()`. That is measured, not
inferred: sweeping `ADC_SOGCTRL` 1..20 on a frozen unit gave `HSACT` 1 at every
level with `VTOTAL` 0 at every level.

**Stuck 0 is a candidate for a self-sustaining stall, and it is NOT yet
established.** If the clamp and the coast windows are refused, the sync
processor is left unconfigured -- and an unconfigured sync processor reads
`STATUS_SYNC_PROC_HTOTAL` wrong and does not move when a divider is written and
latched by hand, which is recorded in CLAUDE.md. That would close the loop:
`HSACT` 0, no clamp, no coast, nothing counted, `HSACT` stays 0. It matches the
`count=0 ht=0 lock=0` measured across a whole 6 s window on a selection that
then acquired at 15 s.

**That loop is REFUTED, and cheaply.** Detection's entry gate was moved to
`SyncProcessor::signalPresent()`, which counts transitions on the test bus and
does not rail, and the passes that found nothing went on finding nothing:
acquisition measured 8.32 s against 8.27 before, with the same five failed
passes. The two instruments agree, so the stall is not the instrument and not
the windows.

**What the five passes were actually waiting for was the absence run's own
threshold.** The teardown at pass five is what makes the source appear -- sync
arrives 0.6 s after it and detection then succeeds in 25 ms -- so the fix was to
spend that patience on a deliberate selection rather than to change any
instrument. `SourceAbsence::selectionChanged()`.

Removing the gate from the two window writers stands on its own, as an owner
removed: whether a source is worth writing for is
`VideoSourceAcquisition::mayWriteForSource()`, which is a count in range plus a
steadiness run.

`SyncProcessor::signalPresent()` counts transitions on the test bus and does not
rail. Detection currently uses it only to decide whether to GIVE UP, never to
decide whether to look.

### A teardown can leave the chip's blocks held in reset after the source acquires

**The screen is black and every configuration register reads correct.** Measured
on `ypbpr` with the Wii at 480p, `/geometry` reporting `state: acquired` at 525
lines and 31468 Hz, `present: true`, both scales matching their windows:

```
SFTRST_MEM_RSTZ  SFTRST_MEM_FF_RSTZ  SFTRST_FIFO_RSTZ
SFTRST_DEINT_RSTZ  SFTRST_OSD_RSTZ          all 0      s0_46 = 0x41
DAC_RGBS_S1EN 0        PLL_MS 2   PLL_R 0   PLL_S 2 -> 0
```

Those five are **active low**, so 0 is a block held in reset and no video
crosses the part. They are what `Tv5725::BringUp::holdAllBlocks()` writes inside
`setResetParameters()`, on the low-power teardown. The source acquired
afterwards and nothing released them.

**Writing the five bits to 1 by hand restores output immediately**, with nothing
else touched -- the capture goes from a black frame to a 1809x1075 picture. The
display PLL divisors are on their reset values in the same state, so what comes
back is structurally wrong until a full re-init runs.

**A FLASH IS WHAT REPRODUCES IT**, twice in four OTA uploads. The unit comes
back, detection runs, the source is acquired at the right count and rate, and
the blocks are still down -- `s0_46` reading 0x41 against the 0x7f a working
state holds, with `s0_45` 0x01 and `PAD_TRI_ENZ` 1 beside it.

`/sc?~` clears it, though not always on the first call: one occasion needed a
second, so the release is path-dependent rather than absent. What is
established is that acquisition does not guarantee it, and that a clean
register dump does not clear the board -- which is why
`bench-output-capture.md` asks `s0_46` first, and why the reading it asks for
is the one that answers in a single request.

### The field rate reads exactly double after a sync reset

`SourceMeasurement::sampleFieldRateHz()` returns exactly twice the source's field
rate in the window after the sync processor is reset -- two or three consecutive
samples, clearing in a few hundred milliseconds. On **both** bench inputs: the Wii
at 480p reads 119.87 against 59.93, and the RiscPC at 320x256@50 reads 100.16
against 50.08.

`rateFollowsCount()` hides it wherever the count has not moved, reporting `line
rate 0`. **A change of input is the case that guard is deliberately off for**, so
a doubled sample admitted there becomes the line rate -- 62936 Hz, exactly twice
31468 -- and the engine sizes a divider from it.

**FOUR EXPLANATIONS ARE REFUTED AND THE CAUSE IS NOT KNOWN.** Do not reinstate
one without new evidence; the measurements are in the investigation.

| refuted | what closed it |
|---|---|
| a half-applied scan | the five scan registers are one write now, and three consecutive samples still read 119.87 with the set wholly agreeing |
| the doubled scan itself | the everyday 311-line `vga` source is captured doubled all day and reads 50.08 |
| the reference divider | held at 2506 with `/sampleclock?hold=2506`, the identical register state reads 50.08 twice over -- and a 1400 progressive reference pair still doubles |
| the sync arrangement, or the Wii | `vga` on separate sync reads 100.16 three consecutive samples once its source is line-doubled |
| the coast | `SP_PRE_COAST`/`SP_POST_COAST` read 7/6 unchanged through the doubled samples and the correct ones |

What is left is the window itself: after the sync reset, before the source
settles. **A 0.4 s HTTP poll cannot resolve it** -- the doubled samples and the
correct ones are 300 ms apart -- so the next instrument is `Tv5725::SamplingLog`
through that window rather than more point reads.

**THE COST IS GONE EVEN THOUGH THE FAULT IS NOT.** With the reference pair
STATED -- `Adc::BringUpDivider` at `Adc::BringUpLineRateHz`, carried undoubled
by `Adc::BringUpLineDoubled` -- the first ACCEPTED sample is the correct one, so
`ypbpr` solves one divider per selection instead of three and no line counter is
refused at all. Measured on both pairs the reference has held, so it follows from
the pair being stated and self-consistent rather than from its value.

**Do not read it as ordinary settling.** 119.87 sits inside the 60..160 Hz band
the console table calls `getSourceFieldRate()` settling, which is what has let it
pass as normal -- but a settling analog path gives arbitrary values, not three
consecutive samples at exactly 2.0000x.
`docs/investigations/the-field-rate-reads-exactly-double-after-a-sync-reset.md`.

### After a flash the output can be featureless vertical stripes, with every register correct

The emitted frame carries horizontal structure repeated down every row and no
vertical structure at all -- one line played out for the whole frame. Seen twice
in one session, both times within a minute of an OTA flash, on `ypbpr` with the
Wii at 576i.

**Nothing in a register dump distinguishes it.** `state: acquired`, count 310
against `VPERIOD_IF` 624, `PLLAD_MD` 2200 equal to `STATUS_SYNC_PROC_HTOTAL`,
`DAC_RGBS_PWDNZ` 1, and `/geometry` self-consistent -- capture 1101x622, scales
and windows all solved. The dongle sees the stripes, so it is the board's output
rather than the television.

**An input round trip clears it**: `/input?src=vga`, then back. Both occurrences
recovered that way at once, and the other input is clean throughout, which is
what rules the board out.

The scaling path is not the only thing that has to be right for a frame to come
out, and this is the one state measured where everything the engine sets is
correct and the played-out frame is not.

### A YPbPr detection that succeeds on its first pass skips the only preparation

**This is the acquisition stall on `ypbpr`, and it is distinct from the absence
run below**: `state: absent` here holds the previous source's solve with a
signal reaching the chip, where that one stalls with detection unable to claim
a signal.

The preparation a new source needs is reached only through detection's FAILURE
branch: `syncFound 0` with no signal present goes to
`goLowPowerWithInputDetection()`, which calls `setResetParameters()`, which
installs `Adc::BringUpDivider` as a reference sampling clock. A first pass
returning 2 skips it, and the chip then measures the new source through the
previous source's ADC clock. **The healthy path depends on detection failing**,
and nothing on the selection path prepares the chip.

Measured over 32 `ypbpr` acquisitions, cycling `/input` against `vga`, the
correlation is exact with no exceptions either way:

| | first `DETECT` | low-power passes | acquired in | n |
|---|---|---|---|---|
| healthy | 441..464 ms, `syncFound 0` | 1 | 4.8..7.1 s | 27 |
| wedged | 96..441 ms, **`syncFound 2`** | 0 | 25.2..32.1 s | 5 |

The determinant is `syncFound`, not the duration -- one wedge claimed the source
after 441 ms, longer than several healthy first passes. **The rate is erratic**:
1 in 20, then 4 in 12, then 0 in 12, pooled 5 in 44. So absence over a dozen
cycles is not evidence that a change fixed it.

What the wedged chip holds, against a healthy `ypbpr` acquisition read the same
way. **A `vga` baseline does not serve** -- that is the RGBHV route, where
`SP_H_PROTECT` is 0 and the clamp is not held, so two rows read as faults
against it and neither is one on that input:

| | wedged | healthy `ypbpr` |
|---|---|---|
| `STATUS_SYNC_PROC_VTOTAL` | 97..105 | 524 |
| `STATUS_SYNC_PROC_HSACT` | 0 | 1 |
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 1438 / 1438, `vga`'s | 1448 / 1448 |
| `SP_CLAMP_MANUAL` | 1 | 0 |
| `SP_H_PROTECT` | 1 | 0 |
| `HPERIOD_IF` | 13..14 | 214 |

**`SFTRST_SYNC_RSTZ` ALONE DOES NOT CLEAR IT, AND THE CLAIM THAT IT DOES IS
REFUTED.** Measured three times with `/freeze?on=1` holding the ladder off so
nothing else could act: the pulse restarts the horizontal side --
`STATUS_MISC_PLLAD_LOCK` 0 -> 1, `HPERIOD_IF` 14 -> 214 -- and `VTOTAL` stays 97
while the lock decays again. A `PLLAD_LAT` rising edge changes nothing, three
times, the divider in force being the one the register already holds. The earlier
claim rested on re-selecting the same input, which also runs `LoadDefault()` and
re-applies the input registers, so it isolated nothing.

The 25..32 s is the ladder: 15 s of first-acquisition hold, then rungs at about
ten a second to `FullReset` at position 150. The early rungs move it into a
second state rather than fixing it -- divider 1448 with `VTOTAL` 524 correct and
`HTOTAL` pinned 1704..1706, lock 0 in every sample over 13 s, which is a
free-running VCO at about 53.6 MHz. `RestartSamplingClock`, the rung written for
exactly that, fires at position 60 and changes nothing; `FullReset` clears it in
under a second.

**And the reordering that made the fault three times more frequent is explained
by this rather than by the mux.** Letting the mux settle before detection runs
makes a first-pass claim MORE likely, so the preparation ran less often.

**The count gate the RGB branch has is not the fix.** `countIsSource(97)` is
false, so that gate would reject this state -- but on the YPbPr branch nothing
can be counted until a divider is sized for the arriving source, which happens
after detection returns, so it would reject every first pass.

**What repairs it is the selection path installing a reference clock.**
`applyInputSelection()` calls `Adc::installReferenceSamplingClock()` before it
resets the sync processor, so the block comes out of reset with a clock the
arriving source can be counted through whether or not detection's first pass
claims it.

Measured over 70 `ypbpr` acquisitions afterwards: **no stall**, and the one pass
that did claim the source acquired in **1.6 s**, against 25.2..32.1 s for all
five that claimed it before. That one is the evidence, being the condition rather
than its absence. It also became rarer, 1 in 70 against 5 in 44, because
installing and latching the group leaves the sync processor with nothing to
report for a moment, so the first pass usually fails and the preparation runs by
design rather than by luck.

**THAT ONE OBSERVATION IS THE WHOLE OF THE SURVIVAL EVIDENCE, AND A SECOND ONE
DISAGREES WITH IT.** Caught on a `GBS_SAMPLING_LOG=1` build with the log running
at 25 ms, a `vga` -> `ypbpr` selection reported `DETECT: 29ms, syncFound 2` and
then held `STATUS_SYNC_PROC_VTOTAL` 97 with `HSACT` 0 for **15 s**, until the
ladder lifted it at `recovery: lift SOG floor at pass 2` and it acquired about
1.3 s later -- roughly 16 s in all, which is the ladder's own hold rather than a
repair.

**The preparation had plainly run**: `PLLAD_MD` 2506 with
`STATUS_SYNC_PROC_HTOTAL` 2506 beside it in every sample through the stall, so
this is not the pre-repair state, which held the previous source's 1438. It is
the reference clock in force and the sync processor still not counting -- the
shape the absence-run entry describes rather than the shape this one does.

**It is confounded and must not be read as a regression.** A 25 ms sampling log
loads `loop()`, which is the thread detection and the ladder both run on. What it
does establish is that "the fault is survived" rests on n=1 and has one
disagreeing observation against it. **Get a real n for first-pass claims on the
default build before relying on it** -- the condition is about 1 in 70, so that
is a long cycling run rather than a quick check, and `syncFound 2` on the first
`DETECT` after a selection is what identifies one.

Installing the bare divider instead is refused: `PLLAD_MD` 2506 written into a
live wedge leaves the count at 97, and `IF_HSYNC_RST` cannot hold 2506 so it is
left describing another line. The whole PLL group is what
`installReferenceSamplingClock()` writes, and the write lands before the
`PLLAD_LAT` rising edge so the PLL leaves on it.
`docs/investigations/a-ypbpr-detection-that-succeeds-first-pass-skips-the-preparation.md`.

### The absence run retries the teardown rather than stalling

**Fixed, and recorded because the shape recurs.** `SourceAbsence::undecided()`
was a no-op: detection finding nothing while a signal IS reaching the sync
processor was treated as neither evidence, so the run neither advanced nor ended
and the teardown was reachable only through `missed()`. A source keeping
something on the test bus while detection could not claim it therefore stalled
**indefinitely** -- measured on `ypbpr`, `state: absent` across 150 s of polling
holding the previous source's solve (`cv` 628 at 37879 Hz on a 525-line source),
`SP_VTOTAL` 97, `HSACT` 0, `PLLAD_MD` still on `vga`'s 1438. Waking the source
did not clear it, the stall outlasting the source returning by 45 s, so it was
the engine rather than the source. `/sc?~` cleared it in 6.2 s and nothing on the
board would have.

**A second guard made the teardown a one-shot**, which is what turned a slow
recovery into no recovery: the call site ran it only `if (rto->isInLowPowerMode
== false)`, and that flag is cleared only where detection claims a source. So a
source that was never claimed got exactly one teardown and then nothing, however
long the run counted.

Both are repaired. `undecided()` advances the run as `missed()` does,
`SourceAbsence::poweredDown()` re-arms it once a teardown has been made, and the
call site acts on every threshold instead of once. **Recovery is retried and
never abandoned.**

**The reproduction is deterministic and is the regression test worth keeping**:
selecting `ypbpr` twice about 1.5 s apart from a settled `vga` acquisition.
Before, 6 of 6 held `state: absent` for the full 90 s each attempt was given,
with `VTOTAL` 0, `HSACT` 0 and `PLLAD_MD` 2506 -- the reference divider showing
the one teardown had run. After, 0 of 6, acquiring in 4.1..6.4 s.

**A quiet console is what this fault looks like, not evidence against it.**
`SYNC_EVENT` needs `GBS_SAMPLING_LOG=1`, and `SamplingLog::event()`
de-duplicates identical consecutive events, so a ladder repeating one branch
prints once and then goes silent.

**The selection edge now says what it did, which is what the wedge above is
gated on.** `applyInputSelection()` reports once the sequence is complete --
`input selected: ypbpr, reference divider 2506, reset +18ms, registers +18ms,
saved +24ms` -- and the guard is that the line precedes detection's first
`DETECT`, not that an acquisition came in under some number of seconds. Neither
the duration nor the condition repeats well enough to gate on: the two duration
distributions overlap, and a first-pass claim was 1 in 20, then 4 in 12, then 0
in 12. The ordering repeats on every selection.

### The encoder holds stale timing with the sync pad correctly low, and nothing re-triggers a re-look

**This is not the latched-down pad above, and reading it as that one wastes the
session.** `PAD_SYNC_OUT_ENZ` reads **0**, which is correct, and the sink still
shows nothing.

Measured on the bench with the Wii on `ypbpr` at 480p, every software-visible
signal healthy: `/geometry` `state: acquired`, 525 lines at 31468 Hz;
`PLLAD_MD` 1448 against `STATUS_SYNC_PROC_HTOTAL` 1448; `DAC_RGBS_PWDNZ` 1;
the scaling path (`DAC_RGBS_BYPS2DAC` 0, `OUT_SYNC_SEL` 0); both scales matching
their display windows to within a pixel. The console carried

    frame time lock: ... in 59939 mHz, out 59939 -> 59939 mHz, clock 105324360

every 1.5 s, and the `out` term is the TV5725's own VSOUT sampled on
`DEBUG_IN_PIN` -- so the scaler was feeding the encoder throughout. The HDMI
capture read mean luma **0.00**; a `PAD_SYNC_OUT_ENZ` 0 -> 1 -> 0 toggle by hand
brought it back to **156.61** within 9 s, with no power cycle and no register
otherwise touched.

**What has no automatic exit is the trigger.** The sync pad is taken away on
a source mode change and on an output change and on nothing else. A unit left
settled, or a sink that dropped the link and re-acquired while the board's
timing never moved, arms nothing -- and the state is indistinguishable from a
healthy one in any register dump, because every register IS healthy.

The raster being asked for is a standing aggravation rather than the cause:
`Geometry::solveRaster()` lands 1561 x 1124 at 105.32 MHz for this source, which
is no CEA mode, because 1080p59.94's 2200 x 1125 needs 148 MHz and
`OutputMode::EngineCeilingHz` is 108. `investigations/encoder-stale-timing.md`
is what the re-look exists for.

The recovery, which needs no reflash and no bench trip:

    python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set PAD_SYNC_OUT_ENZ=1
    python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set PAD_SYNC_OUT_ENZ=0

**Wait 7-8 s before judging a capture taken after it.** The link re-acquires
over several seconds and an immediate grab returns the black frames emitted
during it, which reads as the toggle having failed.

### The capture tail runs a whole sync pulse past the picture

`CaptureWindow::lastCapture()` is `units - 1`, and `firstCapture()` is
`headBlanking + (syncAtHead ? syncUnits : 0)`. On a low-active source the
origin is the pulse's trailing edge, so the head excludes no pulse and needs
none -- and the tail then runs into the NEXT line's pulse, which nothing takes
off it.

Measured at 640x480@60 on `vga`, `PLLAD_MD` 1494, forced 100% framing, against
`RetroScaler-Acorn.mdf`'s `94,22,22,640,22,0` at 25175 kHz:

| | source px | IF units | output px |
|---|---|---|---|
| left band, the back porch | 22 | 41 | 43 |
| right band, into the next pulse | 54.4 | 102 | **107**, measured 108 |

So the right of the picture carries about 107 output pixels of black that no
framing asked for, and the left 43. The asymmetry is not a fault in the source:
640x480@60 and 800x600@60 both have a **front porch of zero** in this monitor
definition, so everything past the right border is sync.

The tail that stops where the content does was measured at 1394 here against
the 1492 in force. **The arithmetic that produced it carried a capture lag that
no longer exists**, so the 1394 is a reading rather than a formula and the
shortfall wants re-measuring before anything is keyed on it. Subtracting
`syncUnits` unconditionally gives 1320 and costs 70 units of picture, which is
the form that was tried and reverted.

`investigations/the-capture-tail-overruns-the-picture-by-the-sync-pulse.md`
carries the arithmetic, the photo calibration and the prediction that refutes
it.

**The HIGH-active case has the same tail problem for a different reason, and it
is closed.** At 320x256@50 the origin is the pulse's LEADING edge, so the next
pulse begins at `units` and the tail holds no pulse at all -- and the last 41 to
43 units are still contaminated, by the approach to it. That mirrors the head,
which is contaminated for about 20 units AFTER the pulse and is guarded by
`DoubledHeadBlankingUnits`.

`InputFormatter::DoubledTailBlanking` blanks it through `IF_HBIN_ST`, which acts
on the input side and takes no picture and no zoom range: the picture's right
edge does not move at any value to 160. Six round trips of the reproduction land
clean.
`investigations/the-hbin-start-blanks-the-captured-tail.md`.

**The LOW-active case still stands, and `IF_HBIN_ST` cannot reach it.** That
branch runs against `IF_HBIN_SP` at `NoHeadBlanking`, where the same field
blanks the WHOLE line rather than its tail -- measured at 320x256@70, whole
picture to 16 and black from 18 up. So the two polarities need different
mechanisms, which is also why subtracting `syncUnits` unconditionally is wrong
in both directions.
`investigations/a-flip-test-cannot-tell-a-captured-tail-from-stale-memory.md`.

### ~~`test_capture_origin.py` compares a live sync width against a latched one~~

**Closed, and the mechanism was not drift.** The engine does not divide
`STATUS_SYNC_PROC_HLOW_LEN` by the divider at all:
`SyncProcessor::hsyncPulseSamples()` folds the count against its complement and
subtracts `InvertedPulseWidthSamples` where the source is high-active, so the
raw ratio is five samples wide on every such source and the disagreement is
systematic rather than a sample of drift.

Measured at 320x256@50, `PLLAD_MD` 2200, `STATUS_SYNC_PROC_HSPOL` 1: the
register reads 156 and the engine used **151**, which places 76 units where the
raw reading derives 79. That is the whole of the three-unit gap, and it survives
a re-solve because nothing about it is stale -- `/sc?U` leaves `fh` at 98 and
the register at 156, three times.

`/geometry` reports the solved interval as `sh`/`sv` now and the test reads it,
so the two cases check what they are for -- the head blanking term applied to
the doubled path and only to it -- against the engine's own measurement.

**The earlier reading of this entry was a different magnitude and is
superseded**: one unit against a `HLOW_LEN` of 155 is what a re-solve moved, and
the three units are the polarity correction. A `/sc?U` that appears to clear a
failure here is moving the solve, not settling the question.

### ~~Three hardware tests carry their own copy of a firmware constant~~

**Closed.** `test_if_head_blanking.py` declared `LINE_DOUBLE_RESET = 272` where
`InputFormatter::LineDoubleReset` is **160**, so
`test_head_blanking_follows_the_scan_mode` failed on the bench's line-doubled
source: `IF_HBIN_SP` reads 160 and the test wanted 272. The firmware moved the
doubled path's origin and the copy did not, which is the hazard a copied fact
carries -- the test read as a firmware defect and was a stale constant.

The case asserts the band the modes measured rather than the value the firmware
picked, 137..179, so it still catches 272 without holding a copy that can go
stale again. `test_capture_origin.py`'s two failures were the entry above and
not this.

**Only one of the three was a genuine copy.** `test_membus.py` and
`test_inrange.py` also carry 272, and neither is a mirror of the constant -- it
is an arbitrary input to a pure arithmetic function and a plausible bench state
for a range checker, so both pass and neither follows the firmware.

### The capture starts in a different place, and the mechanism this was filed against is gone

`PLLAD_MD` came out **2250, 2206 and 2202** across solves on one unchanged
source -- the RiscPC at 320x256@50 -- with `HPERIOD_IF` reading a correct and
stable **431** every time. The picture sits left with a coloured band down one
side, and that part is still seen.

**The explanation this row carried is refuted by construction.**
It was that `SourceMeasurement::measureLineRate()` took
`measureLineRateFromHPeriod()` first and measured the field rate only when that
refused, so which of two disagreeing measurements answered decided the divider.
Nothing derives the line rate from `HPERIOD_IF` any more -- it is a change
detector and nothing else -- so there is one measurement and no path to choose
between.

**What it needs is a fresh diagnosis rather than this one.** The first question
is whether the divider still moves across solves on an unchanged source, and the
second is whether `STATUS_SYNC_PROC_HTOTAL` equals `PLLAD_MD` while the picture
is shifted: equal rules the ADC PLL out and leaves the capture's framing
constants, unequal makes it the same family as
`investigations/the-field-rate-floor-admits-a-pin-the-source-is-not-driving.md`.

### An input change taken while passed through never settles

Pass-through permitted, output already passed through, and the input changed:
the engine sits in its no-sync branch indefinitely -- `m:0`, `s: 0`, the run
pinned -- with the line rate arriving correctly on `HPERIOD_IF` and
`STATUS_SYNC_PROC_VTOTAL` stuck at 97. The route is re-decided only on a settled
measurement, so the state holds itself; `/sc?~` only clears it once the input is
one that can lock. The same change with `preferScalingRgbhv` set completes in
under 40 s in both directions.

The stale quantity is the ADC PLL's crossover row and VCO gain, not the divider:
`dividerFor()` gives 2039 at every rate involved, so the group is set for
99.7 MHz against a source wanting 64.0 MHz and `STATUS_MISC_PLLAD_LOCK` reads 0
throughout.
`investigations/an-input-change-under-pass-through-never-settles.md`.

### `getStatus16SpHsStable()`'s bypass branch is a stability test that cannot fail

With the source passed through, the function answers on
`STATUS_INT_INP_NO_SYNC` rather than `STATUS_16`. **That bit does not latch on
this board.** Measured across a genuine sync loss with the Wii passed through
and the input then switched away: 0 of 1486 samples with bit 4 set, while
`INT_ENABLE4` reads 1, neither of the two acknowledge sites ran, and the
neighbouring bits latch freely in the same window -- `s0_0F` takes the values
168, 160, 136 and 128, every one of them `INT_INP_HSYNC` and `INT_INP_CSYNC`
and none of them bit 4.

So the branch returns true whatever the source is doing. The one sample where
the two tests disagree is in that direction: `STATUS_16` read not-stable with
`STATUS_SYNC_PROC_VTOTAL` 97 while the interrupt branch still said stable.

It reaches detection, which calls the function inside its own 450 ms search, so
an RGBHV source in pass-through is searched against a test that always passes.

Would settle it: whether `STATUS_16` alone is right on both routes -- it counts
in the sync processor, which pass-through does not take out of the path
(`STATUS_SYNC_PROC_VTOTAL` held 524 in 489 of 489 samples on the Wii in
pass-through).

### The picture sits ~150 columns left after a pass-through round trip

**NOT SEEN SINCE THE RASTER CARRIED THE STANDARD'S TIMINGS.** The landing has
been stable through every output change measured since, and the mechanism below
is kept because it is what the measurements rule out rather than because the
fault is live. Two mode changes into each of 1080p and 1024p land the picture
within **0 photo px at r = 0.9978**, and four 1080p/1024p round trips solve
byte-identical rasters. Re-open it on a sighting, not on a doubt.


**THE BOARD IS EXONERATED, MEASURED RATHER THAN INFERRED.** The television's
menu is drawn by the STV9426 from `HS_OUT`/`VS_OUT` and keyed into the video at
U13, downstream of the VDS, so it rides the sync timebase while the picture
rides the VDS's counter. Photographed at two landings they move **together** --
overlay -100.78 px and +26.58 rows against the picture's -101.01 px and
+26.43 rows, agreeing to 0.23 px and 0.15 rows, with the camera controlled at
lag 0 and r = 0.998. Video cannot have moved relative to sync inside the
scaler, so the analog frame is identical at both landings and the displacement
is added after it. It does not separate the MS9288A from the television.
`investigations/the-picture-position-is-latched-not-re-rolled.md`.

**There is a vertical component at some landing pairs** -- +26.4 photo rows at
the 1550/1450 pair -- so "a pure horizontal translation" describes the pairs
that were sampled rather than the fault.

Measured on the bench panel as a translation rather than a scale change: the lit
width is 892, 953 and 968 columns across three frames while the left edge moves
159 -> 5.

**What has been checked is the solve, and it is unchanged** -- `VDS_HSYNC_RST`
1915, `VDS_VSYNC_RST` 1124, `VDS_HSCALE` 546, display window 110..1899,
`/geometry` `oh 51, eh 954, ov 38, ev 582`. `VDS_HS_ST` 0 / `VDS_HS_SP` 32 is
the correct derivation for this raster, `syncNs x clockHz` giving
296.30 ns x 108.03 MHz = 32, so the output sync placement has not drifted either.

**THE FULL DUMP HAS NOW BEEN TAKEN AND IT IS EMPTY.** `snapdiff.py --save` at
three different positions on one source, covering all 1536 addresses: **0 bytes
differ** between the reference position and either of the other two. The
position of this picture is not a register on this part.

**The sampling clock is refuted for this occurrence.** At 640x480@60 `PLLAD_MD`
is 1494 at all three positions, with the whole part byte-identical, so a
divider that takes two values cannot be the carrier. The 2250/2206 reading at
320x256@50 stands as a separate observation about the divider.

**The display clock is refuted.** `/framesync` carries what the Si5351 has been
steered to, which no dump reads: 107996832..107997000 Hz across eight round
trips, a spread of 1.6 ppm that does not sort with position.

**The frame buffer restarting is refuted.** Five `holdMemoryBlocks()` /
`releaseVideoBlocks()` cycles with automation frozen move the picture 0.00 px.

What does move it, with every other byte on the part unchanged, is
`PAD_SYNC_OUT_ENZ` -- once in nine toggles, so it is a demonstration that the
choice is made downstream of the output pins rather than the trigger a round
trip pulls. `investigations/the-picture-position-is-latched-not-re-rolled.md`
carries the measurements. Its open candidate was that the pad returned 300 ms
in, while FrameSync was still steering; the pad now returns after the rate
match and the phase, and whether the landings still vary is not re-measured.

Neither a `PAD_SYNC_OUT_ENZ` toggle nor a source mode round trip re-centres it.

**THE PAD MOVES THE LANDING RATHER THAN REPAIRING ONE, and a single trial
cannot tell the two apart.** Nine 3 s drops from three different positions at
640x480@60 moved the picture twice: once from the right landing to the left one,
and once from the left landing to a third in the middle, right edges 1444, 1504
and 1545 photo columns. A drop that happens to land somewhere better reads as a
fix, and the next one moves it again.

**THE THREE LANDINGS ARE NOT A DRAW, AND THEY ARE NOT OUR BLANKING EITHER.**
The explanation carried here -- that the encoder latches its window origin from
`VDS_DIS_HB_SP`, so a landing is whatever the blanking was when the pad returned
-- is **refuted**: measured at 320x256@50 into 1080p the window sits at our unit
161.6 whether the register is 143 or 200, across a source mode round trip and
across a pad toggle. The toggle does move the landing, about 11 columns between
two positions with the span unchanged, so the landings are real; what they are
not is a reading of our own blanking.
`investigations/the-transmitted-window-is-latched-from-our-blanking.md`.

**And a pass-through round trip is not a provoker either**: twenty-two of them
across two builds moved the picture once, that once being the first round trip
after an ESP restart, with the other twenty-one inside 0.53 photo px. Budgeting
a bisect against it costs a session and measures nothing.

**The black margin is refuted.** Cropping the display window to the content --
`VDS_DIS_HB_ST` 1583 -> 1480, frozen, which moves the picture 0.02 photo px
because only black is cut -- leaves the first jog moving it the full 41 px.
Blanking 220 units off the line also leaves the picture exactly where it was,
neither translated nor rescaled. Both refute a stage deciding where active video
is by where the video stops being black, which was the leading candidate.

**The provoker is deterministic once it is conditioned on the restart.** An ESP
restart followed by `/sc?B` and `/framing/full?on=1` lands the right edge at 1544
in 4 of 4, and one output disturbance after it leaves 1544 in 3 of 3, while a
disturbance from either of the other two landings moved it in 0 of 18. So a
trial is a restart and one jog rather than a round trip at 2 in 8.

**The byte-identity is not merely observed, it has been constructed.** Writing
all 26 bytes that separate a post-restart position from a post-pass-through one
-- so `snapdiff.py --save` reports 0 differing over all 1536 addresses -- leaves
the picture where it was, and so does resetting the video blocks, the SDRAM, the
phase adjusters and the sync processor afterwards. Differencing frames taken at
two `VDS_DIS_HB_ST` values in each position shows the **blanked strip moving by
the same 100 columns as the picture**, so the picture keeps its place inside the
raster the scaler emits and what moves is where that raster is painted.

### `/sc?~` recovers the picture but leaves the engine calling the source absent

Measured on `vga` at 320x256@50, twice, on two builds: after
`goLowPowerWithInputDetection()` the chip is fully correct -- `PLLAD_MD` 2206
against `STATUS_SYNC_PROC_HTOTAL` 2206, `STATUS_SYNC_PROC_VTOTAL` 311,
`STATUS_SYNC_PROC_HSACT` 1, `SP_SOG_MODE` 0, `SP_CLAMP_MANUAL` 1,
`DAC_RGBS_PWDNZ` 1 -- and the panel shows a clean, complete PM5544. `/geometry`
nonetheless reports `present: false, state: absent`, and stays there across ten
polling rounds.

So the recovery works and the engine's acquisition state does not follow it.
What that gates is maintenance rather than the picture: the clamp re-place, the
sampling phase and the deinterlacer steer all key off an acquired run.

**Not the step-12 predicate substitution.** The A/B was run deliberately --
`sourceIsRgbhv()` reverted to the standard byte, rebuilt, reflashed -- and the
byte build behaves identically.

`/input?src=vga` clears it.

### A composite-sync source in pass-through does not acquire, and the VERTICAL count is what is left

640x480@60 on `vga`, one cable, one mode, the sync type the only thing moving.
Separate sync passes through and fills the panel. Composite sync goes `absent`
and stays, with `STATUS_SYNC_PROC_VTOTAL` 522 against 524.

**The horizontal half is FIXED and the attribution to the route is REFUTED.**
The ADC PLL was running far below its divider -- `STATUS_SYNC_PROC_HTOTAL`
1700..1729 against a `PLLAD_MD` of 2039 -- because `PLLAD_KS` held 3 where the
65.7 MHz that pair implies needs 1. The row had been sized from a field rate of
15.32 Hz, which is what the board counts at a pin a composite-sync source does
not drive, and which cleared a `FieldRateMinHz` of 15.0 by two tenths of a hertz.
The floor is 30.0 now. Measured after the change, in the same state: `PLLAD_KS`
1, `HTOTAL` 2040 against `PLLAD_MD` 2039, `STATUS_MISC_PLLAD_LOCK` 1.
`investigations/the-field-rate-floor-admits-a-pin-the-source-is-not-driving.md`
carries the refutations of the divider, the route and the charge pump, each
measured.

**WHAT IS LEFT IS THE IDLE PASS, AND THE MEASUREMENT IS NOT IMPLICATED AT ALL.**
The source is read right and solved right: measured at 800x600@60 composite with
the arrangement re-probed, **15 complete solves in 42 s** -- one every 2.8 s --
with 50 of 89 `measured:` lines reading the correct 627/628 and the duty taken
through a latched clock. Every one is discarded by the next idle pass, which
reads a count the solve never saw: **34 distinct values from 253 to 1880**, 13
of the 15 arms firing on the count. 623, 1246 and 1870 are the source's count
and twice and three times it, so vertical pulses are being missed, and
`countIsSource()` cannot reject 1246 -- any count from 200 to 1300 is a source.
`investigations/the-composite-sync-count-re-arms-its-own-solve.md`.

**The field-rate reading of 3937.58 Hz is REFUTED as evidence about this path.**
It was taken with the sync type stale, the separate-sync configuration in force
on a composite source. With the arrangement right the input formatter's vertical
-- which is what the field rate is timed off -- is a clean field-rate pulse, and
the two sync types are indistinguishable there: `/testbus?ms=25&if=3` reads **4
transitions at duty 0.2% on both**. `vs_act_det` is the one stage that stops
carrying anything, toggling 115 times on separate sync and sitting high for the
whole window on composite.

**The sync-type probe is NOT why**, and an earlier reading of this row saying so
is withdrawn. Forced to re-probe, it answers correctly on both sync types of the
bench source. What puts a composite source on the separate-sync configuration is
that a sync-type change arms no re-probe, so the held answer stands.
`investigations/a-stale-sync-type-leaves-a-composite-source-uncoasted.md`.

Three findings from the same window:

- `SP_H_PULSE_IGNOR` is found at 255, the value the separate-sync arrangement
  then wrote, inside a `SP_SOG_MODE` 1 configuration: two writers
  disagree. Moving it off 255 takes `VTOTAL` from 498 to 521 at once, and 107,
  51, 16 and 2 are indistinguishable from each other.
- The separator level matters more than the coast window. `ADC_SOGCTRL` 1 gives
  a `VTOTAL` wandering 534..543; 4 and above give a steady 522. Coast pairs of
  7/3, 9/9, 3/3, 1/1 and 0/0 move it by at most one line; 16/16 destabilises it.
  **The level that works is per source and 4 is not a floor to generalise** --
  at 800x600@60 it takes 20, and 4 is among the worst there.
  **BUT THE LEVEL IS NOT A REPRODUCIBLE LEVER, so none of those figures may be
  used to choose one.** Two consecutive acquisitions of the same source in the
  same mode, the level walked from inside `loop()` with 24 counts read a level,
  give 624/624 and 1691/1691 at level 18 -- the surface the coast pair has
  rather than the one `SP_H_PULSE_IGNOR` has. Every table of it taken so far
  was also held from outside against the 500 ms ladder, and the polling that
  takes starves the loop: 30 console lines in 338 s against 1070 in 80 s, the
  engine solving nothing across three 75 s windows. Walk it from inside
  `loop()`, or the reading describes a starved engine.
  `investigations/the-separator-level-steadies-the-count-but-does-not-present.md`,
  `investigations/the-composite-sync-count-re-arms-its-own-solve.md`.

**`STATUS_MISC_PLLAD_LOCK` is not a discriminator in the failing direction**: it
is 0 in the working pass-through separate state as well. It reading 1 does mean
something, and it goes to 1 when the crossover row is right.
`HTOTAL == PLLAD_MD` remains the witness that the divider reached the PLL.

**Pass-through is refused below about 31 kHz**, so route and rate cannot be
separated the other way round on this bench: `/uc?x` at 320x256@50 leaves
`DAC_RGBS_BYPS2DAC` 0 and the source on the scaler.

**Sync polarity on the composite path is still an open lead for the capture
window.** `SourceMeasurement::readSource()` reads `STATUS_SYNC_PROC_HSPOL` and
normalises `SP_HS_INV_REG` from it, with no csync branch. That bit reports the polarity of the signal arriving BEFORE
the separator, and on csync the separator regenerates H, so its output polarity
is the separator's property rather than the incoming signal's. Measured support:
of ten csync legs at 320x256@50, five read the duty as its complement -- 93.13%,
98.23%, 93.14%, 84.47%, 93.14%, and one leg read 148.07% -- every one refused,
which places the capture window's head from `FallbackDuty` rather than from a
measurement.

### A sync-type change arms no re-probe, so the held answer outlives the source

The RiscPC sets its sync type from CMOS at one line count -- 311 at 320x256@50,
524 at 640x480@60 -- so nothing in `source moved` distinguishes `SYNC 0` from
`SYNC 1`. `establishSyncType()` reuses what it holds, and a composite source
runs on the configuration chosen for separate sync: uncoasted, with the
separate-sync separation thresholds.

Uncoasted, the line counter loses the lines the vertical pulse occupies and
dithers -- 307/308 against a true 311, 522 against 524 -- and the picture bounces
vertically without rolling, confirmed at the panel. Coasting holds the count
still and stops the bounce; it does not recover the missing lines.

**The probe itself is right.** Forced to re-probe, `own V sync` answers `yes`
after 2..203 ms on separate sync and `no` after 1000 ms on composite, four
trials. Do not file this against `SyncMeasurement`'s question.

`investigations/a-stale-sync-type-leaves-a-composite-source-uncoasted.md`.

### The own-V-sync probe spends a second concluding by absence

`hasOwnVsync()` pays `OwnVsyncSettleMs` 240 unconditionally, then polls
`STATUS_SYNC_PROC_VSACT` for up to `OwnVsyncWindowMs` 1000. `VSACT` can only
rise, so a composite source spends the whole window -- about 1240 ms per probe --
to conclude something by absence, where the positive case lands in 2 ms.

With the separator OUT, which is the state the probe creates and then waits in,
`STATUS_SYNC_PROC_VTOTAL` already answers both directions: **311** on a
separate-sync source and **50** on a composite one, twice each, with only the
source moving. Reading it for plausibility replaces the window with a comparison.

**What gates the change**: whether `VTOTAL` has settled by 240 ms. The readings
were taken after about 1.5 s. Eleven frames at 50 Hz is plausible and is not
proof, and a plausibility read taken early answers for the previous state.

`investigations/the-own-vsync-probe-answers-by-absence.md`.

### `PAD_SYNC_OUT_ENZ` is found at 1 with everything else healthy

Seen twice after an OTA flash: `/geometry` reporting `acquired`,
`DAC_RGBS_PWDNZ` 1, `STATUS_SYNC_PROC_VTOTAL` 311, `STATUS_SYNC_PROC_HTOTAL`
equal to `PLLAD_MD`, and the panel reporting no signal -- because s0_49 bit 2 is
set and HSOUT/VSOUT are not being driven at all.

Distinct from the encoder's stale-timing lock, which drops the link with the
pads enabled. Clearing the bit is not enough on its own once the sink has given
up: a 1 -> pause -> 0 toggle is what makes it re-acquire, after which the TV
reports 1920x1080/60Hz again.

**`/sc?~` is one of the things that sets it.** Read 0 immediately before the
call and 1 after it, with the source unchanged and the panel then showing no
signal while every other register read correct. So a recovery that is reached
for on a unit with no picture is also a way to arrive at one, and the toggle
belongs after every `/sc?~` rather than only when the symptom appears.

### ~~The defaults signature cannot tell a wiped preferences file from a chosen one~~

**Closed.** The question this asked -- what discriminator separates a defaulted
load from a correctly configured one -- the keyed file answers directly, and the
guard asks it now.

The entry recorded that `presetPreference=5 frameTimeLock=0 suspect=0` cannot
distinguish the two, because `Output1080P` IS 5 and is also the default. That
reasoning stands; what changed is that the trace no longer carries a value
signature at all. The boot log reads:

```
PREFS: attempt 1 t=6944ms size=840 applied=34 end=1
PREFS: loaded output=1920x1080 frameTimeLock=0 slot=65 SeleInputSource=2 suspect=0
```

`applied` counts the keys a line actually set, and a defaulted load sets none,
so **a boot that reaches the file's end with `suspect=0` and applied nothing is
the regression** and nothing else is. `test_bootlog_reports_the_preferences_read`
asserts that, and it had been failing only because it still matched the
positional file's `got=`/`plausible=` wording.

**A non-default preference survives a WARM reset**, measured: `line-filter` set
to 1 through `/uc?m`, the unit restarted over HTTP -- gone and back in 8.6 s --
and it came up with `line-filter = 1` and `VDS_D_RAM_BYPS` 0, the filter in
circuit. So the file is written and read faithfully. **This does not settle the
cold-boot question**, which is a power-up race on the SPI flash and needs mains
and USB pulled; nothing here was tested that way.

### A composite source can be acquired on the separate-sync configuration

On the SCALING path the engine acquires a composite-sync source without ever
probing the type, because the count is plausible and steady on the wrong path --
308 lines either way on the bench RiscPC at 320x256@50 -- so nothing arms a
re-probe and the ladder never runs.

Measured over ten `SYNC 1` / `SYNC 0` round trips: the composite leg settled with
`SP_SOG_MODE` 0 in **five of ten**, and the duty came out the complement every
time -- 93.13%, 98.23%, 93.14%, 84.47%, 93.14% -- so `forDuty()` refused it and
the capture window's head was placed from `FallbackDuty` rather than a
measurement. The other five probed, reached `SP_SOG_MODE` 1 and read 6.99..7.02%.

It costs nothing visible on this source, because 0.07 and the real 0.071 give the
same window. It is not free on a source whose duty differs -- the entry on
`STATUS_SYNC_PROC_HLOW_LEN` has that arithmetic.

What would settle it: a signature that says the held sync type is wrong while the
count is right. `forgetSyncType()` has exactly one caller and it needs an
implausible count, which this state never produces.

### A 15 kHz source left in pass-through is not recovered by `/sc?~`

Turning `preferScalingRgbhv` back on does not move the route -- the engine
re-decides only on a settled measurement, and the source cannot settle -- so
returning the RISC PC to 320x256@50 while passed through strands it: `state:
absent`, `STATUS_SYNC_PROC_HTOTAL` 13, `SP_SOG_MODE` 1 on a separate-sync
source, and a held line rate of 10166 Hz that no correct reading displaces.

`/sc?~` takes the route back to the scaler and finds separate sync again, and is
**not enough on its own** -- four minutes later the held rate was still 10166 and
the state still absent. `/input?src=vga` re-acquired in under a minute. So the
input re-selection is the recovery here, not the one the stuck-divider row of
`CLAUDE.md` names.

### The clamp window sits inside the sync pulse, on both sync branches

Two instances: 320x256 on composite sync, clamp 14..76 against a 144-sample
pulse; 640x480 on separate sync, clamp 11..65 against a 126-sample pulse. It
reaches the picture -- at a clamp inside active video the greys take the colour
beside them. `SyncProcessor::acquireClampWindow()`'s fractions are the fault and
it is general, not a composite-sync quirk.

### The sampling-phase sweep scores on exact equality, and the count dithers by one

`Adc::acquirePhase()` scores a phase clean only when all twenty of its
`measureLineSamples()` reads equal `PLLAD_MD` EXACTLY, and refuses the whole
search unless seventeen of the thirty-four phases score clean. The gate in front
of it, `SourceMeasurement::dividerLatched()`, allows a tolerance of **8**. The
sweep allows none.

Measured on the bench RiscPC at 320x256@50, acquired with a clean picture,
`ms=25` over 15 s from inside `loop()`: `STATUS_SYNC_PROC_HTOTAL` equals the
divider in **410 of 556 samples** and sits one count either side in the other
146. At a per-read mismatch of 26% a phase clears twenty reads about twice in a
thousand, so no phase ever scores clean and the search refuses every time --
`sampling phase: no clean window, oversample 4`, seven attempts per solve,
at 320x256 and at 640x480 alike.

What that leaves is not a chosen phase: `PA_ADC_S` stays at the 16 nothing
chose, and `PA_SP_S` is wherever the sweep's walk stopped, which moves run to
run -- 20, then 0, then 20 across three solves. **The Wii at 480p is the
contrast**: the search succeeds there and `PA_ADC_S` reads the 0 a successful
4x search picks.

**A tolerance is not obviously the fix.** Accepting +/-1 would make every phase
clean, `worstScore` zero, and the answer `MidField` regardless -- which is where
the phase already sits. Whether the sweep should tolerate the counter's own
dither while still discriminating between phases wants a bench sweep behind it.

### `HPERIOD_IF` rails, and the recovery ladder is not certain

Long-standing and documented in `../CLAUDE.md` and
`investigations/hperiod-if-railing.md`. Worth repeating here only for what a
session needs to know: the rungs are a source mode round trip, an
`ADC_INPUT_SEL` bounce, then a cold boot, and **a rung that fails once may work
on a second attempt** -- measured this way round, a round trip and a bounce both
leaving 511/255 with `STATUS_IF_HT_OK` 0, and a second round trip restoring 431
in 4 of 4 samples.

### A separate-sync source parks the recovery ladder, so nothing on the unit clears the post-flash state

Every OTA flash lands `vga` here: `/geometry` all zeroes, `DAC_RGBS_PWDNZ` 0,
`STATUS_SYNC_PROC_VTOTAL` and `STATUS_SYNC_PROC_HTOTAL` 0,
`STATUS_MISC_PLLAD_LOCK` 0, `PLLAD_MD` at 1792 against the 2206 the source
wants, while `STATUS_SYNC_PROC_HSACT` reads 1 and the source is sending.

**The ladder cannot leave it.** `SyncRecovery::ReprobeSyncType` restarts the run
whenever the source has its own V sync -- deliberately, because a V sync
arriving is proof of a source and the next rung would toggle the input away from
it -- so a separate-sync source cycles rungs 0..151 every ~7 s for ever and
never reaches `ToggleInput` or `ReopenSogSeparator`. The console is

    own V sync: yes after 2ms
    recovery: own V sync found, the run restarts
    No Signal Out

repeating on that cadence with nothing changing. `/sc?~` does not clear it.

**Every escape is external.** Re-selecting the input and round-tripping the
source mode clears it in about 4 s; a bare source mode change clears it on its
own some of the time and not others. The engine has no rung that reaches it,
which is why a unit that has just been flashed needs a source touched before it
can be judged.

**An `/input` bounce is the escape that needs neither the source nor the bench**,
and it is the one that worked every time over six flashes in one session:
`/input?src=ypbpr`, a few seconds, then `/input?src=vga`. It cleared states that
`/sc?~` and a source mode round trip both failed to clear -- including one where
`STATUS_SYNC_PROC_HTOTAL` sat at 2423..2431 against a `PLLAD_MD` of 2506 with a
correct crossover row.

**A reflash appears to fix it, and that reading is a trap in a second way.** A
flash resets the ESP, so putting a known-good image on and watching the picture
return tests the reset and the image at once. A change cannot be attributed from
that comparison; re-flashing the suspect image is what separates them, and a
suspect image that acquires in under a second on the second attempt was never
the cause.

### The output porch is a duration and the active span a fraction, so above 70.9 Hz the picture is clipped

Every mode at 72 Hz and above lands with a black band down the left of the panel
and the card's outer columns missing; at 60 Hz the same card fills the screen. It
is the FIELD RATE and not the mode -- 800x600 is clean at 60 and clipped at 72
and 75, 640x480 clean at 60 and clipped at 75.

**`Mode960p` is a 60 Hz standard being run at every rate.** Its raster is
1800 x 1000 at 108 MHz, and `OutputMode::solve()` keeps the mode and the clock
while re-solving the horizontal total for the source's field rate. Two
quantities then come out of it, and they are not the same kind:

- the sync pulse and back porch are DURATIONS, `scaled()` against the clock, so
  at an unchanged 108 MHz they are a **constant 112 + 312** whatever the total;
- the active span is a FRACTION, `horizontalTotal x carriedPx / totalPx`, so it
  shrinks with the total.

The blanking before the picture is therefore 424 of 1800 at 60 Hz and 424 of
1440 at 75 Hz -- 23.6% of the line against 29.4% -- while the span it has to fit
alongside stays 71.1% of the line. The two no longer fit, and `activeStop` is
clamped to `horizontalTotal - FrontPorchMinPx`, throwing the rest away.

| raster | `activeStart + span` | `lastUsable` | `activeStop` predicted / measured |
|---|---|---|---|
| 1800 | 424 + 1280 = 1704 | 1784 | 1704 / **1703** |
| 1496 | 424 + 1063 = 1487 | 1480 | 1480 / **1479** |
| 1440 | 424 + 1024 = 1448 | 1424 | 1424 / **1423** |
| 1432 | 424 + 1018 = 1442 | 1416 | 1416 / **1415** |

**The boundary follows from the arithmetic.** `424 + (1280/1800) x T <= T - 16`
holds only for `T >= 1523`, and `T = 108e6 / (1000 x f)`, so the rate above which
picture is thrown away is **70.9 Hz**. 60 Hz clears it and 72 Hz does not.

**The clipped columns are real picture, and the display window is what removes
them.** Frozen at 640x480@75, `VDS_DIS_HB_SP` alone moves the left edge of what
is shown: at 300, 340, 380 and 410 the card is whole, at 426 it is not, and the
strip the register blanks is live video rather than border.

**It is NOT the encoder re-locking.** Measured within one frozen acquisition, a
row-averaged profile over the middle half of the panel correlates at r = 1.0000
between `VDS_DIS_HB_SP` 410 and 426 with a best shift of **0 px** across the
right-hand 55% of the frame, and the only columns that change at all are 50..148
at the far left. 340 and 380 differ from 300 in zero columns. The picture does
not move; a strip of it is blanked. A photograph taken across two ACQUISITIONS
does appear to move, which is the confound --
`investigations/the-picture-position-is-latched-not-re-rolled.md`.

**Putting the porch on the same footing as the span recovers the whole card.**
At 640x480@75, frozen, with `312/1800 x 1440 = 250` in place of 312 --
`VDS_HB_SP` 260, `VDS_DIS_HB_SP` 340, `VDS_DIS_HB_ST` 1364 -- both castellation
columns come back and nothing is clipped.

The code states the reason for the span being a fraction: the encoder resamples
the line into the standard's active pixel count however long the line is. **The
same argument reaches the porch**, which the mode currently states as a time.
Whether the fraction is the right conversion for the pulse as well as the window
is what a fix has to settle, since the two claims -- that the encoder finds
active video where the blanking ends, and that it resamples the whole line --
are not both true in the way they are currently used.
`investigations/the-active-window-is-a-fraction-of-the-line.md`.

**IT ALSO BITES BELOW THE STATED BOUNDARY, in the other direction.** 800x600@56
runs a LONGER raster -- 1706 against the 1600 that makes the line exactly
1080p's -- and lands with a 105-column black band down the left, measured on the
emitted frame with the capture window sitting on its published raster to four
hundredths of a percent. So the 70.9 Hz figure bounds where the active span
stops fitting, and it does not bound the band: every raster measured away from
1600 carries one, 1440 giving 133 columns and 1706 giving 105, against 4 and 14
at 1600 and 1592. What the encoder does with a line that is not 1080p's length
is not settled.

Two things that are NOT the fault: the capture window, which matches the mode's
published active region to the unit (640x480@75 reads `oh 292 / eh 1017` of
`ch 1335` against DMT's `(64+120)/840` and `640/840`); and `EngineCeilingHz`,
which buys room at 129.6 MHz -- a raster of 1728 at 75 Hz clears the bound -- but
raises the rate at which the same mismatch bites rather than removing it.

### 800x600@56 never settles the frame time lock, and the picture wobbles vertically

Seen on `vga` on separate sync, and it is the FIELD RATE rather than the mode:
800x600 at 60 and 72 are steady and 56 is not. The engine's measurement of the
source is steady throughout -- `in 56249 mHz` on every line for 65 s -- and what
does not settle is the correction:

    phase 796640/2844464 target 711116 err  85524
    phase 722362/2844464 target 711116 err  11246
    phase 719190/2844464 target 711116 err   8074
    ...
    phase 676064/2844464 target 711116 err -35052
    phase 771320/2844464 target 711116 err  60204

54 lines over 65 s, the error swinging between -35000 and +65000 without
converging, and the display clock dithering 107947536 .. 107961952 with it.

**The `pin` column is the lead.** It reads about 2844400 on most passes and
5688808, 5675154 or 8524106 on others -- two and three times the frame period,
which is a MISSED EDGE rather than a measurement. A doubled reading scales the
phase target with it, which is the shape
`investigations/the-frame-time-lock-saturates.md` records.

### The RISC PC emits 1024x768@60 about 23 units after its published raster

Measured two ways that need no common assumption: at the default framing the
card's green frame lands 38 columns into the panel, and at a forced 100% framing
the source's first active column reads 342 of a 1443-unit line. Both put active
video at 23.7% of the line where DMT states 22.02%, and the monitor definition's
entry carries DMT's own `136,160,0,1024,0,24` with no border to spend.

The capture opens at 22.04%, which is the published raster to two hundredths, so
this is the source rather than the placement. 640x480@60 and 800x600@60 on the
same machine are exact -- 261 against 260.5 and 296 against 294.3 -- so it is
not a general offset either.

It costs a 38-column band at the left of that one mode, which a pan press
removes and the framing table then remembers.

### A 100% framing plays the captured frame out twice, compressed

**FIXED.** `CaptureWindow::lastCapture()` returns `units - 2`. It returned
`units - 1`, which is `IF_HSYNC_RST` itself -- the line total, a value the
counter never equals -- so a framing that asked for the whole line stopped the
capture window on it and the window never closed.

Measured on the RiscPC at 320x256@50, engine-solved at a forced full framing,
automation frozen and `IF_HB_ST2` the only variable:

| `IF_HB_ST2` | `IF_HSYNC_RST` | emitted |
|---|---|---|
| **1100** | 1100 | **two cards**, the upper one cut, the card's own animation stopped |
| 1099 | 1100 | one card, complete, animating |
| 1098 | 1100 | the same |

The stopped animation is what names the side: the source keeps drawing, so a
frozen picture is the write side and not the playback. Solved, the same framing
now gives `IF_HB_ST2` 1099 and one card, three forced framings in a row.

**Only the extreme of the range reached it**, which is why the default framing
never showed it -- that framing takes 94% of the counter and stops well short.
One zoom step in was clean for that reason alone.

**The green frame's extent cannot score this.** A second copy inside the first
one's bounding box leaves the outermost green where it was, and readings taken
that way report a doubled frame as clean. Count the card's single red block.

`investigations/the-capture-stop-must-be-a-unit-the-counter-reaches.md`

### A short output raster shreds a source of few lines, and only that combination

The RiscPC at 320x256@50 -- 311 lines -- into 480p or 576p: the card is torn into
vertical bands, wrapped sideways about a seam, with alternate-line combing. The
scaling path is in circuit throughout, `DAC_RGBS_BYPS2DAC` 0, `OUT_SYNC_SEL` 0,
a solved raster of 2070 x 625, and every stage reads self-consistent.

**It needs a short output raster AND a source of few lines**, which the bench
mode confounds because 480p and 576p are the only outputs that turn the line
doubler off. Changing one at a time separates them:

| source | output | doubler | capture | result |
|---|---|---|---|---|
| 311 lines | 1080p | on | 954 x 582 | clean |
| 624 lines | 1080p | off | 914 x 576 | clean |
| 624 lines | 576p | off | 914 x 576 | coherent, right edge clipped |
| 311 lines | 576p | off | 1020 x 291 | **shredded** |
| 311 lines | 480p | off | 954 x 291 | **shredded**, indistinguishably |

480p failing the same way rules out the 625-line raster and anything keyed on
it, including the television reporting 800x600 for that mode. The doubler being
off is not sufficient and the short raster is not sufficient.

**It is not the encoder.** A 2072 x 625 @ 50 Hz raster carries a coherent
picture and a 2069 x 625 @ 50 Hz one is shredded, so one output timing produces
both and only the source differs.

Ruled out by measurement with the fault standing, each against an unchanged
control frame:

- `PB_CAP_OFFSET` 592 is indistinguishable; 148 gives the documented fetch
  overlap, so the stride is live and is not it
- `PB_FETCH_NUM` 510 is indistinguishable
- `IF_LINE_SP` 590 is indistinguishable
- `CAP_REQ_FREEZ` 1 gives a stable, identically corrupt frame, so it is not a
  capture/playback race
- `IF_HSYNC_RST` 590 destroys the picture, so the IF counts the 1180 it is set
  to
- `/sampleclock?md=2250` barely moves it

`VDS_VSCALE` at unity removes the combing and leaves the horizontal fault
untouched, so there are two artefacts: the combing is the vertical interpolation
and the horizontal one is upstream of the vertical scaler. `VDS_HSCALE` at unity
leaves the memory line's content stretched with its tail unwritten.

**The scaler cannot downscale, and that is NOT the mechanism.** `VDS_HSCALE` is
ten bits with 1024 as unity, so `produced` is never less than the capture -- but
it does not need to be. The display window is 1941 px against a produced 1941 px
on a 2070 px raster, matched to the pixel, and the capture is 1020 units, so the
window is wider than the capture rather than narrower and no scale value is out
of reach.

**The television reporting 800x600 is not evidence of a pixel budget either.**
The output raster is a TIMING: 576p names a line count and a field rate, the
horizontal total is whatever the display clock affords, and the encoder resamples
the analog line to whatever mode it chooses. A sink's reported mode therefore
says nothing about how many pixels the VDS emitted, and cannot be read as the
picture overflowing a line.

What is left unmeasured is where the horizontal content in memory comes from.
Every width recorded against this fault so far was estimated by eye off a
photograph, which is the method that invented `CORNER_H` and
`PANEL_VISIBLE_LEFT`. Calibrate first -- difference two frames at different
`VDS_DIS_?B_ST` so the difference IS the strip the register blanked -- and the
estimates become measurements.

### `STATUS_SYNC_PROC_HLOW_LEN` latches one of two readings, and one of them is rejected

`SourceMeasurement::readSource()` computes the duty as
`STATUS_SYNC_PROC_HLOW_LEN / PLLAD_MD`. The register reports either the sync
pulse or its complement, latches whichever it took, and holds it -- measured on
ONE unchanged source, the RiscPC at 320x256@50 on `vga`:

| | `HLOW_LEN` | `HTOTAL` | `HSPOL` | quotient | what `forDuty()` uses |
|---|---|---|---|---|---|
| before a mode round trip | 2051 | 2206 | 1 | **0.930** | rejected, `FallbackDuty` 0.07 |
| after one | 156 | 2206 | 1 | **0.071** | the measurement |

Both readings are steady over repeated one-pass reads -- 2051 held across four
output resolutions and several solves, 156 held after a round trip through a
448-line mode. **`HSPOL` is 1 in both**, so the polarity bit does not predict
which, and 2206 - 2051 = 155 against a measured 156 says the two readings are
the same pulse counted from opposite ends.

`VideoSourceLine::forDuty()` accepts `DutyMin` 0.041 to `DutyMax` 0.152 and
substitutes `FallbackDuty` 0.07 outside that, so the complement state runs the
whole engine on a constant.

**It is invisible here because 0.07 and 0.071 give the same capture window** --
`IF_HB_SP2` is 82 in both states. It is not invisible on a source whose duty
differs: an 800x600@60 mode whose sync is 128 of 1056 has a duty of 0.121, and a
capture window built from 0.07 puts the line's origin about 5% of a line out.
That is the size of the line-offset fault in
`investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md`,
which records that duty measuring correctly at 12.19% -- so that mode was read in
the pulse state and a round trip could have put it in the other.

**The console says it now**, as
`duty refused: 931/1000 outside 41..152, falling back to 70/1000`.

**A sync type change is one way into the complement state.** Measured on
320x256@50 over six `SYNC 1` / `SYNC 0` round trips: the one leg where the
sync-type probe was never armed acquired the composite source on the
separate-sync configuration and read `2334 pulse / 2506 divider`, refused, where
`2506 - 2334 = 172` was available. So the two states correlate with which sync
path the source is being read on.
`investigations/a-sync-type-change-arms-no-probe.md`.

**Rejecting a reading and substituting a constant is silent**, which is what lets
this survive: downstream, the fallback is indistinguishable from a good
measurement. Taking the complement when the quotient exceeds `DutyMax` is the
obvious repair, but which reading the register is in has to be established first
-- both are self-consistent and only the picture can arbitrate.

### The recovery ladder livelocks, and nothing in it re-derives the divider

A unit can reach a state where it never acquires and **no remote recovery
clears it**. Reproduced by an input round trip, `vga` -> `ypbpr` -> `vga`: the
engine keeps the Wii's held line rate against a 15625 Hz source, `PLLAD_MD`
stays at the Wii's 1792, `STATUS_MISC_PLLAD_LOCK` reads 0 and
`STATUS_SYNC_PROC_VTOTAL` counts 155 where 311 is due.

Everything that is documented as a recovery was tried against it and none
worked: `/sc?~` twice, a source mode round trip, `/input?src=vga` again, an
`ADC_INPUT_SEL` bounce, and `/sampleclock` -- which is refused outright, being
gated to the pass-through channel.

**The console says why, and a register dump cannot.** The escalation ladder runs
to its top and starts again, about every five seconds, for ever:

```
recovery: lift SOG floor at pass 2          recovery: hold clamp at pass 34
recovery: coast window at pass 8            recovery: nudge mode detect at pass 38
recovery: sync processor dynamic at pass 27 recovery: hsync overflow protect at pass 48
recovery: release capture at pass 32        recovery: full reset at pass 150
recovery: reprobe sync type at pass 151
own V sync: yes after 3ms
recovery: own V sync found, the run restarts
```

Two defects, and they compound:

- **No rung re-derives the sample clock.** The stuck divider is the only thing
  wrong, and not one of the nine rungs writes `PLLAD_MD`. However long the
  ladder runs it cannot reach the fault.
- **A successful sync-type probe restarted the run. FIXED.** `own V sync found,
  the run restarts` put the pass counter back to 2, and the probe succeeds every
  cycle, so the ladder could never escalate past that rung -- a livelock rather
  than slow progress, and the pass numbers in the trace above are what made it
  visible. Own V sync is proof of a SOURCE, which is a reason not to move the
  mux: it is the input toggle's precondition now, and the counter keeps
  climbing. The trace above is therefore a pre-fix one, and the re-probe has
  since moved to pass 44.

**`STATUS_MISC_PLLAD_LOCK` held at 0 across a whole ladder cycle is the
detectable condition**, and the engine's held rate disagreeing with
`STATUS_SYNC_PROC_VTOTAL x` the field rate is a second. Either would justify a
rung that re-derives the divider from the measurement rather than trusting the
held rate -- which is the rung the ladder is missing.

Recovery needed `ESP.reset()` -- reachable remotely as `/uc?u`, which reboots
without touching preferences, where `/uc?1` would wipe them -- **followed by**
`/sc?~`. The reset alone leaves the unit at `DAC_RGBS_PWDNZ` 0 with nothing
acquired, which is the post-flash signature.

**This is the reliability class, not one fault.** The stuck divider, the railed
`HPERIOD_IF`, the post-flash no-picture state and the encoder's stale timing all
present as a working unit with no picture and all need a different kick, and the
table of which clears which is in this file and in `CLAUDE.md`. What they share
is that the engine cannot tell it is in one of them: every register reads
self-consistent, and the only instrument that distinguishes them is the console
cadence. A unit that has to be kicked by hand is the defect, not the kick.

### A 576-line source loses its right edge and flickers, scaled and passed through

720x576@50 on `vga`. Passed through: a full-screen picture with the title text
smeared illegible, the highest-frequency grating block moiring, the right-hand
edge clipped and flicker across the whole frame, colours correct. The bypass
divider being capped by the channel counter is the candidate for the smearing,
`investigations/the-bypass-divider-is-capped-by-the-channel-counter.md`.

**Scaled to 576p the same source still clips its right edge and still
flickers**, so neither is a property of the bypass route. The clipping is the
open third fault in
`investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md`
-- the emitted active window is wider than what reaches the panel, by 5.3% at
576p and 5.7% at 480p where 1080p fits with room to spare. `VDS_HSYNC_RST` is
the lever and the response is one for one, so `Geometry::solveRaster()` sizing
the two short rasters is where it is fixed.

### The encoder drops the link with nothing on the board moving

A unit left settled at 1080p on a locked source comes back to no signal at the
television with every register correct: pads enabled, DACs powered,
`STATUS_MISC_PLLAD_LOCK` 1, `STATUS_SYNC_PROC_VTOTAL` and `HTOTAL` reading the
source, `HPERIOD_IF` at the 431 the mode is due, the engine acquired and the
solved raster intact. Toggling `PAD_SYNC_OUT_ENZ` 0 -> 1 -> 0 restores the
picture at once with nothing else written.

That is the recovery in `investigations/encoder-stale-timing.md` firing where no
timing change has happened to arm it, so whatever the encoder lost, it lost
while the board held still.

### The divider is chosen from the crossover row, and the row is a trade

**A doubled line is capped again, and this time on a measurement.**
`SamplingClock::DoubledLineSampleLimit` holds it at 2200 ADC samples, because
the capture path stops writing video at sample 2236..2256 of a doubled line.
The earlier cap was removed on the finding that the tail green is the VDS's
one-line delay -- which is a REAL second cause and is now defaulted off, but it
was also the confounder in every measurement of the first. Measured with it
bypassed, the band is still there and it is the capture path.
`investigations/tail-green.md`.

`SamplingClock::recommendedDivider()` takes the most samples the ceilings allow
and oversamples only where that is free -- the kept count carries the pixels and
Nyquist is best effort (`docs/sampling-table.md`) -- and `Adc::maxDivider()`
answers at the ratio the row will actually install. Deleting the cap without that is measured and is
worse: the engine solved `divider 4012` at oversample 2, `sampling phase: no
clean window` repeated, the screen was a solid green block and the sink dropped
the link -- because `maxDivider()` asked at the 162 MHz row, which installs no
oversampling, so the ceiling came back as the 12-bit register maximum.

**The picture has now judged it on the doubled path.** At 320x256@50 the cap
takes the divider 2506 -> 2200 and the tail band goes with it: greenness in the
last quarter of the line falls from a peak of 22 over 156 photo columns to zero
columns above the noise, on two card patterns. What the cap costs is density --
2.15 kept samples per source pixel against 2.45 -- and `docs/sampling-table.md`
says where that bites: 1056x256 at 0.72 samples a pixel is short only because
this cap holds it there.

**The divider now follows the measured line rate**, where the cap flattened it:
4 counts per 25 Hz on a 15.6 kHz doubled line. Undoubled sources are immune where
the counter binds, because the wall does not move with the rate. Where the RATE
binds it, a wobble used to rewrite `PLLAD_MD` and re-latch the ADC PLL on every
measurement pass, which limit-cycles -- measured, and held against the rate the
divider was sized from instead.
`docs/investigations/the-divider-is-an-actuator-in-its-own-sensor.md`.

**The conversion budget is gone with it.** It preferred the oversampling row
that spent most of the ADC's rating, which stood in for the green block; under
a cap it bought a ratio by halving the kept count, choosing 1242 samples at 4x
over 2200 at 2x on a 31.5 kHz doubled line. Both scan modes take the most
samples the ceilings allow now, and a ratio only where it is free.

**The row is the real ceiling, and it is a trade rather than a limit.** The row
is chosen from CKO, which is `divider x line rate` alone:

| oversampling | CKO must be under | divider at 15625 Hz |
|---|---|---|
| 4 | 40 MHz | 2559 |
| 2 | 80 MHz | 5119 |
| 1 | 162 MHz | the 12-bit field |

So sampling density is bought with conversion quality, one for the other. The
twelve deleted preset tables all sit at 2553..2559, hard against the 4x row.

**Preserving the requested oversampling is not the rule either**, and that is
measured too: written that way, `recommendedDivider(90000, 4, true)` returns
**434** against today's 1764, because no fast source can be oversampled 4x at
all. The ratio has to be allowed to collapse as the line rate rises.

**The PLL does not explain the failure.** At divider 4012 the CKO is 62.7 MHz,
`PLLAD_KS` 1 and the VCO 125 MHz, inside the band
`investigations/adc-pll-lock-range.md` measures as locking. What failed is the
sampling-phase sweep, which is its own open defect above -- it scores on exact
equality while the count dithers by one.

What settles it: whether the phase sweep can find a window at a high divider
once it stops scoring on equality, and then a density-against-oversampling
judgement made on the picture at each row rather than in the arithmetic.

### Try the horizontal decimator, so the divider can rise without the capture overrunning

**The grating beats because the divider is too low to resolve it**, and the
divider is bounded by what the raster can SHOW rather than by the part: the
capture is in ADC samples, `VDS_HSCALE` cannot minify, so a line sampled finely
enough produces more units than the display window holds and the surplus is
cropped. `VideoPath` sizes the divider from `Axis::maximumCapture()` for exactly
that reason.

**`IF_HS_DEC_FACTOR` decimates in the input formatter**, which drops the count
the capture carries WITHOUT dropping the rate the ADC samples at. That is the
one direction the trade in `docs/sampling-table.md` does not currently take:
the kept count is what carries resolution and oversampling buys only freedom
from aliasing, but a decimator after a higher divider keeps the sampling density
that resolves the grating while handing the capture a count the window can hold.

Untried. What would settle it is a divider raised past what
`recommendedDivider()` allows with the decimator taking the difference, judged
on the finest grating of the card against the same framing.

**And `IF_HS_DEC_FACTOR` is only the coarse half of it.** Beside it sits a
12-bit DDA -- `IF_HS_RATE_SEG0..7` with `IF_HS_RATE_LOW` -- which scales down
continuously from 1.0x to 0.5x, where the factor steps in halves. The ratio
this wants is rarely a half, so the DDA is the better tool.
`docs/scaling-down-path.md` is the whole path, both axes, and what else it
would buy.

### The pass-through ADC PLL does not lock, and the finest grating beats

`STATUS_MISC_PLLAD_LOCK` reads 1 in **2 of 38** samples in pass-through against
**39 of 40** on the scaling path, on the same source in one window, and
`STATUS_SYNC_PROC_HTOTAL` wanders 2038..2040 where the scaling path holds a
single value. The picture shows it: the highest-frequency grating on the test
card beats, and whether it does changes between entries into pass-through.

Refuted: the charge pump. Walked across every value the three-bit field holds,
each latched, lock stayed at 0..2 of ~28 throughout -- which is why
`Adc::applySampleRate()` now writes one value for every path.

Not explained by the VCO either: `investigations/the-vco-gain-follows-the-vco.md`
has 128.8 MHz locking at gain 0, and pass-through runs 128.5 MHz at gain 0.

The untested difference is the oversampling -- that sweep ran at `os=1` and
pass-through solves to 2, which halves `PLLAD_CKOS` and doubles the conversion
clock. It cannot be tested through `/sampleclock`, because that route re-enters
`HdBypass::applyPassThroughSampling()` and re-imposes the solved oversample.
`investigations/the-pass-through-adc-pll-does-not-lock.md`.

### A bypass round trip leaves the HD channel and the phase adjusters loaded

The sync-path half of this is **closed**. `SP_HS2PLL_INV_REG` was the cause of
94 output px of the displacement, and `SyncProcessor::normaliseHsyncPolarity()`
now clears it: the scaling path has already made the polarity one shape, so a
second inversion into the ADC PLL is pure displacement. Measured six round trips
before and six after, and the 94 px is gone.

What a round trip still leaves behind is the `HD_*` pass-through block loaded
with the raster bypass was driving, plus `PA_ADC_BYPSZ` and `PA_SP_BYPSZ`. None
of those has been shown to reach the scaled picture, and the residual
displacement measured after the fix is the ordinary landing set rather than a
bypass artefact -- so this is an ownership defect looking for a symptom, not a
known fault.

`SP_H_CST_SP` is separately excluded as the cause of the black band: frozen and
moved to 100, the picture is unchanged to within a pixel.
`investigations/leaving-bypass-leaves-the-sync-path-behind.md`.

### Nothing bounds the horizontal capture, so a default framing can arrive broken

The picture breaks up when the capture grows past about 2% of the **output
raster**, and only the vertical axis is bounded: `Axis::maximumCapture()` has one
call site and it is `AxisVertical`. `VDS_HSCALE` cannot minify -- ten bits with
1024 as unity -- so once zoom-out has pinned the scale at 1023 every further
unit of capture is a unit of produced picture, and the playback is asked to
fetch more pixels per output line than the line has clocks.

It is not only a zoom-out edge case. **640x480@75 solves to a capture of 1448
against a 1280 raster**, 13% past the threshold, so the mode arrives corrupt
with nothing touched.

**That reproduction has moved and the entry stands.** 1280 was 1080p at 75 Hz,
which asks the encoder for 185.6 MHz against its 165, so a 1080p preference now
lands on 1024p and a 1352 px raster -- the overshoot narrows from 13% to 7% and
the capture is still unbounded.
`investigations/the-encoder-ceiling-is-the-raster-floor.md`.

Measured at four rasters, the divider held so only the framing moves:

| raster | clean at | corrupt at |
|---|---|---|
| 1280 | 1288 | 1328 |
| 1600 | 1625 | 1633 |
| 1920 | 1693, the capture ceiling | -- |
| 2400 | 1827, the capture ceiling | -- |

Memory bandwidth reaches it: at a capture 1.1% under the threshold, dropping the
memory clock from 162 MHz to 108 MHz corrupts it.

What settles it: a horizontal `maximumCapture()` derived from the raster the
engine has already solved, and then whether the 2% of slack is the write FIFO or
the output blanking.
`investigations/the-capture-may-not-outgrow-the-raster.md`.

### Above a 2047-unit line the capture window's start does not fit its register

`IF_HB_ST2` is eleven bits and nothing bounds the write. At a held divider of
2200 the engine writes 2199 and the chip holds **151**, a window whose start is
past its stop, and the picture is horizontal streaks. Every other register reads
correct for the framing, and the wrapped one reads plausible.

This is the one thing a divider ceiling genuinely has to prevent, and
`SamplingClock::KeptCeiling` at 1900 prevents it only by accident -- the bound
is on the IF line, not on the kept count, so a doubled line at twice the divider
is equally safe and is not treated as such.

What settles it: clamp the window to the field, and say so when the clamp bites.

### The Wii's 480p framing leaves a wide margin and clips the right edge

`ypbpr` on the Wii in 480p acquires and holds -- `state: acquired`, 525 lines,
59.940 Hz, FrameSync ready and driving -- and the framing is wrong in a way the
bench RISC PC's is not: a wide black margin, the right-hand edge clipped, and
horizontal pan clamping before it reaches the picture.

Measured at that acquisition: capture `206..1216` of a 1449-unit line and
`36..516` of 525, `lineRateHz` 31468, `PLLAD_MD` 1448.

**The divider is the thing to look at first, because its VCO is below the
documented lock edge.** `PLLAD_KS` reads 1 at that divider, so
`CKO = 1448 x 31400 = 45.5 MHz` and the VCO runs at **90.9 MHz** --
`investigations/adc-pll-lock-range.md` puts the edge between 110 and 120 MHz and
solid lock from 150 MHz up. A divider of 1096 at the same line rate gives CKO
34.4 MHz, `KS` 2 and a 137.6 MHz VCO, which is inside the range, and 1096 is what
the working record for this mode carries. `SamplingClock::recommendedDivider()`
has a CKO ceiling (`Adc::maxCkoFor()`) and no VCO floor, so nothing stops it
choosing a divider whose VCO cannot lock.

Whether that is what the framing is about is NOT established -- the margin and
the clip are a placement question and the divider is a sampling one, and they
have not been separated. `/sampleclock?md=1096` is the instrument, because the
whole PLL group has to move together.

Acquisition also took about 40 s against the 15.2 s on record for this mode.

### The six-tap filter is forced on at every mode change, and the web UI reports the preference

`VideoPath::applyPictureFilters()` writes `VDS_TAP6_BYPS` from a hardcoded
`true`, so the filter is in circuit after any mode change whatever
`uopt->wantTap6` holds. The option is stored (`six-tap` in the settings file,
defaulting to 1), the OSD's `'t'` key toggles it, and the status frame reports it
in `toSend[4]` bit 3 -- so a unit told to turn it off shows the box clear and
runs the filter anyway from the next source change onwards.

Unlike the line filter and peaking beside it, which are passed in, nothing reads
the preference on the setup path. Whether the preference should be honoured or
the option retired is the open part: the OSD case is marked unused, and no
measurement here says what the filter costs or buys.

Passing it in as a third argument is the whole of the fix if it should be
honoured.

### One reading of the sync width decides the duty, and on sync on green that register is bimodal

`SourceMeasurement::readSource()` takes ONE reading per mode change, guarded only
by the divider being latched and the ratio being inside `HsyncPulse`'s band. On
the Wii at 480p on `ypbpr` the register holds 135/136 for a mean run of 6.3
samples and jumps to 163..166 for up to 4, 21% of 1143 samples — and
`STATUS_SYNC_PROC_HTOTAL` equals the divider in 100% of rows in BOTH bands, so
`Adc::dividerLatched()` passes every excursion.

**The duty therefore differs between acquisitions of a motionless source**, and
it reaches three places. `SourceKey` carries it at `SyncWidthIdentity` of 0.002,
so three consecutive solves key `/877`, `/377` and `/732` and every one prints
`no framing stored` — no framing can be recalled or usefully kept for this
source. `VideoSourceLine::forDuty()` and `SyncProcessor::retimeStopFor()` place
the capture from it, and three consecutive boots gave `/geometry` `fh` of 137,
70 and 164.

**A STEADINESS RUN IS THE FIX AND IT IS BLOCKED UPSTREAM.** A run that waits for
the width fixes the duty completely — `fh` 137 on five boots of five, every
excursion refused — and stops the source acquiring, because each waiting pass
has the rate measured again, which on a dithering count installs a different
divider and re-latches the ADC PLL: liveness 10.0% and 1.2% against 99.4% on the
shipped build. A run filled from the passes the rate already spends costs no
pass and no liveness, and leaves the excursion landing when the duty pass is
reached before the run settles — `fh` 137, 165, 136.

So the pass has to be made free first: a pass of the mode-change window must
stop costing a rate re-measurement and a divider re-install. That is the same
loop the rate re-solve and the terminal reference-retry cycle are made of.
`investigations/the-sync-width-is-bimodal-on-sync-on-green.md`.


## Fixed, kept here until the next session has seen them

### A composite source passed through could not leave a reference clock -- FIXED

`measureSource()` states the rule: the clock the rate just measured asks for,
installed BEFORE the duty is read. The pass-through route did not keep it. The
channel's clock is `HdBypass`'s rather than a solve's, so
`VideoPath::installSampling()` returned on its first line, and the only action
that re-asserted the channel's clock ran from `passSourceThrough()` --
downstream of the duty. `takeDuty()` refuses a reading through a clock the PLL
is not running, so the two closed a loop: a reference clock installed by the
rate-failure retry could not be replaced, because the duty needed the channel's
clock and the channel's clock was only re-asserted after the duty succeeded.

`VideoSourceAcquisition::installSamplingForMeasurement()` is the one step both
routes take now, and `installPassThroughClock()` treats a clock the PLL is not
running as not in force. `HdBypass::applyForSource()` splits so the raster half
follows the duty while the clock half precedes it, and the raster is applied
unguarded -- it writes no part of the PLL group, so a count that moved at an
unchanged divider is no longer suppressed.

Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1` passed through to
1920x1080, `/geometry` and the registers read together at about 4 Hz:

| | before | after |
|---|---|---|
| readings acquired | 370 of 622 across 180 s | **788 of 865** across 240 s |
| longest unbroken absent run | **78.8 s**, running to the end of the window | **5.8 s**, the initial acquisition |
| after the first acquisition | -- | 788 of 834, longest run **4.9 s** |

`/sc?~` on that route acquires in 17.1 s, and 14.3 s after a `/uc?x` toggle. The
Wii on `ypbpr` acquires in 3.6 s on the same route.

### The search's own pulse ignore sat below the floor at a latched divider -- FIXED

`applyForSearch()` wrote `SearchPulseIgnore`, 16, whenever the engine was
hunting. That value is sized for the chip's own reference clock, where a pulse
is a third of the samples it is at a divider -- and the search is entered from a
latched state as well as from a cold one. Through the divider's own line 16 sits
under the floor an unserrated vertical interval imposes, so the count the search
then took was not the source's, and a count that is not the source's is what
keeps the source hunted for.

`Dynamic` carries whether the block is counting the line the divider describes,
from `VideoPath::samplingClockLatched()`, and the search takes the divider's
fraction where it is and its own value where it is not. Both halves were already
the stated rule: the fraction is of the latched line, and `present` was standing
in for latched.

Measured on `vga`, the RISC PC at 800x600@60 with `SYNC 1` passed through to
1920x1080, `/geometry` and the registers read together at about 4 Hz:

| | before, 363 readings over 90 s | after, 622 readings over 180 s |
|---|---|---|
| readings latched with the threshold under the floor and the count not the source's | **114**, t+54.7 to the end of the window | **2** |
| `SP_H_PULSE_IGNOR` in that state | 16 | 99 |
| counts read there | 512, 558, 559, 824, 1069, 1185 | the source's 622 |

The source is still not HELD on that route, for the reason in "A composite
source passed through settles at a reference clock it cannot latch and arms
nothing" above, which the deadlock was masking.

`investigations/the-engine-makes-the-composite-count-unmeasurable.md`,
`investigations/the-pulse-ignore-is-a-fraction-of-the-line.md`.

### The sync pad came back before the mode was set up, and the sketch set it up a second time -- FIXED

The pad was driven from the flip to the acquired state, 0.2 s after the
solve and before the sketch's rate match, a second sampling pass and the
sampling phase; and 500 ms later the sketch's rate-change check re-ran the
whole preset path, arming a second measure, solve and rate match. Now the arm
takes the aperture and the pad away before its first write, the setup runs
once in order, and `VideoSourceAcquisition::presentWhenSettled()` gives both
back a pass after the solve, once the pad has been away `MinimumSyncAwayMs`,
the divider has latched and the phase has been searched. The sketch's
re-entry is deleted. Measured on the console across five changes: away on
the arm, one divider install, one duty, one rate match, the phase, driven;
dark 3.7 to 4.1 s off the dongle where the sink takes its first attempt.

**The early return was not what placed the sink's window short.** A settled
toggle with picture at the edge lands where the transition did, to a tenth
of a unit, on the two rasters that showed it.
`investigations/a-transition-and-a-settled-toggle-place-the-window-alike.md`.

### A mode change into a falling-back resolution installed one extra divider -- FIXED

The output for the arriving rate was chosen after the divider had been
installed against the previous mode's raster, and the re-install was then
suppressed as the same rate: 1548 against the 1024p fallback on a 240x352@70
-> 640x480@60 change, with the 1080p raster's 1444 landing only through the
sketch's second pass. The rate is measured first now, the output chosen for
it, and the divider installed once against that output's raster; the console
shows 1444 straight away on that leg and the host pins it. The 800x600@60 ->
640x480@75 leg the entry was measured on takes the same route and is not
re-measured.

### 640x480@60 was captured too narrow and magnified to fill -- FIXED

`eh` 623 of `ch` 1023 was 61% of the line where the matched raster puts active
video at 80%. It measures `eh` 1159 of `ch` 1449 now, which is 79.99%, and the
emitted frame carries the card's green frame at both ends of the line.

The capture window was opened a whole hsync pulse early, the polarity having
been compensated for twice -- once by the normalising write and again by the
placement.
`investigations/the-capture-floor-followed-a-normalised-polarity.md`.

### The vertical capture opened five lines into the picture -- FIXED

The top of the picture was cut and the same amount of the source's own blanking
shown at the bottom in its place: 17 black rows at 640x480@60, 10 at
800x600@60. The published raster stated its vertical start as the back porch
alone, which is right only where the vsync pulse is as wide as the counter's
origin is far from its leading edge. Measured across four pulse widths, that
origin is a fixed 7.3 lines and does not follow the pulse.

Now 3/6, 3/5 and 2/3 rows at 640x480@60, 800x600@60 and 1024x768@60, with the
card's green frame on the panel at both edges.
`investigations/the-vertical-capture-window-is-placed-late.md`.


### The ADC sampling phase was chosen against the oversampling ASKED FOR

**FIXED.** `rto->osr` carries the REQUEST -- `Adc::OversampleAsClockAllows`,
which is 8 -- and `Adc::applySampleRate()` clamps it to the ADC PLL's crossover
row and returns what it installed. That return value was discarded, so nothing
held the ratio in force and `Adc::acquirePhase()` saw the 8: both of its
`choosePhaseAdc()` arms test `oversample == 4`, which an 8 never satisfies, so
the half-sample offset they exist to apply could not run on the engine's path.

`Adc::oversampleInForce()` holds it now, and the console says what the search
was given and what came of it. Measured after: `sampling phase: no clean window,
oversample 4` on the bench source against `rto->osr` 8.

**It reaches the picture only where the search succeeds**, which on this bench
is the Wii. Photographed at both phases on both sources with the same state shot
twice as the control, the two are indistinguishable: PM5544's finest grating
gives 45.13 / 45.56 at the new phase against 45.00 / 45.55 at the old, and the
Wii's text 2.16 / 2.02 against 2.05 / 2.02 -- the control's own repeat spans the
whole difference in both.

### A stored framing of shape 0 fills the raster, so a 4:3 source is stretched

**OPEN -- not yet established whether anything but a user wrote it.** The
framing table stores the shape beside the framing, and 0 means fill. Read off
the bench unit, both entries for the RiscPC at 320x256@50 carry it:

```
# framing, one source a line: <lines>@<fieldRateHz>/<syncWidth><hPol><vPol> = originH extentH originV extentV shape
311@50.45/732++ = 2570 6412 1186 8269 0
311@50.06/686++ = 2153 6249 1154 8205 0
```

So `/geometry` reports `aspect: 0, shaped: true` and the card is stretched to the
full 16:9 raster, where the 4:3 default would pillarbox it. It survives `/sc?~`,
because a stored framing is meant to.

**The two entries are also why the two sync types frame differently**, which
reads as a composite-sync pan and is not one: the sync width is part of the key
-- 732 on composite against 686 on separate -- so each arrangement has its own
entry, and these two were tuned to different framings. That is the design.
docs/source-identity-and-framing-lookup.md.

**What is not established is how shape 0 came to be stored.** If the auto-save
can write 0 where the source matched no published raster, every untuned source
ends up filling and the shape default never reaches the picture. Deleting
`/framing.txt` restores the defaults and throws away the tuning with it, so that
is the user's call rather than a repair.

**A SOURCE MATCHING NO PUBLISHED RASTER IS REACHABLE WITHOUT A USER, AND IT IS
MEASURED.** A sampling-clock install leaves a key taken from the count and the
duty read across it -- `521@59.94/1100??` on the Wii at 480p, where 480p is 525
lines -- so the auto-save has an unmatched key to store without anyone touching
the shape. The line-filter entry above carries the console readings.

**What that does NOT establish is the shape going with it**, because
`SourceTiming`'s 720x480p row declares `F43`: a matched key and an unmatched one
both give 13333 on that source, so `aspect` 17778 to 13333 is equally the user's
own `/uc?G` being discarded. **A SECOND TRIGGER DOES SEPARATE THEM AND NEEDS NO
RESTART**: `/uc?G` to 17778, then `/sc?B`, and the aspect is 13333 again at once
-- `geometry.reset()` re-derives it from the key and the chosen shape is gone.
Measured on the Wii at 480p, one command pair. So the user's shape does not
survive a framing reset, which is separable from whatever the key is doing, and
a source whose declared shape is not 4:3 is what would separate the two.

### The Wii on ypbpr takes about seventy seconds to acquire 576i

**OPEN.** Measured with the console attached, `/input?src=ypbpr` to a presented
picture: `state: absent` for the first ~70 s, the first unrefused duty at 72 s
(`duty: 156 pulse / 2200 divider, htotal 2200, negative`), acquired and holding
afterwards at `STATUS_SYNC_PROC_VTOTAL` 310 x 50.02 Hz. `docs/bench-sources.md`
records 576i as about 40 s.

The console shows the derived line rate ping-ponging for some 45 s of that --
15924 and 16025 alternating, each one re-deriving the same divider 2200 -- with
`recovery: full reset at pass 150` in the middle. An interlaced source alternates
its count by one, which `SteadyRun::agree()` accepts; the LINE RATE the pair
gives differs by 6.3 per thousand, which is what has to settle before a solve.
Whether that is the gate has not been established.

**The emitted frame could not be judged**: it is a static flat yellow field with
line structure, stable to 0.01 grey levels across 12 s, and nothing here can say
what the Wii was displaying. The colour path is configured as designed --
`ColourSpace::applyYuv()` writes `DEC_MATRIX_BYPS` 1 deliberately, all three DAC
channel enables read 1 and the three ADC gains are equal -- so the yellow is not
the `DAC_RGBS_B0ENZ` signature. Establish the Wii's output mode and what it is
showing before judging the input.

### Returning from composite to separate sync left the output black for ever

**FIXED.** `SP_DIS_SUB_COAST` was written by `SyncProcessor::prepare()` and by
nothing that follows the sync type, so a source that had acquired on composite
sync kept the sub coast enabled when it went back to separate sync -- while
`prepare()`'s own `applyDefaultCoastWindow()` put the sub-coast window back to
its default 16..256.

The sub coast is the coast WITHIN a line: `SP_H_CST_ST`/`SP_H_CST_SP` mask a
window of the retiming so a serration cannot be taken for the line's hsync. The
default window masks exactly where a separate-sync source's own hsync edge
arrives, so the retiming lost the edge and the ADC PLL never relocked.

Measured on the bench, RiscPC 320x256@50 on `vga`:

```
csync, acquired:    SP_DIS_SUB_COAST 0   SP_H_CST_SP 1672   HTOTAL 2200   PLLAD_MD 2200
back on separate:   SP_DIS_SUB_COAST 0   SP_H_CST_SP  256   HTOTAL 3245   PLLAD_MD 2200
write the bit:      SP_DIS_SUB_COAST 1   SP_H_CST_SP 1672   HTOTAL 2200   PLLAD_MD 2200
```

`STATUS_SYNC_PROC_HTOTAL` wandering 3218..3251 against a divider of 2200 is an
unlocked ADC PLL at the CORRECT divider, so every duty reading was refused as
`UNLOCKED`, the solve never completed, and `presentWhenSettled()` never drove the
sync pad back: `sync pad: away` with no `driven` after it. The console reported
`sampling: 311 lines x 50.08 Hz -> line rate 15625` 482 times in 60 s throughout,
so every instrument but the picture said the unit was healthy. The escalation
ladder climbed to its last rung without clearing it -- including
`RestartSamplingClock`, which re-applies the whole PLL group and latches it --
and `/sc?~` cleared it at once. A full 1536-register diff between the wedged and
recovered states named this one bit: the PLL group and every other sync-type
field were byte-identical.

**The bug needed the composite leg to acquire first**, which is what ran
`prepare()` with serration in force, so a round trip whose composite leg never
settled did not reproduce it.

**This is `sp-sog-mode-had-two-owners` one field later.** That repair moved
`SP_SOG_MODE`, the coast pair and `SP_NO_COAST_REG` into `applyForSyncType()`
and left the sub coast behind. `applyForSyncType()` now takes `serrated` and owns
it, and `prepare()` no longer writes it.

Guarded by `test_sync_type_round_trip.py`, which measured 3 of 3 return legs
failing before and 6 of 6 legs settling after, and by `a source with its own
hsync is left with no sub coast` in `test_sync_processor.cpp`.

### Composite sync re-solved every two to four seconds, and the sink dropped it

**FIXED.** `countMoved` compared a raw count against the solve's raw count
rather than using `SteadyRun::agree()`, so a source whose count alternates --
308/309 on the bench RiscPC under `SYNC 1` -- disagreed on half the polls and
each disagreement armed a mode change, a 1000 ms sync-type probe and a re-solve.
The loop never converged.

After: two probes and one solve in the first six seconds, then 74 seconds
silent, and a picture where both builds previously reported no signal. The
left-edge bar on that leg is a separate, pre-existing artefact.

### A source returning from composite sync to separate sync never re-acquires

**REFUTED.** Twelve phases over six `SYNC 1` / `SYNC 0` round trips on
320x256@50, scaling path: the return to separate sync settles in 4.8-5.8 s every
time, `ch` 1145-1159, 311 lines, duty 7.06-7.11%, 15625 Hz, and a photographed
picture the card labels as separate sync. The composite direction is the slow and
variable one -- 4.8, 7.9, 8.9, 9.9, 14.0, 28.4 s.

The entry rested on a poll that waits for the capturable region to hold one
value five samples running, which `/geometry` can satisfy from the PREVIOUS
acquired state. The pass-through route is a different state and still fails, in
its own row above. `investigations/a-sync-type-change-arms-no-probe.md`.

### `/testbus` could read a dead bus on every selector

**FIXED.** `Tv5725::TestBus::select()` drives `PAD_BOUT_EN` along with
`TEST_BUS_EN`, because the pad the signal leaves the chip on is the same fact as
the bus enable one stage further out: a selection nothing is driving is not a
selection. The three explicit writes in `TestBusRateMeasurement` are gone with
it, so one call owns "the pin carries this".

Measured before, frozen with the bit cleared by hand: **0 transitions on all 32
selectors**, which reads as every block being dead. After, the same sweep
reports the live ones. `calibrateAdcOffset()` clears the bit at boot, which is
how a sweep could arrive in that state without anyone touching it.

`/testbus` also takes `sig=` now and the header names it, so a stage is read on
a stated signal rather than whichever `SP_TEST_SIGNAL_SEL` the last caller left
-- the sweep calls `SyncProcessor::driveTestBus()`, which writes the module and
the signal together, instead of writing the module bare. Two sweeps taken at
different times are comparable, and each says what it read.

## Measured wrong, no picture consequence found yet

### `DAC_RGBS_ADC2DAC` reads 0 in pass-through

`rgbhv-bypass-trap.md` has it at 1 as one of the two tells that the ADC-to-DAC
route is in force. Measured 0 with `OUT_SYNC_SEL` 1 and a correct full-screen
passed-through picture, so either the tell is wrong or the route is reached
another way.

### `HD_VB_SP` keeps its resting value on an input-change entry

20, which is what `applyVerticalBlanking(0)` leaves, where the Wii's published
raster puts active video at 36. The active start line handed to the bypass
switch was zero on an entry taken by changing input from a scaled `vga`. Which
entries resolve the raster match in time is open.
`video-source-acquisition.md`.

### What normalises the composite pulse-ignore floor is not known

`SP_H_PULSE_IGNOR` is one fraction of the line, 49 per thousand, for every
composite source. Where the floor below which a source cannot be counted sits is
measured on one source at two dividers and the two do not fit one rule:

| source | divider | sync pulse | floor |
|---|---|---|---|
| RISC PC 800x600@60 composite | 2038 | 246 | **86** |
| RISC PC 800x600@60 composite | 1438 | ~174 | **none; 2 counts it** |

A floor absent at one divider and 35% of the pulse at another is neither a fixed
duration, nor a fixed fraction of the line, nor a fixed fraction of the pulse.
The fraction clears the measured floor at both -- 99 and 70 -- so nothing on the
bench is broken by it, and the risk it leaves is a divider low enough that the
fraction approaches the source's own pulse, which is what the threshold must
stay under.

**The measurement that would close it is the serrated source's own floor**,
which lies between 51 and 107 at divider 2200 and needs the Wii set to 576i --
a bench trip. With that the three bounds could be fitted together, or a clamp
below the measured pulse sized without risking the one source whose floor is
high. `STATUS_SYNC_PROC_HLOW_LEN` already measures the pulse, so a clamp needs
no new instrument.
`investigations/the-engine-makes-the-composite-count-unmeasurable.md`.

## Costs time rather than correctness

### The acquisition acceptance test asks whether the source was ever acquired, not whether it is held

`test_acquires_every_source.py` waits for one reading of `state: acquired` at
the right line rate and then asserts the route and the latch. A source that
reaches acquired and is then lost for good passes it: measured on `vga`, the
RISC PC at 800x600@60 with `SYNC 1` passed through, the deadlocked engine
reached acquired in **218 of 363 readings across 90 s** before stalling
permanently, and a run of the two composite cases passed with the deadlock live.

**So a green run of that file is not evidence the bench holds**, and the window
it cannot see is the one every stall on this page lives in. What the cases need
is the same reading at the END of a window as well as at the start; the
instrument is `/geometry` polled at a few hertz, which is what the measurements
in the entries above were taken with.

### FIXED: OTA ran out of heap margin, and a child array on every menu item was most of why

`OLEDMenuManager` holds `OLEDMenuItem allItems[OLED_MENU_MAX_ITEMS_NUM]`, and
every one of those items held `OLEDMenuItem *subItems[OLED_MENU_MAX_SUBITEMS_NUM]`
-- a parent's child array, carried by the leaves as well, which is most of the
pool. `OLEDMenuChildBlocks` holds those arrays instead and an item holds a
pointer that is NULL until its first child, so the storage follows the items
that have children.

**The pool's COUNT is not the waste and must not be cut.** The icon tree
registers 38 items at setup and the preset and WiFi pages re-register theirs on
entry, up to 16 and 6, so the peak is 60 of the 64 the pool holds. Sizing the
count down has four items of room, not thirty.

Measured either side on the bench unit, `GBS_DEBUG=1 BOOTLOG_BYTES=0`:

| | before | after |
|---|---|---|
| globals | 54028 | 50868 |
| `oledMenu` in `.bss` | 6999 | 3812 |
| free heap, regpanel polling | 11664 | 17192 |

espota then carried the image **first time with `regpanel.py` left polling**,
against the run of failures between 0% and 7% that the heap produced before.

**A FREE-HEAP FLOOR IS NOT A TEST FOR THIS, AND IT WAS MEASURED BEFORE BEING
REJECTED.** Free heap moves 13200..17192 on an unchanged unit with nothing but
clients attaching -- about 1.4 KB per websocket, and `WEBSOCKETS_SERVER_CLIENT_MAX`
is 5 -- so the load swamps the 3160 bytes the pool costs and a floor cannot tell
the regression from two open consoles. What pins it is the host suite's
`sizeof(OLEDMenuItem)` against a child array, plus the boot itself:
`panicAndDisable()` spins in `while (1)`, so a pool too small to register the
tree never reaches WiFi.

**The icon tree may still be retirable rather than merely shrunk.** `Osd::Menu`
draws the described page on the same 128x64 panel and already carries Input,
Smooth, Compatibility, Format, Pass Through and Reset. What only the icon tree
has is resolution selection, the preset slots, the WiFi page and seven of the
twelve ADV7391 TV modes, so retiring it is a feature question rather than a
storage one. `osd-menu.md`.


### The OLED preset page walks the slot array from an uninitialised index

`presetSelectionMenuHandler()` in `OLEDMenuImplementation.cpp` reads the slots
file into `slotsObject` and then loops `for (int i; i < SLOTS_TOTAL; ++i)`,
indexing `slotsObject.slot[i]` from a value nothing assigned. Whatever the
compiler leaves in that register decides where the list starts, so the page can
show no presets, a tail of them, or names read past the end of the array --
and the same build is consistent, which is what makes it look like a slots-file
problem rather than the loop.

`SLOTS_TOTAL` is 72 and the page caps what it registers at
`OLED_MENU_MAX_SUBITEMS_NUM`, so a start in range still truncates the list
silently.

Not reached from HTTP: the page draws on the unit's own panel, so the slot
list is only observable there or by giving the described menu the preset rows.

### `read_settings()` cannot tell a refused read from a refused file

`/fs/download` needs 10000 bytes of free heap and a console client costs enough
to take the unit below it, so the route answers **503** while a capture is
attached -- intermittently, as the heap moves either side of the gate.
`gbs_unit.fs_read()` maps every non-200 to `None` and `read_settings()` passes
that on, which is the same answer it gives for a file that does not reach its
`end` line. Those are opposite diagnoses: one is a console on the socket, the
other is a boot that refused the file and will refuse every save after it.
`docs/preferences-file.md`.

Measured 2026-10-14 on `ypbpr`: with a websocket capture running, one
`curl /fs/download?f=/preferences.txt` returned 503 and a `read_settings()`
call seconds later returned the whole dict.

What it costs is a hardware run. `test_slot_framing.py` asserts
`read_settings(host) == settings_before`, so a blip reads as a slot write having
changed the settings; `test_firmware.py` keeps `original = read_settings(host)`
and then calls `original.get(...)`, which raises `AttributeError` on the same
blip. Neither is reachable by `ruff`, and the suite is where it surfaces --
minutes in, against a live unit.

The fix is to separate the two answers rather than retry blindly: a read the
unit refused is not a reading at all.

### A sync-type excursion leaves two registers behind, and a re-acquisition does not clear them

A `SYNC 1` round trip on the bench RISC PC leaves `ADC_SOGCTRL` at **11**
against the 12 a `/sc?~` settles on, and `MADPT_VTAP2_COEFF` at **4** against
`Deinterlacer::init()`'s 6. Measured across all 1536 registers, with two
readings of one settled state differing in **zero** bytes as the control, so
neither is dither.

Both stick. A source mode round trip on `SYNC 0` re-acquires and restores
NEITHER; `/sc?~` restores both at once. The mechanism is that nothing on the
separate-sync path rewrites either — `acquireSeparatorLevel()` runs only in the
csync branch of the load, and the deinterlacer is steered only where the source
is interlaced — so whatever the csync visit left is simply carried.

**The cost is to measurement rather than to the picture.** A 1536-register diff
that crosses a sync-type excursion reports two fields that belong to the
excursion and not to whatever is under test, which is two candidate regressions
per comparison. Re-detect with `/sc?~` before the second reading, or subtract
the pair.

What is unsettled is whether either SHOULD be re-derived per acquisition.
`ADC_SOGCTRL` is a searched value and arguably should survive; `MADPT_VTAP2_COEFF`
describes a deinterlacer the separate-sync source is not using.

### A register snapshot taken straight after the oracle catches a solve in flight

`configure_oracle.py` reads the test bus and takes a capture burst, which costs
seconds, and the engine re-solves whenever the measured field rate wobbles — it
does so every few tens of seconds on the bench RISC PC at 320x256@50. A
`snapdiff.py --save` queued behind the oracle therefore lands inside the arm
about half the time.

The signature is `PAD_SYNC_OUT_ENZ` **0 -> 1** or `VDS_DIS_VB_ST` **1121 -> 42**
sitting among the rate-derived fields: the arm takes the sync pad and the
display aperture away before the first register it moves, and
`presentWhenSettled()` gives both back. Seen three times in one session, each
time reading as a regression in the geometry.

**Read `PAD_SYNC_OUT_ENZ` and `VDS_DIS_VB_ST` before believing a diff.** Pad
driven and aperture open is the settled state; anything else is a snapshot of a
machine mid-change. Polling the pair until they settle costs about fifteen
seconds.

### The bench's recorded margins are a BOOT figure, and a re-acquisition moves them

On `vga` at 320x256@50 into 1080p, with `/geometry` byte-identical either way,
`picstate.py` reads the vertical margins as **2/0 and a height of 1078 after a
boot**, and **0/10 and a height of 1070 after a `/sc?~` or a source mode
change**. Seven boots and three re-acquisitions, no exceptions, across three
builds. The horizontal pair is 242/254 throughout and the spread stays
109.1..109.5.

The capture window is identical in both -- `ov` 72, `ev` 512, `cv` 624 -- so
nothing the board emits moved. It is the sink placing its own window when the
link acquires, which `investigations/the-shown-window-is-latched-at-lock.md`
carries.

What it costs is a false regression: `242/254/2/0` is the figure a bench note
records, and a frame scored after a `/sc?~` reports eight rows missing from the
bottom against it. **Score a boot against a boot.** What would settle whether
the two placements are the sink's alone is the same reading taken on a second
display.

### `locked_steadily()` cannot be satisfied by an interlaced source, so six tests ERROR

`gbs_unit.locked_steadily()` requires `STATUS_SYNC_PROC_VTOTAL` to read the
**same** value `LOCK_SAMPLES` times running. An interlaced source alternates its
count by one, so the predicate is unsatisfiable on one however healthy it is.

Measured with the Wii in 480i on `ypbpr`, 40 consecutive reads: **260 twenty-three
times and 259 seventeen times**, interleaved, while the unit read
`STATUS_SYNC_PROC_HTOTAL` 2200 against `PLLAD_MD` 2200, `STATUS_SYNC_PROC_HSACT`
1 and `/geometry` `state: acquired`. `locked_steadily()` answered False
throughout.

**The firmware's own definition is looser, and it is the right one.**
`SteadyRun::agree()` treats a pair alternating by one as agreeing, which is why
480i acquires at all. The tooling asserts something stricter than the engine, so
it reports a fault on a unit that is working.

**The cost is six fixture ERRORs that read as firmware faults** --
`test_capture_origin.py` (two), `test_if_head_blanking.py` (three) and
`test_memory_window_parity.py` (one), each stopping at "no locked source after
90 s". They pass with the Wii in 480p or 576i, which hold a steady count, so
which mode the bench happens to be in decides whether the suite looks clean.

**The fix is for the predicate to mirror `SteadyRun::agree()`**: accept a pair
of adjacent counts. Not done -- what the tooling should call a lock is a
decision, not a transcription, and an interlaced source is the case that makes
the two definitions differ.

### An input is identified by a hardcoded line rate, so the bench mode skips three tests

`gbs_unit.SOURCES` pins one line rate per input -- `vga` 37879, `ypbpr` 31468 --
and `acquired_rate()` returns None for anything more than `RATE_TOLERANCE_HZ`
from it. Those are 800x600@60 on the RiscPC and 480p on the Wii, neither of
which is what the bench runs: the everyday source is 320x256@50, which measures
**15625**, and the Wii is currently in 576i, which measures **15549**.

Two consequences, and the second is the expensive one.

**`wait_for_acquisition()` can never return on the bench source**, so the
`on_vga` fixture takes its `pytest.skip("vga does not acquire: check the source
is on")` branch on a unit with a perfect picture. Three test modules depend on
it -- `test_input_selection_prepares.py`,
`test_input_selection_recovers.py`, `test_selection_measures_one_line.py` -- and
a skip reads as green.

**And the two sources cannot be told apart at all** while both run at 50 Hz and
~15.6 kHz: 76 Hz separates them, well inside the 900 Hz tolerance, so no rate
test can say which input is acquired. A stalled selection keeps the previous
source's solve and still reports `state: acquired`, so the rate is the only
discriminator there is.

The workaround in `test_acquisition_time.py` is to drive the RiscPC to
800x600@60 for the run -- making the `SOURCES` entry right by construction
rather than by hope -- and to LEARN the Wii's rate, whose output mode is a bench
setting with no readback. The fixture has not been changed; doing it properly
means the expected rate being measured rather than declared.
### `test_reset_puts_the_framing_and_the_shape_back` is intermittent in a suite run

It passes alone every time and fails perhaps one battery run in three, always on
the same shape assertion. Two causes have been removed already -- it read the
shape it started with and called that the source's default, and it compared the
shape without polling for it -- and neither was the whole of it.

What remains is most likely the press queue: each surface holds ONE press, so
presses sent faster than `loop()` consumes them coalesce, and this test walks
two levels and presses Ok four times. A press that lands on the wrong row leaves
the cursor somewhere the next `walk_to()` still finds, so the test gets further
than it should before anything disagrees.

**Judge a battery run by whether this one test is the only failure**, and re-run
it alone before believing it. A run where the colour suite fails as well is a
different fault -- that one is the unit being too slow, and the draw cost is
what moves it.

### A build with a boot log cannot be armed for OTA, and USB is the way back

The refusal is legible now: `/`, `/sc`, `/uc`, `/bin/slots.bin`, `/slot/set`,
`/slot/save`, `/fs/download` and `/fs/dir` go through `RouteHeap`, which answers
**503 with the free heap and what the route needed** instead of returning
without sending anything. A gate and a crashed handler are no longer the same
reading. `/sc` and `/uc` have a floor of 4000 rather than 10000, which is what a
queued byte and an empty 200 cost, so the arm route itself answers on a build
the web UI could not load.

**What is NOT closed is the arm.** `/sc?c` is queued for `loop()`, and `case 'c'`
calls `initUpdateOTA()`, which allocates. Measured three times at ~10.8 KB free
immediately after a reboot: 200 from the route, and `ota_probe.py` reporting the
unit not armed 5, 10 and 20 seconds later. So a boot-log build can be told to arm
and still not arm.

The boot log is what costs it -- `BOOTLOG_BYTES=4096` is 4096 bytes of globals,
and the default build is 0. So this is a property of the diagnostic build rather
than of the product, but the diagnostic build is the one a session flashes.

**A reboot does not open a window, and the claim that it does is wrong.**
Measured with `GBS_DEBUG=1 BOOTLOG_BYTES=4096`: **9184 bytes free at boot**,
below the 10000 a reply needs, and the menu's own Restart -- which queues `'a'`
through `userCommand` and so was never gated -- comes back to the same figure.
So the way to flash a boot-log build is **USB**, and a session that wants OTA
afterwards flashes `BOOTLOG_BYTES=0`.


### A mode change arriving while a solve is pending keeps the old divider's premise

`VideoPath::setOutputMode()` returns early where `modePending_` is already set,
so the output is stored and no divider is derived. The pending solve then runs
`installSampling()` through the MEASURED path, whose tolerance forgives two
dividers within 5% -- and 480p's 1876 against 576p's 1952 is 4.1%. The exact
comparison that makes a deliberate output change take effect is on the
`SamplingFollowsOutput` path, which this route does not reach.

What it wants is not a wider comparison but a re-measurement: the divider is
what the source is measured THROUGH, so a solve whose output moved underneath it
is measuring the source against a premise that no longer holds. A mode change
arriving mid-solve should abandon that solve and re-arm the measurement.

**The narrow rule is the one to write.** Re-measuring on EVERY output change
contradicts a tested invariant -- `test_video_path_raster.cpp`, "an output
change re-solves the raster without re-measuring the source" -- so the trigger
is `modePending_` already being set, not an output change as such.

Not reproduced on the bench: it needs a resolution picked inside the window
between a source event and its solve. The host can drive it directly.


### A mode change into a taller frame stalls seconds in the field-rate spin

Timed with `SamplingLog` at 25 ms across 311 -> 524: **5.0 s of stall in 21
passes**, individual passes taking 914, 794 and 783 ms. `getSourceFieldRate()`
is a blocking spin with no `yield()`, one field period nominal and up to
3 x 250 ms on retries, and `FrameSync::matchRate()` wraps its own rate
measurement in five more attempts. The reverse change is 2.89 s.

**An input change between the two bench sources is the same transition**, since
`vga` at 320x256@50 is 311 lines and the Wii at 480p is 524. `/input?src=ypbpr`
and back both spend it, which is why a switch that `CLAUDE.md` times at about
15 s can need several rounds of polling before `/geometry` reports acquired.

**THE SLOW DIRECTION IS THE OTHER ONE, measured end to end.** The 5.0 s above
is time spent inside the field-rate spin, not time to re-solve, and the two do
not rank the same way. Timed from the source mode change to the engine holding
the correct line rate, 311 -> 524 takes about 2.1 s every run, while
524 -> 311 took 3.9 to 18 s and rolled the picture throughout -- a separate
fault, the stale-divider deadlock, now fixed and bounded to about 5 s.
`investigations/hperiod-if-railing.md`. Reach for this entry for the spin;
reach for that one for a change into a SHORTER frame.

A bounded *poll until `STATUS_IF_HT_BAD` clears* is the candidate replacement:
`HT_BAD` re-locks within 25 ms measured and 1.4 ms nominal, against a 20 ms
blocking spin for the fallback. **The bound is essential** -- on the
separate-sync fault the measurement never converges and the flag stays 1
indefinitely.

**DERIVING THE FIELD RATE FROM THE LINE COUNT IS NOT THE FIX, AND WOULD REMOVE
THE ONLY CROSS-CHECK.** `STATUS_SYNC_PROC_VTOTAL` and `VPERIOD_IF` are both
LINE COUNTS -- RD-5725-1.1 gives the second as "input source V total lines" --
so neither carries a time base and neither states a rate. `HPERIOD_IF` is the
only register that does, as "input source H total pixels / 4" against the 27 MHz
reference. So `fieldRate = lineRate / lines` can only be computed from
`HPERIOD_IF`, which makes it the same reading rearranged rather than a second
one: `lineRateFrom()` already multiplies in that direction.

`measureLineRate()` calls `getSourceFieldRate()` **only** where the counter rate
is refused or uncorroborated -- exactly where `HPERIOD_IF` cannot be trusted.
Substituting an algebraic rearrangement of it there would agree with a railed
counter by construction, `ratesAgree()` would pass, and the railed rate would be
adopted. The fast path already takes no pin measurement at all.

The pin is not used for `VPERIOD_IF`, which is a plain register read. It carries
the FIELD RATE, and FrameSync's input and output vsync sampling, which needs a
phase and an output period that no register reports.

### FIXED, not yet confirmed at the bench: Bypass reported at 576p

A unit scaling correctly to 576p said it was passing through. Two display sites
switched on a table-shaped preset id -- the OSD's resolution display tested
`0x04` and `0x14` and fell through to an `else` drawing **Bypass**, and the
websocket status had no case either and sent the default the web UI renders the
same way -- while the registers said `DAC_RGBS_BYPS2DAC` 0, `OUT_SYNC_SEL` 0 and
a solved raster of 2070 x 625.

**It reads as a fault in the video path and sends a session after one.** The
board is the only instrument that reports which route is in circuit, so a wrong
answer there costs whatever is spent before the registers are read directly.

`presetID`, `presetIdFor()` and the id itself are deleted. An output mode names
itself and both sites ask it, so there is no second encoding left to disagree.
**Confirm by selecting 768x576 and reading the OLED**, which must draw
`768x576`, and the web UI's 576p button must light.

`RgbhvOutput` reports bypass on a unit that is scaling, below, is a second route
to the same wrong answer by a different mechanism -- `printInfo()`'s `m:15` from
`RgbhvOutput::isScaling()`. It is independent and is NOT fixed by this.

### `src/tv5725/` reaches registers through `GBS::`

`GBS` is the transitional flat view and exists **for legacy call sites** -- the
sketch, and anything not yet moved. Nothing under `src/tv5725/` should name it:
that directory is where registers are migrating TO, so a class there reaching
for the flat view is pointing back at the thing it replaces.

Two costs. The base list in `gbs_types.h` reads as a progress bar only while
every migrated register is named through its owner, and a `GBS::` reference to
one that has an owner makes the bar lie. And the shortest way to reach a
register stays the legacy way, so the next call site copies it --
`SyncProcessor.cpp` takes `STATUS_SYNC_PROC_HSACT`, `VTOTAL`, `HTOTAL` and
`HLOW_LEN` as `GBS::` while `SyncOnGreen.cpp` names `Tv5725::` for the same
register. `GBS` inherits the declaration so both resolve to one slice and the
values agree; this costs the migration rather than correctness.

A subsystem reaching its OWN block is the case to clear first, and
`STATUS_SYNC_PROC_*` is the instance: it is still declared in `Tv5725::Tv5725`
rather than in `SyncProcessor`, which is what leaves the flat view the shortest
route. Moving a block to its owner and dropping that file's `GBS::` references
go together, and doing both is what removes a base from `gbs_types.h`.

## Dead code whose fate is undecided

### `RgbhvOutput` reports bypass on a unit that is scaling

Measured on the bench RiscPC at 320x256@50 on `vga`, scaled, `OUT_SYNC_SEL` 0
with a clean full-screen picture: `printInfo()` reports `m:15`, which is
`BypassRgbhv`. `getVideoMode()` returns `heldStandard()` for an RGBHV source and
that picks the bypass spelling when `RgbhvOutput::isScaling()` is false, so the
class is holding the opposite of what the output is doing.

The sequence writes it twice and the second write loses. `detectAndSwitchToActiveInput()`
calls `holdStandard(BypassRgbhv)` -- which is `chooseBypass()` -- then
`applyPresets(BypassRgbhv)`, which converts its argument to `Rgbhv` and ends in
`holdStandard(Rgbhv)`, so `chooseScaling()` runs. It then returns 3, and the
caller's `syncFound == 3` branch runs `holdStandard(BypassRgbhv)` again, putting
it back to bypass after the load settled it.

**The blast radius is small and that is why it has survived.** `rgbhvBypass()`
reads true on a scaling unit, and its two gates are `updateCoastPosition()` and
`optimizeSogLevel()` -- but the coast window is placed by another route, measured
0/0 on separate sync and 7/3 on composite, which is correct for each. And
`getVideoMode()`'s 15 round-trips back to `Rgbhv` inside `applyPresets()`. So
nothing observable is wrong with the picture.

It is recorded because it is the shape step 12 removes rather than a bug to
patch: one fact -- what an RGBHV source's output is -- stored where two writers
can disagree, with no check that they do not.
`docs/video-source-acquisition.md`.

### The component-output option persists but nothing can set it

`component-output` is a saved preference, the web UI draws a toggle for it, and
`VideoPath::applyColourPath()` has a live branch behind it that re-takes the
chroma gains and the balance's rest. **The handler that would set it is
commented out in full** -- `/uc?L` in the sketch -- so the only way to reach 1
is to hand-edit `/preferences.txt`, and the web UI's switch reports a state
nothing it does can change.

So the branch is reachable and untested, and the toggle is a control that lies.
Measured on the bench with the option 0: the gains read 0x1C/0x29, which is the
input space with the modifier correctly idle.

Three ways out and no evidence to choose between them: restore the handler and
find out what a component output does on this board, delete the option and the
branch, or leave the branch and make the web UI stop offering a switch. What
the commented-out code also did -- forcing 720p and reloading a preset -- says
the original was more than a colour change, so restoring it is not a one-liner.

## The frame time lock arms itself on a cold boot with the option off

**Measured once, on a true cold boot, and not explained.** `/framesync` came up
`ready:true` and the console printed corrections at the design cadence, so the
option was enabled in RAM -- and the boot log says it was not:

    PREFS: LittleFS.begin()=1 at t=1737ms (took 1ms)
    PREFS: attempt 1 t=1738ms open=1 size=39 got=39 plausible=1 first=[35 30 41 30]
    PREFS: loaded presetPreference=5 frameTimeLock=0 slot=65 SeleInputSource=2 suspect=0
    BOOT: reason='External System'

**The read was clean**, so the power-up race on the SPI flash is refuted here:
39 bytes asked for and 39 got, plausible, not suspect. The file was positional
then, reading `50A000000111` with index 1 as `enableFrameTimeLock` -- verified
against the WRITE order rather than assumed. It is keyed text now, so the trace
reads `applied=` and `end=` in place of `got=`/`plausible=` and the setting is
`frame-time-lock`; repeating the measurement means reading those instead.
`docs/preferences-file.md`.

Nothing else can turn it on. `FrameSync::init()` is reachable from exactly one
place, `FrameTimeLock::unarmedBecause()`, which `blockedBy()` only reaches after
`conditions.optionEnabled` passes; `conditions.optionEnabled` is
`uopt->enableFrameTimeLock` directly; and the only writers of that field are the
defaults (0), the preferences parse, and `toggleFrameTimeLock()`, whose two
call sites are `/sc?W` and `/uc?5`. Neither was sent -- the bench operator did
nothing but switch the power off and on, and the web UI only READS that bit to
draw its switch.

**It did NOT happen on any warm `/restart`**, where the lock consistently came
up disarmed and had to be armed by hand. So it is cold-boot specific, which is
also where the preferences race lives even though this instance is not it.

Left unpoked on purpose: toggling it to inspect would spend the state. The
cheap check is the next cold boot -- `/bootlog` for `PREFS: loaded ...
frameTimeLock=` against `/framesync`'s `ready`, both before opening a console,
since the boot log stops recording once a websocket client takes delivery.

## Untried experiments with a known payoff

### The ADC sampling phase is a fixed guess, and the chip can score it

`Adc::acquirePhase()` sweeps the SYNC PROCESSOR's phase and scores each by
dither in the line count. The ADC's phase is not swept at all: it is set to
`MidField`, or half a sample off it at oversample 4, and left. That was
invisible while no phase took effect at all, and is now a live choice.

**The chip can measure it without the picture.** `TestBus::readHigh()` returns
the digitised video sample, which `runAutoGain()` already reads -- it watches
for `0x7f` as the green channel's clipping limit. A phase sampling on pixel
transitions averages neighbours and loses peak-to-peak; one sampling mid-pixel
returns the source's real levels. So the metric is the SPREAD of those samples,
maximised over the phase, which is how a monitor tunes its sample clock. The
reads being uncorrelated with pixel position does not matter to a statistical
measure, and a source with sharp edges is what it wants -- PM5544's frequency
wedge.

**Reproduce the artefact before trusting the metric.** The candidate symptom is
beating, reported as worst in bypass and as tracking the choice of `PLLAD_MD`.
Two mechanisms reach that and a phase sweep only answers one: in bypass the
divider is a hardcoded value rather than one solved from the source, so the
sample rate need not match the source's pixel rate and beats at ANY phase.
`the-bypass-divider-is-capped-by-the-channel-counter.md`. Bypass does digitise
-- `DAC_RGBS_BYPS2DAC` is the HD bypass channel to the DAC, and there is no
scaler resampling to mask a bad phase -- so the phase reaches it.

**Re-check the symptom first.** `PA_ADC_S` was arbitrary per boot until the
phase adjuster was restarted on apply, and is deterministic now, so the
behaviour being explained may already have moved.


### The recovery ladder escalates through every first acquisition -- FIXED

`unmeasuredPasses_` carries TWO facts: how long it has been since the engine
could measure the source, and where the escalation has reached --
`SyncRecovery::stepAt()` takes the position as `passes % CycleLength`. The first
climbs legitimately while a source is still being acquired; the second must not
move then, and nothing separates them.

A pass is 20 ms, so the rungs fall due in seconds:

| rung | pass | time |
|---|---|---|
| lift SOG floor | 2 | 0.04 s |
| reprobe sync type | 44 | 0.88 s |
| restart sampling clock | 60 | 1.2 s |
| full reset | 150 | 3.0 s |
| toggle input | 413 | 8.3 s |
| cycle restarts | 451 | 9.0 s |

**A component acquisition takes about ten seconds at its best**, of which 7.4 s
is detection, so the whole ladder -- sync-type re-probe, sampling clock restart,
full reset and input toggle -- runs *during* an ordinary YPbPr selection rather
than after a failure.

Measured on the bench, one selection of the Wii on `ypbpr`:

    13.68  evt,det found,2                          detection succeeded
    16.71  recovery: full reset at pass 150         3.03 s later
    19.38  sampling: rate 82991 -> divider 620      garbage
    28.33  sampling: rate 37879 -> divider 1438     the OTHER source's rate
    28.61  source moved: interrupt (627 lines, solved 627)

The engine then held a solve for the RISC PC while the Wii's signal arrived, and
the picture was sheared. `/sc?~` cleared it and the source acquired in 10 s.

**The two ladders are not the problem and are already mutually exclusive.**
`SourceMaintenance` runs on an acquired source and `SyncRecovery` on one that is
not, selected by `sourceState_`, and both read the same two counters by design.
What is wrong is that one of those counters is also the ladder's position.

**The ladder now carries its own position.** `recoveryPosition_` advances only
where escalation is warranted, and `unmeasuredPasses_` is left to mean the one
thing it says. The position is pinned while the engine has not yet had its
chance at the source now selected -- set by an input selection, cleared by an
acquisition -- so a source that is still being acquired escalates nothing.

**The grace is not permanent, and that is what keeps a stuck source reachable.**
Maintenance being withdrawn after being granted is detection concluding there is
nothing there, which spends the engine's chance: the rungs are what is left, and
the ladder runs from that point exactly as before. The `ToggleInput` rung is
therefore still reached on a unit with nothing chosen, which is the sweep a
fresh boot depends on.

Measured across the same `vga` -> `ypbpr` selection, before and after: recoveries
during the switch fell from **3 to 1**, and `recovery: full reset at pass 150`
-- the rung that wrote a garbage divider of 620 and left the engine solving for
the other source -- no longer fires at all.

**It does not fix the outcome, because the ladder was not the cause of it.** The
same measurement lands on the other source's raster either way; the sync
arrangement outliving the input change is what does that, filed below.

### The sync arrangement outlives an input change, so YPbPr measures the source on the other connector -- FIXED

**The ADC input follows the selection and the sync path does not.** `ADC_INPUT_SEL`
moves, `SP_EXT_SYNC_SEL` and `SP_SOG_MODE` keep the answer chosen for the input
being left, and the sync processor carries on watching the external H/V pins --
which still carry the VGA connector's hsync. The engine then measures the OTHER
source, live, and solves for it.

Measured with the RISC PC on `vga` at 800x600@60 and the Wii on `ypbpr` at 480p:

| state | `SP_SOG_MODE` | `SP_EXT_SYNC_SEL` | `ADC_INPUT_SEL` | `STATUS_SYNC_PROC_VTOTAL` |
|---|---|---|---|---|
| on `vga` | 0 | 0 | 1 | 627 |
| after `/input?src=ypbpr` | 0 | 0 | 0 | 627 |
| RISC PC moved to 320x256@50 | 0 | 0 | 0 | **311** |
| RISC PC back at 800x600@60 | 0 | 0 | 0 | **627** |
| after `/sc?~` | 1 | 1 | 0 | 524 |

**The third and fourth rows are what make it a leak rather than a stale number.**
The count on `ypbpr` FOLLOWS the RISC PC's mode, so the sync processor is taking
live edges from the connector that is not selected. `/geometry` reports
`state: acquired` at 37879 Hz over 628 lines throughout, and every register reads
self-consistent, so nothing in a dump says the wrong source is being measured.

**Nothing arms the re-probe, and the reason is circular.**
`Geometry::useSyncTypeProbe()` runs per source MODE change, and from the engine's
side the source never changed mode: it measured 627 lines before the input change
and 627 after, because it is the same physical signal. A source identity that
cannot move cannot arm the probe that would notice it had.

`/sc?~` is the recovery -- it runs `goLowPowerWithInputDetection()`, which forgets
the sync type -- and it is the only one. `/input` does not clear it, which is what
makes an input change appear to have been ignored by the HC32 when the analog
switches followed correctly.

**Three things had to change, and each hid the next.**

`sourceMoved()` arms on the SELECTION. `establishSyncType()` runs only while a
mode change is in flight, and an input change never looked like one, so the
re-establish was never reached at all.

The arrangement is held against the selection it was chosen for, so it cannot be
reused across a change of connector.

And `VideoPath` kept its own record of whether the sync type was known, beside
`SyncMeasurement`'s. A second record cannot see the sketch's own `forget()`, so
`setResetParameters()` -- which says the answer is unknown and then guesses
separate -- left `VideoPath` reading that guess as a measurement. Detection
drops to low power about five seconds into a selection, which is where the
right arrangement was being undone. `SyncMeasurement::syncType()` already
expressed probe-if-unknown, so the flag was an owner to remove rather than a
conflict to arbitrate.

After: the arrangement is applied 0.11 s after the selection and holds, the Wii
acquires at 524 lines and 31468 Hz, and the picture comes up with no `/sc?~`.
Selecting `vga` probes -- `own V sync: yes after 3ms` -- because VGA is the one
connector that can present either, and lands on separate.

**The arrangement is logged now**, because every register it writes is one
several other paths also write: which owner last had it cannot be read off a
dump, and this overwrite was invisible until the log said so.

    0.03  source moved: input (163 lines, solved 627)
    0.11  sync arrangement: composite or SOG for input 4

**`SyncMeasurement` still has more than one writer.** `setResetParameters()` and
`resetRunTimeDefaults()` both call `set(false)` beside a `forget()`, which is a
reset guessing at a measurement; detection decides a sync type of its own from a
sweep at `detectAndSwitchToActiveInput()`; and
`TestBusRateMeasurement::sourceFieldRateHz()` reads the held DECISION to pick a
test bus, which is why detection brackets a measurement with `set(1)`/`set(0)`.
None of those can now reach a settled input through `VideoPath`, but they remain
owners. `docs/acquisition-migration-plan.md` step 2 is what retires them.

### The component separator search cannot exit early, so it costs 6 s every time

`detectAndSwitchToActiveInput()`'s YPbPr branch runs a 6000 ms loop whose only
early exit is `VideoSignal::countIsSource(SyncProcessor::lineCount())`. Measured
across five switches to the Wii on `ypbpr` 480p, instrumented with
`SamplingLog::event()`: the count reads outside source range for the whole
window on every one of them, the loop always times out, and both paths then
`return 2`. **The search's entire effect is to burn 6 s and leave `ADC_SOGCTRL`
at 14**, which is the `choose(14)` the timeout applies and which is what works.

Detection cost 7.4 s on all five switches, against a 9.6 s best-case total
acquisition -- so this is most of the fixed cost of selecting the input, and it
is spent on a search that never succeeds. The ratchet inside it walks
4, 6, 8, 10, 12, 14, 1, 2 and round again, twice, at 400 ms a step; 14 is
therefore tried twice DURING the window without the count ever coming into
range, which is the evidence that the level is not what the loop is waiting for.

**What is not established is why the count is out of range throughout**, and the
candidate is that nothing has set a divider the arriving source can be counted
through -- the loop measures before any clock is installed for it. That is the
rule `docs/investigations/the-reference-divider-was-the-bootstrap.md` removed the
reference divider from, so the answer is not to reinstate it.

Removing the wait outright is not obviously safe: `choose(14)` is what carries
this source, and no other component source has been measured here.

### The 450 ms hsync wait in detection never waits -- FIXED

**Both halves.** `DetectionEntry::stepAt()` owns the decision now and the
caller loops on it, so the window is spent rather than sampled once, and the
instrument is `SyncProcessor::signalPresent()` -- transitions on the test bus --
rather than `STATUS_SYNC_PROC_HSACT`, which rails in both directions and said
nothing there.

**And the trace reaches the console.** `bootLogPrintf()` wrote to `Serial` and
appended to the boot log, which is `SerialM` minus the websocket, so
`LOWPOWER:`, `INPUT:` and `DETECT:` existed only on a cable -- and in the
default build, where `BOOTLOG_BYTES` is 0, nowhere a session could reach at
all. It goes through `SerialM` now; `SerialMirror::write()` appends to the ring
itself, so nothing is written twice.

Verified on the bench: `DETECT: enter`, `DETECT: found` and
`input selected: vga` all arrive on the websocket console across an
`/input?src=vga`.

### The divider should be keyed to the source identity, not to a raw measurement -- FIXED

`SamplingClock::recommendedDivider()` takes a measured line rate, so the divider
inherits that measurement's scatter and the same source lands on a different one
each boot. Measured on the bench RISC PC at 800x600@60, two boots of one build
on one mode:

    PLLAD_MD      1436 -> 1440      0.28% apart
    IF_HSYNC_RST  1436 -> 1440      = PLLAD_MD
    SP_RT_HS_SP   1335 -> 1339      = 93% of PLLAD_MD
    IF_HB_ST2 / IF_HB_SP2 / IF_LINE_SP / VDS_HSCALE / PB_CAP_OFFSET   follow

Those nine bytes are the WHOLE difference between the two boots across all 1536
addresses, so nothing else is moving and this is the scatter on its own.

**The measurement only has to say which source mode the input is in, and being
finer than that buys nothing.** Framings are already stored per `SourceKey` and
the raster is already solved once per source identity; quantising the divider to
the same granularity would make one source choose one divider every boot. A
wobble in the measured rate currently re-latches the ADC PLL, which is the cost
being paid for precision nothing asked for.

**It is a repeatability tidy and must not be sold as a fix for anything
downstream.** In particular it does not reach the frame time lock's phase noise,
which is timed on the ESP off the input formatter's vertical output.
`docs/investigations/the-frame-time-lock-saturates.md`.
**Do not quantise the RATE into buckets** -- `SourceKey.h` rejects that by
measurement, and `SourceKey`'s tolerance is the mechanism that has no boundary
to land near.

**It was worse than a repeatability tidy, and the bench has now been watched.**
The divider is an actuator inside the loop that measures it -- the field rate is
timed off the input formatter's vertical and the IF's line counter IS the
divider -- so re-deriving it per measurement pass limit-cycles. Measured across
one input switch to the Wii on `ypbpr` 480p: 48 divider writes over 40 s
alternating `31519 -> 1444` and `31440 -> 1448`, with no `source moved:` line in
the window, `STATUS_SYNC_PROC_HTOTAL` reading 1703 against the divider's 1448
and an explicit `UNLOCKED`. The 1446 the true rate asks for is never visited, so
**no tolerance on the divider converges** -- it decides how far each swing
travels and nothing else.

`installSampling()` now holds the divider against the rate it was sized from, at
the tolerance that means two readings are one source. A commanded divider and
one a mode change asks for are choices rather than measurements and compare
exactly. `docs/investigations/the-divider-is-an-actuator-in-its-own-sensor.md`.

### WiFi light sleep in the edge sampler does nothing and has no stated reason

`debugPinPulseEdges()` enters `WIFI_LIGHT_SLEEP` after the first edge and holds
it across the second -- the edge whose timestamp is the measurement -- restoring
`WIFI_NONE_SLEEP` only after the wait. `WIFI_NONE_SLEEP` is the low-latency
mode and light sleep is the one that adds wake latency, and the SDK only enters
it when the CPU is idle, which a busy-spin never is.

The comment above that loop explains the `delay(7)` and explains why there is
deliberately no `yield()` in the spin. It says nothing about the sleep mode,
which is inherited from upstream.

**Measured as making no difference**, by flipping it mid-boot so the
boot-to-boot variable is gone: a clean boot stayed clean with it on, and two
disturbed boots stayed disturbed with it off, one of them getting worse. So it
is a latency knob in the most timing-sensitive loop in the firmware, with no
reason recorded and no measured effect. Removing it is a tidy; keeping it wants
a reason written down.

### The same framing must reproduce at every output resolution

**The framing is stored as PROPORTIONS, so it scales with the raster and must
never clamp.** The zoom floor is derived as `raster / maxMagnification`, so the
reachable proportional range is raster-independent by construction: shrinking
the output shrinks `produced` with it, which lowers the magnification and moves
*away* from the floor. A framing that clamps at any output resolution is
therefore a defect in the arithmetic, not a limit of the hardware.

`SourceKey` is the line count and the field rate and nothing else, so one source
keeps one framing across an output change -- which is what makes this testable.
Hold one source, set one framing, and walk the output resolutions with
`/uc?s` 1080p, `/uc?p` 1024p, `/uc?f` 960p, `/uc?g` 720p. At each:

- `/geometry`'s `poh`, `peh`, `pov`, `pev` must be identical. If the stored
  proportions move, the resolution change is rewriting the framing.
- `capture x 1024 / scale` against that mode's raster must be a constant
  fraction, though every absolute number changes.

**480p (`/uc?h`) and 576p (`/uc?j`) are excluded from the walk**, and so is
576p from the host assertion. The framing arithmetic reproduces at both, but the
picture is shredded at both, so the leg cannot be corroborated by a photograph
and a green assertion would be its only evidence -- which is the failure mode
that let a register-only check stand in for a framing that had never been seen.
Put them back when
`investigations/a-short-raster-and-a-short-source-shred-the-picture.md` closes.

The arithmetic is pure, so most of this is a host assertion before it is a bench
run. **The camera is corroboration only**: a photograph-to-column mapping does
not survive an output mode change -- 57 columns adrift over a 1080p/960p/1080p
round trip -- so compare fractions of the panel, never absolute columns.

`/uc?<letter>` writes flash, so a bench run belongs behind `--preset-save` and
puts the preference back.

### Whether the VDS line filter is worth keeping at all

`uopt->wantVdsLineFilter` carries `VDS_D_RAM_BYPS`, the VDS's one-line delay,
and it is now defaulted off because with it in circuit the tail of every line is
destroyed. What it buys has never been established.

Against it: every VDS stage that needs a previous line is bypassed in this
firmware -- `VDS_PK_Y_V_BYPS`, `VDS_C_VPK_BYPS`, `VDS_BLEV_BYPS`,
`VDS_W_LEV_BYPS`, `VDS_NS_BYPS`, `VDS_SK_BYPS` and both SVM bits -- so the data
passes through a delay nothing reads, and vertical detail photographed A/B/A/B
differs by less than the repeat-to-repeat spread.

For it: the sketch's scanlines handler recommends it, and
`Deinterlacer::enableScanlines()` clears `VDS_W_LEV_BYPS`, which is a plausible
consumer. **That pairing is untested** -- scanlines do not engage on a
progressive source, so the branch never runs on the bench RiscPC.

What settles it: an interlaced source with scanlines on, photographed with the
filter both ways. The Wii on `ypbpr` in 480i is the interlaced source here,
though it never reaches `state: acquired`.

If nothing consumes it, the option, its preference byte, its slot field, its
OSD page and `VideoProcessor::setLineFilter()` all go, and the divider stops
having to dodge it.

### Whether the encoder's relock scales with the length of the output blank

A source mode change is 8.07 s of dark panel, of which 1.83 s is the engine and
6.24 s is the encoder re-acquiring after the sync pad comes back, on a leg where
the output raster does not change at all.
`investigations/the-transition-is-mostly-the-encoder.md`.

Flat against blank length means the blank is nearly free and can be lengthened
for robustness. Scaling with it means it is paid for one-for-one, and the blank
becomes the lever on transition time. Nothing distinguishes them yet, and the
answer decides whether the transition can be shortened at all: the reference
build is 2.9 s faster and shows visible junk instead.

Note the tension with `investigations/encoder-stale-timing.md`, where a stuck
encoder needed a 2.5 s sync drop and 0.4 s did nothing.

### The escalation ladder and the sampling clock group are not logged

`SyncRecovery::Step` names every rung and none of them is emitted, so which
recovery ran is not recoverable from a capture. `SamplingLog::event()` exists
for exactly this, carries a device-side timestamp, and has one call site.

The console cannot stand in for it: no console line carries a device timestamp,
the host's own are delivery times, and the console drops bursts under FrameSync
spam -- so ordering and duration taken from it are not evidence.

The same applies to the sampling clock: `smp,` logs the divider alone, where
`PLLAD_MD`, `KS`, `CKOS`, `ICP`, `FS`, the two decimators and `PLLAD_LAT` are
one setting loaded on a rising edge. Without the group a divider anomaly cannot
be told from a latch that did not happen.


### Sample VSACT fast enough to see whether it follows the pulse

`STATUS_SYNC_PROC_HSACT` and `_VSACT` are read as presence flags -- sync activity
is there -- rather than as the instantaneous sync level, and the evidence is
statistical rather than direct: at random phase HSACT is 1 in **2190 of 2190**
samples across two sources and both routes, where a level-follower would be high
only for the sync duty of about 7%, and it goes to 0 when sync is lost.

The direct form is reachable for VSACT and not for HSACT. The I2C bus runs at
**400 kHz** (`Wire.setClock` in `setup()`; the core remembers it across the
`Wire.begin()` in `startWire()`, so the bus-recovery paths do not drop to 100
kHz). One field read is a segment aim plus a register read, about 200 us, so
sampling tops out near 5 kHz: far inside a 20 ms field, far outside a 64 us line.

So a burst of back-to-back s0_16 reads inside one `loop()` pass, reported as a
value histogram, would settle VSACT directly. `SamplingLog` cannot do it -- its
floor is one sample per loop pass -- so it wants a small `GBS_DEBUG` route of its
own.

### The nine bus-exercise reads want a name

`GBS::STATUS_00::read();` appears three times in a row with the result dropped,
after `startWire()`, in four places. The register's content is never used; any
readable register would serve. What the block wants is a name for what it is
doing -- prove the bus answers -- and it currently reads as a status check that
forgot to check anything. `whole-byte-convenience-names.md`.

### The display clock could ask for 129.6 MHz rather than 108

`OutputMode::EngineCeilingHz` is 108 MHz on a usability argument that no longer
holds on its own terms: it rested on the zoom floor landing exactly on the
default framing at a scale floor of 500, and the floor is `Axis::minimum
Capture()` now -- 721 at the 2298 raster -- leaving real travel rather than
none. 129.6 MHz is already measured as working
and sharp, and buys a third more horizontal resolution. Not tried.

### A source's first solve after a boot can miss its stored framing

`/framing.txt` holds a framing per source keyed on the measured pair, and the
Wii at 480p has one: `524@60 = 1769 6090 669 9178`. Two boots of the same build
family, the same source and the same measurement -- `PLLAD_MD` 1096 against
`STATUS_SYNC_PROC_HTOTAL` 1096, `HPERIOD_IF` 214, `VTOTAL` 524 -- landed on
different framings for the FIRST acquisition after the boot:

| | `/geometry` | the framing applied |
|---|---|---|
| one boot | `oh 181, eh 623, ov 35, ev 480` | the stored entry, `poh 1769 peh 6090` |
| the next | `oh 47, eh 948, ov 32, ev 489` | the computed default, `poh 459 peh 9267` |

Both pictures are clean; the default shows more of the source than the stored
entry, which crops the Wii menu's right column. Every input switch AFTER the
first restores the stored entry, twice in a row on each of two round trips, so
what is intermittent is the first solve rather than the restore.

The table is read from flash at boot behind the same guard as the preferences,
so a first solve that runs before the read has nothing to restore from. Not
established: whether that is the mechanism, and whether a short read of
`/framing.txt` is silent -- the settings file is not, since `end` is what says
it arrived whole. `docs/preferences-file.md`.

### The search configuration writes a threshold the sync type owns

`SP_H_PULSE_IGNOR` — s5 0x37, the width in ADC samples below which a horizontal
pulse is ignored — has one value per sync arrangement, each measured:

| the source's sync | value | evidence |
|---|---|---|
| its own V sync line | 0xFF | the bench RiscPC counts a steady 311; on the Wii the same value reads 97, no lock |
| composite, unserrated | 0x02 | the Wii at 480p runs on it |
| composite, serrated | 0x6B | the Wii's 576i counts 310, where 0x02 gives 315/316 and 0x90 gives noise |

`SyncProcessor::applyForSearch()` writes **0x02 whatever the sync type says**, so
the escalation ladder hunts every source as though it were unserrated composite,
against `applyPulseIgnore()` writing one of the three from the sync type. Two
writers, contradictory values, on a field the investigation settled as following
the sync type.

**The cost is not measured where it would hurt.** On the separate-sync bench
source the two values are indistinguishable: 0x02 written onto a locked source
leaves `STATUS_SYNC_PROC_VTOTAL` at 311 in 10 of 10 samples over 9 s with
`HSACT` 1, and 0xFF restores identically. RD-5725-1.1 says why — the counter
"is start when sync large different", so the ignore applies while the separator
is telling pulse widths apart inside one composite stream, which is not what a
source with its own V sync line presents. The table says the serrated case is
where it bites, four to six lines high and perfectly steady, which no steadiness
run can see, and that case has not been provoked through the ladder.

**SERRATION IS A COMPOSITE-SYNC PROPERTY, so there are three states and not
four.** A serrated vertical interval is one chopped by continued line-rate
pulses so an H oscillator stays locked through it; a source with a dedicated H
line never stops sending them, so there is nothing to serrate and no pulse
widths for the separator to resolve. `applyPulseIgnore()` already encodes that —
`serrated` is read only under `csync` — and so does
`sourceHasSerratedSync()`.

**What it is not**: this does NOT explain a unit that comes back from a flash
searching with 0x02 standing, `STATUS_SYNC_PROC_VTOTAL` 0 and `HSACT` 1. That
was diagnosed here as the threshold and the diagnosis is refuted by the
measurement above; the state matches the documented post-flash one, and `/sc?~`
cleared it.

The resolution is the derivation the field once had: `HPERIOD_IF` for the line
and `STATUS_SYNC_PROC_HLOW_LEN` against `HTOTAL` for the sync duty, which is
what produced the 0x6B in force on the Wii, with `SyncMeasurement::probe()`
answering separate against composite. All three arrangements are on this bench —
`SYNC 0` and `SYNC 1` on the RiscPC, 480p and 576i on the Wii — so a derived
value can be checked against all three.
`docs/investigations/the-pulse-ignore-value-is-measured-not-chosen.md`.

## 800x600 in bypass clips the top and leaves a bar at the bottom

Observed, not diagnosed. With `preferScalingRgbhv` off and the RiscPC at
`MODE X800 Y600 C256 F60`, pass-through fills the panel horizontally and the
picture is clean, but the top of the source is slightly cut and a black bar of
roughly ten rows stands at the bottom. Both edges are the source's own, so the
capture window is not involved — bypass passes the source's timing straight to
the encoder.

**IT IS NOT ONE SOURCE, AND THE SHAPE IS WRONG AS WELL AS THE MARGIN.** The Wii
on `ypbpr` at 480p shows the same bottom bar in pass-through, and there the
picture's **aspect ratio is wrong with it** — so the vertical extent is not
merely offset, it is scaled, and a bar at one edge is the visible part of a
larger error rather than the whole of it. That rules out a pure placement
account, which a bar alone would admit.

It varies between entries into bypass: one calibration frame had the card's
outermost yellow band cut off at the top, and another taken after a bypass round
trip showed the band intact on all four sides, with the panel corners solved
from the two agreeing to under 0.7 px. So the vertical placement moves between
entries rather than being fixed.

Where to look: the vertical timing `bypassModeSwitch_RGBHV()` writes, and
`HD_VSYNC_RST` against the source's frame. Nothing here has been measured
against the encoder's own active window, and
`docs/rgbhv-bypass-trap.md` is what to read first.

**This is not the scaling path's framing flip**, which moves the picture
horizontally and is `HPERIOD_IF` quantisation reaching the raster solve.

### 800x600 in bypass shows a coloured band at the left edge

Observed 2026-09-20 while taking a pass-through panel reference, and not
diagnosed. The card carries a magenta band about 75 photo columns wide hard
against the left of the panel, inside the painted area and outside the card's
own black-and-white border. The reference is otherwise good -- the picture fills
the panel horizontally and the fit against it is what the active-window
measurement rests on.

What it is not yet separated from: the same magenta appears at the RIGHT of the
scaled picture at both 1080p and 1024p with the bench framing, which crops 123
capture units off the near end, so a card drawn with a magenta border at both
extremes would show exactly this. `PatLib` draws it at the source, so the source
is where to look first. `RiscPc/tools/video-source/`.

### `/sc?~` in pass-through strands the output off its DAC route -- FIXED

Low power detection from RGBHV pass-through left the unit dark with no way
back short of changing the output mode. The engine keeps measuring the source
and sizing the pass-through channel while the chip's routing sits where
detection left it, on the scaling path.

`passSourceThrough()` claims the route only on the way in, and asks the engine's
held output mode whether it is already there -- which detection does not clear.
So `passThroughSwitch_()` never runs again and `DAC_RGBS_BYPS2DAC` and
`OUT_SYNC_SEL` stay 0.

Fixed by having `outputIsPassedThrough()` ask `VideoRoute` -- the route in
force -- rather than the held output mode. Verified on the bench: the recipe now
holds `state: acquired` with the route claimed and a full picture.

Before the fix `/uc?x` recovered it, while a source mode round trip recovered
the engine's acquisition and not the picture -- which is why the arms looked
absent when the screen was what was being judged.

`docs/investigations/low-power-detection-strands-pass-through-off-its-route.md`
has the measurements and what they refute.

### The first and last source lines cannot both be shown

**Narrowed to sub-row granularity.** The capture window opens on the source's
first picture line and the aperture closes on the write, so what is left at each
end is under an output row.

The instrument is `PATTERN CARD`, whose `PROCframe` draws a one-pixel green line
on the source's outermost rows -- one source line, so it is present or it is
not. At 800x600@60 into 960p both lines are now at the extreme edges of the
display window and each reads about a third of the amplitude it has when fully
inside, which is what a half-shown row looks like.

The near end loses the 0.48 of a row between the aperture opening at the mode's
first active line and the write starting at `VDS_VB_SP` plus the origin offset.
The far end loses the fraction the floor discards. Neither is recoverable
without a finer scale; see the aperture entry above.

### The frame's lag was measured on one source and one scan mode

**Closed, and the constant is since deleted.** `FrameLagUnits` was taken to -7
applying in both scan modes, from -1.5 undoubled and nothing doubled, which had
put the capture five to six source lines inside the picture on every undoubled
source and cost the top of it. The lag itself then turned out to be the sync
retiming being bypassed and the term went altogether --
`investigations/the-capture-lag-was-the-retiming-bypassed.md`. The measurement
below is what established the symmetry. Measured with the green frame on three sources, one of them in
both scan modes:

| source | counter | scan | first picture line | it arrived at |
|---|---|---|---|---|
| 320x256@50 into 480p | 312 | undoubled | 36 | 29.4 |
| 320x256@50 into 960p | 624 | doubled | 72 | 64.3 |
| 800x600@60 | 628 | undoubled | 27 | 20.3 |
| 1024x768@60 | 806 | undoubled | 35 | 28.3 |

**It is a count of the COUNTER's units, and that is what one source in both scan
modes settles.** Read as source lines the same source is 6.6 early undoubled and
3.9 doubled; read as a fraction of the frame it is 0.021 of a 312-unit frame
against 0.011 of a 628-unit one. Read as counter units every reading is seven,
so the doubled branch is gone.

**The 1.5-line figure it replaces does not survive.** That was the difference
between the two scan modes taken on 320x256@50 by creeping until the source's
flashing border entered the picture; the same source measured here with the
green line gives 2.75.

**IT IS GOOD TO THE UNIT AND NO BETTER, AND THE CAPTURE MARGIN IS WHAT CARRIES
THAT.** One integer serves every source, and across six VESA DMT modes the
largest `IF_VB_SP` that still keeps the source's first active line lands one
unit apart either side of it: five modes sit exactly on the engine's own choice,
1024x768@75 keeps the line a unit later than the engine opens, and 1024x768@60
loses it at the engine's own value and keeps it one unit earlier. So a margin
equal to what the path drops leaves no slack at all, and `Axis::captureMargin`
is 2 vertically for that reason — one unit the path drops, one for this
rounding. Nothing here says the lag is anything but 7; it says the bench cannot
resolve it more finely than that.

### The green frame measures presence, not amplitude

`PATTERN CARD` draws one source pixel, which is what makes it the only feature
that IS the source's outermost line — and what bounds it as an instrument.

**A BURST OF STILLS BEATS WITH THE RING'S FLASH.** The ring flashes twice a
second and covers the frame in one phase, and each `tv-snap` takes about the
same time, so stills taken back to back sample one phase over and over. Twelve
evenly spaced stills read a line that is plainly on the panel as absent; the
same twelve with a jittered gap separate 23.4 from 2.0. `vesa_acceptance.py`
jitters.

**AND THE PEAK FALLS WITH MAGNIFICATION, BY AN ORDER OF MAGNITUDE.** The line
is one source line, so it occupies `magnification` output rows, and every stage
after the scaler resamples it — the encoder, the television and the camera. At
1.99x it reads 20..27 and at 1.25x it reads 3..5 with the SAME line captured,
and the sub-row phase modulates it with a period of `1 / frac(magnification)`
units of `IF_VB_SP`: alternate units at 1.59x, every fourth at 1.25x, and
uniform at 1.99x where a unit is a whole row.

At 1.05x the peak does not clear zero at all. 1280x1024@60 into `Mode1080p`
reads −5.4 at the top with `ov` 41 and `ev` 1024 — the whole published active
region captured and the framing default to the ten-thousandth. Differencing
settles it where the peak cannot: closing `VDS_DIS_VB_SP` over the first six
output rows removes 7 units of G−R from the photograph's rows 32..35, so the
green is there. **Read a low peak as the instrument's floor unless a creep shows
a cliff** — a line outside the capture window reads the same at every aperture
setting, and a line inside it moves.

### A half-unit lag kills the control that steps through it

Latent rather than live, and it cost a session. `CaptureWindow::place()` maps
the framing into the counter with `lrintf(fraction x units)`, and `lrintf`
rounds ties to even -- so where the lag is half a unit every whole-unit step of
the framing lands on a tie, the window does not move, and `VideoPath::step()`
reverts a framing that moved no register. The control is then dead for good
rather than coarse: the press that was swallowed once is swallowed every time.

It was reachable while `FrameLagUnits` was -1.5, on `/sc?*=1` at 800x600@60.
**Nothing reaches it now, because there is no lag term at all** --
`CaptureWindow::videoAt()` is the proportion and, where the counter zeroes on
the sync pulse's trailing edge, a whole-unit sync interval. **Anything that
re-introduces a fractional displacement into that mapping brings it back**, and
no test covers it because no constant can currently produce it.

### The source identity moves when the sync type does

At 800x600@60 on `vga` the line count reads 627 on separate sync and 623 on
composite, with nothing but the source's sync type changed. `SourceKey` is the
line count and the field rate, so the same mode on the same machine is two
sources across that change and a framing tuned on one is not found from the
other. The count itself is the entry below.

`STATUS_SYNC_PROC_HSPOL` moves with it too, and the information is LOST rather
than inverted: measured on three modes, it reads 0 on composite whatever the
mode's horizontal polarity, where separate sync gives 1 for an H+ mode and 0 for
an H- one. Composite sync is sync-tip-low and there is no separate HSync line to
read, so the bit carries nothing there and no correction recovers it. That is
why it may not join the identity. `STATUS_SYNC_PROC_VSPOL` does survive the
change, correct on both sync types across three modes and two polarities.
`docs/source-identity-and-framing-lookup.md`.

The sync width does survive it, shifting 0.12204 to 0.12017.

### Composite sync undercounts the line total, and the picture falls apart

The same source mode reads four lines short on composite sync, and that is
enough to take it out of the published raster table entirely.

Measured at 800x600@60 on `vga`, `SYNC 1` against `SYNC 0` with nothing else
touched:

| | `SP_SOG_MODE` | VTOTAL | `VDS_HSCALE` | `VDS_VSCALE` | capture lines |
|---|---|---|---|---|---|
| separate | 0 | **627** | 958 | 641 | 26..626, 600 |
| composite | 1 | **623** | **1023** | 621 | 37..619, 582 |

**The count is the root and the rest follows from it.** `SourceTiming::lookUp()`
matches on `measured.lines() + 1 == raster.totalLines`, so 623 asks for a
624-line raster and the table holds 628. Nothing matches, the timing goes
unpublished, and the solve falls through to the default guess -- which is a
different capture, a different scale on both axes, and `VDS_HSCALE` at 1023,
near enough unity that a near-full-line capture is played out unmagnified.

The picture shows exactly that: the card repeats about 1.7 times across, with
two crosses and two captions, over heavy green line tearing.

The ADC PLL is not implicated. `STATUS_SYNC_PROC_HTOTAL` equals `PLLAD_MD` at
1606 on both, `IF_HSYNC_RST` is the 1606 an undoubled line is due, and
`HPERIOD_IF` reads its correct 176.

The sync width survives the change -- 0.12204 against 0.12017 -- so whatever
miscounts the frame is counting the line correctly.

**THE SHORTFALL IS THE MODE'S VERTICAL SYNC WIDTH**, measured on three modes
whose vsync differs:

| mode | `v_timings` vsync | separate | composite | short by |
|---|---|---|---|---|
| 800x600@60 | 4 | 627 | 623 | 4 |
| 640x352@60 | 3 | 363 | 360 | 3 |
| 640x480@60 | 2 | 524 | 522 | 2 |

A fourth mode agrees: 320x256@50 has a vsync of 3 and reads 308 against 311.

**And the mechanism is already written down.** The RISC PC's composite sync
carries no serrations, so the broad vertical pulse offers the line counter no
horizontal edges and the lines under it cannot be counted.
`docs/investigations/the-risc-pc-composite-sync-is-not-serrated.md`. That makes
it a consequence of the signal rather than a fault in the coasting, so no coast
setting recovers the lines -- what has to change is the engine adding the
interval back, or matching a raster without it.

**One mode must look the same on both sync types**, so this is a defect rather
than a property of composite sync. It also makes the source identity move:
`SourceKey` is the line count and the field rate, so a framing tuned on one sync
type is not found from the other.
`docs/source-identity-and-framing-lookup.md`.
