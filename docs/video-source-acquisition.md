# Video source acquisition

Goal: `runSyncWatcher()` and `rto->videoStandardInput` are both deleted, and the
responsibility they share -- keep video coming, and know what is coming -- has
one owner instead of none. **Both are gone.**

**`VideoSourceAcquisition`** sits ABOVE `Tv5725::`. It owns the tick, coordinates the
measurement and decides. `Tv5725::SourceMeasurement` reads the source for it and
`Tv5725::VideoPath` is handed the answer and writes registers. Each of the three
holds what it alone reads, which is the rule the whole page turns on.
`loop()` ends up with one call where it has two:

```
inputAcquisition.poll(millis());
```

`VideoPath::poll()` did not survive: it was turned inside out rather than moved,
and is gone.

## One job seen from two ends

This was two pages -- one for the watcher's order, one for the standard byte's
semantics -- and each cited the other on every substantive point. They are not
two jobs. The byte's surviving readers are the branches the watcher runs, and
the watcher's parallel model of the source is what the byte exists to feed, so a
step that moves one moves the other whether it means to or not.

Both questions are still here and they are still different:

- **what the byte conflates, and what measurement replaces each fact** -- up to
  *The rule for every step*
- **the order the code moves in** -- *The order*, thirteen steps, with the
  byte's stages folded onto the steps that carry them

## VideoSourceAcquisition decides, and that is the whole design

Three parties, one direction of flow. Each holds what it alone reads.

| | |
|---|---|
| `VideoSourceAcquisition` | **decides.** Owns the tick, coordinates the measurement, decides the divider and the input, runs the ladder, reports "no signal out". Holds what the last solve ran against, because that is what it compares a fresh reading to |
| `Tv5725::SourceMeasurement` | called by it. Reads the source off the chip -- the only thing that does |
| `Tv5725::VideoPath` | told what changed. Solves raster, clock, windows and scales, and writes them, holding what one solve leaves for the next. Decides nothing |

**The table is the shape the code now has**, and the list of steps below is what
it took to get there.

### `VideoPath` does not poll, it is told

`VideoSourceAcquisition` owns the tick, the escalation ladder, and the
orchestration of every part the acquisition touches -- the ADC, the sync
processor, the sync separator, Mode Detect, the input mux. `VideoPath` receives
EVENTS, and there are three:

| event | what moved |
|---|---|
| the input mux changed | a different source is on the ADC |
| the video source changed its mode | the raster the source is sending |
| the output resolution changed | including passthrough being selected |

Nothing else reaches it, and it asks for nothing on a tick. Remeasuring and
reconfiguring the chip is what it does in response to one of the three; deciding
that one happened is not its job.

**Today that is a four-call handshake rather than an event.**
`establishSyncType()`, `prepareToMeasure()`, `sourceMeasured()` and
`solveFromMeasurement()` are the acquisition layer driving one solve in stages,
which is the polling relationship the target removes.

### The measurement is the acquisition layer's, so the timings arrive known

**`VideoPath` no longer reads the source at all.** It read the chip in four
places -- the corrected line count for the scan mode, the hsync pulse twice for
the capture window, and the field rate in `resolve()` -- which gave the source
two readers with nothing reconciling them, and made a framing press pay for a
register read to re-derive what the engine had already computed. The order the
reads have to happen in is what the handshake now spells out:

```
establishSyncType()                    the path the counting happens on
prepareToMeasure(readSourceLines())    the count -> scan mode -> reference clock
measureSource()                        the rate, through that clock
sourceMeasured(readSource())           the pulse, against that clock
```

**`Tv5725::SourceReading` holds the sync DUTY, not the register.**
`STATUS_SYNC_PROC_HLOW_LEN` counts ADC samples, so its value means nothing
without the divider it was counted against -- and the divider moves on every
solve. Carrying the ratio is what lets one reading survive the solve it is
handed to. It also closes a disagreement nobody had noticed: `readRasters()` was
dividing that register by the SOLVED divider while `solveSampling()` divided the
same register by the reference one, so a single solve ran on two different
duties.

**The line count is not in the reading**, and the order above says why: the scan
mode is judged from the count, the reference clock follows the scan mode, and
the duty is counted against that clock. Two facts, two moments.

What remains is the rest of the handover: of the nineteen places `VideoPath`
reaches `SourceMeasurement`, the reads that are left are of values it was
handed or computed -- the line count, the field rate, the line rate, the
divider, the scan mode -- and those become what the event carries. Seven are
commands that drive the measurement, and they belong to the layer that owns the
tick. `applySampling()` stays: `PLLAD_MD`, `IF_HSYNC_RST` and `SP_RT_HS_SP` are
one quantity in three registers, and writing them is configuring the chip rather
than measuring it.

**The reason is the ladder, not tidiness.** An escalation ladder and the
measurement it escalates against cannot be optimised while they are read from
two places: which recovery has been tried, and what the source read when it was
tried, have to be one state before either can be reasoned about.

**One circularity has to be designed around.** The scan mode depends on how many
lines the OUTPUT can display, which is `VideoPath`'s, and the divider depends on
the scan mode -- so the acquisition layer has to ASK before it decides. A query,
the way `outputMode()` already is, not shared state.

**"The engine" is the new code, all of it.** The word names one axis and only
one: the classes under `src/` against the legacy sketch -- `rto` and the
globals. `VideoSourceAcquisition` is engine, and so is every `Tv5725::`
class; a move between them is internal and says nothing about the axis.

**Every step below moves a responsibility INTO the engine, and nothing ever
moves out.** So a step is described by what it takes over, never by what a class
"loses" -- writing that `VideoPath` lost something to `VideoSourceAcquisition` reads as
the engine shrinking, which is the opposite of what is happening.

**Nothing above `Tv5725::` touches the bus.** `VideoSourceAcquisition` coordinates the
measurement; it does not take it. `SourceMeasurement` stays one class and
changes owner rather than being divided, because its statics are already pure
chip reads and its instance is the steadiness run and the divider -- one
coherent job, in the wrong hands.

### The measurement escalates, and stops as soon as VideoPath can be called

One sequence, cheapest first, driven by what is still missing rather than by
which tier failed. **The recovery ladder is the bottom of this list, not a
second one**: both answer "there are no usable timings yet, what next", and
`SyncRecovery` is that list starting at position 4.

    the chip says the mode changed
      0  acquire the sync type            2-3 ms with its own V sync
      1  read Mode Detect                 one burst, the whole classification
      2  read HPERIOD_IF, VPERIOD_IF      the IF's own periods
      3  the test-port measurement        the slow one
      4  reconfigure                      coast, clamp, SOG level, sp dynamic
      5  reset                            sync processor, Mode Detect
      -> the timings are complete
      -> call VideoPath, which recalculates the input windows, then the
         active window, and the rest of the solve

**The sync type is above the measurements, not below them.** Every reading under
it is taken through the sync path, so a separate-sync source counted through
csync measures nothing usable -- `SP_SOG_MODE` 1 with `SP_VTOTAL` 97. It is
cheap enough to sit there, and the full probe window is spent only on a
genuinely composite source where the timeout is the answer.

**Input selection is not in this list.** It answers which connector carries a
source, which is a level up; folding it in makes every failed measurement a
candidate for moving the mux, and taking the input away turns the screen green
for as long as it is gone.

**IT BAILS THE MOMENT IT HOLDS WHAT `VideoPath` NEEDS.** The exit condition is a
complete timing set -- line count, field rate, line rate, scan mode -- not
reaching the end of the list. A tier that answers half of it leaves only the
other half to escalate for, which is the RGBHV case exactly: `HPERIOD_IF` is
exact there and `VPERIOD_IF` is debris, so the horizontal completes at the
second step and only the vertical goes on to the third.

**The trigger is the chip's, not a poll.** `STATUS_INT_INP_SW`, `s0_0F[3]`, is
"input source switch the mode", and `INT_ENABLE3` is already 1. Two things stand
in the way today: nothing reads it, and `loop()` calls
`Interrupts::acknowledgeAllButSogBad()` every 3 s, which writes `0xfe` to
`s0_58` -- bits 1 to 7, bit 3 among them. **The signal is destroyed on a timer
before any reader could consume it.** `STATUS_INT_SOG_SW` is what the sketch
uses instead, and it is a proxy: it reports the sync separator switching, not
the mode changing.

What each step can answer, measured:

| step | Wii 480i on `ypbpr` | RiscPC 320x256@50 on `vga` |
|---|---|---|
| Mode Detect, `s0_00..s0_05` | `SD`, `NTSC_INT`, `INT` -- standard, scan, and so the timings | **no mode bit set at all** |
| `HPERIOD_IF` | 428, exact for 15734 Hz | 431, exact for 15625 Hz |
| `VPERIOD_IF` | 524, `STATUS_IF_VT_BAD` 0 | 144, **`STATUS_IF_VT_BAD` 1 -- debris** |

`HPERIOD_IF` measures a period against the chip's 27 MHz rather than counting
lines, so interlace is invisible to it and it answers on every source.
`VPERIOD_IF` answers only where the IF completes a vertical measurement, which
RGBHV never does -- structural and reproducible,
`docs/investigations/vperiod-if-follows-the-sync-route.md`.

**The recognised modes are read first, the user slot second, and both arrive in
the same burst.** `IF_STATUS_` carries the whole table -- SD, VGA, SVGA, XGA,
SXGA, 720p, 1080i/p, 1250p -- alongside `STATUS_IF_INP_USER`, so the precedence
is an order over one read rather than a second lookup. A named mode wins,
because it carries a standard raster; the user slot answers only what no named
mode does.

**THE DETECT TABLE IS PROGRAMMABLE, AND ITS HORIZONTAL VALUES ARE `HPERIOD_IF`
UNITS.** `MD_*_CNTRL` are not datasheet constants, they are a lookup the
firmware writes at init, and the horizontal ones are `27e6 / (4 x lineRateHz) -
1` -- checked against what `ModeDetect::init()` writes, six of six within 1.0:

| register | written | computed |
|---|---|---|
| `MD_VGA_75HZ_CNTRL` | 178 | 179.0 |
| `MD_VGA_85HZ_CNTRL` | 154 | 155.0 |
| `MD_SVGA_60HZ_CNTRL` | 177 | 177.2 |
| `MD_SVGA_75HZ_CNTRL` | 142 | 143.0 |
| `MD_SVGA_85HZ_CNTRL` | 124 | 124.8 |
| `MD_XGA_60HZ_CNTRL` | 139 | 138.6 |

So a detect value is computable for any mode, and what Mode Detect recognises is
a choice rather than a fixed list.

**AND AN 8-BIT HORIZONTAL VALUE CANNOT REACH A 15 kHz SOURCE.** The ceiling of
255 puts the lowest expressible line rate at `27e6 / (4 x 256)` = 26.4 kHz. The
bench RISC PC at 15625 Hz needs 431, so it cannot be expressed horizontally at
all, `MD_USER_DEF_HCNTRL` included -- which is why the SD modes are defined by
VERTICAL detect values instead (`MD_NTSC_INT_CNTRL` 32, `MD_PAL_INT_CNTRL` 38,
`MD_NTSC_PRG_CNTRL` 65). Teaching Mode Detect a 15 kHz custom mode has to go
through `MD_USER_DEF_VCNTRL`, and the unit those vertical values are in is not
derived: the interlaced and progressive ones fit roughly field-lines/8 and
`MD_VGA_CNTRL` at 62 does not.

**THE USER SLOT IS WHAT STOPS A CUSTOM MODE COSTING THE SLOW PATH TWICE.**
`MD_USER_DEF_HCNTRL` and `MD_USER_DEF_VCNTRL` (`s1_81`, `s1_80`) are a
user-defined mode, and `STATUS_IF_INP_USER` is the bit that fires when they
match. Both read 255 -- the reset value, never programmed -- which is why that
bit never answers. Written from a completed measurement, a custom mode becomes a
recognised one: Mode Detect answers it at the first step from then on, and the
mode-change interrupt starts firing for it, because a switch detector keyed on
the classification has nothing to report while the classification is nothing.

**TIER 1'S SCAN ANSWER IS NOT TRUSTWORTHY AT 15 kHz, AND IT FAILS CONFIDENTLY.**
The bench RISC PC at 320x256@50 is PROGRESSIVE, and on composite sync Mode Detect
classifies it as PAL INTERLACED:

| | `s0_00..05` | bits set |
|---|---|---|
| Wii 480i, genuinely interlaced | `8f 00 00 00 40 00` | `SD`, `NTSC_INT`, `INT` |
| RISC PC 311-line progressive | `a7 00 00 00 40 10` | `SD`, `PAL_INT`, `INT`, `SW` |

311 lines per progressive field is what a vertical-period detector sees from a
312.5-line PAL field, so the two are ambiguous to it. **The generic
`STATUS_IF_INP_INT` bit repeats the error rather than correcting it**, which is
why extending `ModeDetect::sourceIsInterlaced()` to read it is not the
improvement it looks like: it would spread a wrong answer to every mode rather
than fixing one.

So the classification supplies the FAMILY and the rate, and **the scan type comes
from the half line in `VPERIOD_IF`**, which `SourceMeasurement::scanType()`
reads. The parity carrying that half line INVERTS with line doubling, because
doubling is what puts the count in half lines: a real interlace change moves the
doubled 320x256@50 from 623 odd to 624 even and the undoubled 640x480@60 from
524 even to 525 odd. A Wii reads 524 at 480i and at 480p, so the doubling is
what separates them and neither parity nor a table of totals does.
`docs/investigations/interlaced-source-measurement.md`.

On separate sync the same source sets no bit at all, so the misclassification
needs the separator in the path, exactly as `VPERIOD_IF` does.

**The steadiness run is the third step, and reaching it on a recognised source
is the fault.** `countHeld()` needs four consecutive identical counts, and an
interlaced source's field count alternates by construction: measured on the Wii
at 480i, `STATUS_SYNC_PROC_VTOTAL` takes exactly two values over 1417 samples,
260 and 259, near evenly. So `sourceIsPresent()` never goes true, the mode never
solves, and the picture rolls -- while Mode Detect has the answer in one burst
and `STATUS_IF_INP_NTSC_INT` reads 1 in 755 of 755.
`docs/investigations/interlaced-source-measurement.md`.

### What moves is what another class reads, and nothing else

**The test is whether a field has a reader outside the class that writes it**,
not whether `VideoPath` holds it. A field with two owners is the fault this
design exists to remove; a solver holding its own working state is not.

Applied to `VideoPath`, one field met it:

| | |
|---|---|
| `solvedLines_`, `solvedLineRateHz_` | **moved.** Written by `VideoPath`, read by it never, read by `VideoSourceAcquisition` at nine sites |
| the raster, both scales, the porch stops, the capturable region | **stays.** Outputs of one solve that only the next solve reads |
| `framing_`, `framedKey_`, `scanModeApplied_`, `syncTypeProbed_` | **stays.** No reader outside the class |
| `mode_`, `modePending_`, `solvePending_`, `usableHorizontal_`, `usableVertical_` | **stays.** Published through accessors; the class derives from them |

**Keep the state that describes the video output nearest the class that solves
it.** Gathering all of it into `VideoSourceAcquisition` does not remove state -- it
relocates it and adds a parameter, because a signature taking it as separate
arguments is unusable, so it becomes one struct another class mutates. That
leaves the data in one class and the behaviour that owns it in another, and it
makes the acquisition layer a fresh place to put things: the accretion `rto` is,
with a new destination. The acquisition layer has no use for a porch stop.

**The fault behind the rule is duplication, and it does not generalise.** Two
callers advancing one steadiness run leaves `idle_` double-advanced and the
run no longer over consecutive polls. That argues against two owners of one
fact, which is what `solvedLines_` was, and says nothing about a solver holding
what it derived.

**A move still costs something, so it is worth being the only one.** Bypass used
to zero the count directly as it was entered; the reader now forgets it when
`outputMode()` reads as bypass -- equivalent, because the guard runs before any
comparison, but a derivation where there was a direct write.

**The framing table is NOT `VideoSourceAcquisition`'s.** The user's pan and zoom, per
source, persisted to flash, is product state rather than acquisition -- and a
layer that takes it takes everything, which is the accretion this class exists
to avoid. **The root loads it, holds it and passes it down.**

**Done.** `sourceFramings` is a root global beside `sourceSampling` and the
display clock, passed to `VideoPath` by reference. It still reads and writes the
table -- that is what a press does -- but it no longer publishes it, so the load
and the save reach it directly rather than through `rememberFraming()` and
`framings()`, and the `const_cast` the save needed is gone with them.

**The revision is `FramingTable`'s, and it has to be.** It says a flash write is
owed, and BOTH the root and `VideoPath` change the table -- so a counter held by
either one cannot see the other's change. `remember()`, `forget()` and `clear()`
move it only when they change something, so a refused press costs no write.

**Not `RetroScaler` yet.** Holding one member is not a job, and a root class
created ahead of its owners is a fresh place to put things -- the accretion `rto`
is. It arrives when it also constructs `VideoSourceAcquisition`.

### The root is a class, and `rto` drains into it rather than becoming it

There is no composition root today -- "the root" is the sketch's globals -- which
is why `struct runTimeOptions` became the place state goes when it has nowhere
else. It needs to be a class, `RetroScaler`, holding what nobody else claims and
composing the rest: `VideoSourceAcquisition`, the framing table, the web server, the
OSD, audio, IR.

**`rto` IS NOT PROMOTED TO IT.** Forty-odd fields with at least five owners
between them, so a class built by renaming the struct is born holding four other
classes' state -- the accretion the section above exists to prevent, and a step
that adds an owner rather than moving one. It is drained instead, a group at a
time, as the step that claims each group lands:

| group | goes to |
|---|---|
| `notRecognizedCounter`, `sourceDisconnected`, `syncWatcherEnabled`, `isValidForScalingRGBHV`, `HdmiHoldDetection` | `VideoSourceAcquisition`. The counters it replaces -- `noSyncCounter`, `continousStableCounter`, `failRetryAttempts` -- are gone |
| `osr`, `presetID`, `applyPresetDoneStage` | the value handed to `VideoPath`. `videoStandardInput`, `presetDisplayClock`, `presetVlineShift` and `presetIsPalForce60` are gone |
| `phaseIsSet` | `Adc`, which holds both phases; `phaseSP` and `phaseADC` are gone |
| `deinterlaceAutoEnabled` | `Deinterlacer`, which holds the motion-adaptive state |
| `medResLineCount` | **gone.** `ModeDetect::init()` owns the threshold, at the 51 that was in force |
| `videoIsFrozen` | `FrameBuffer` |
| `autoBestHtotalEnabled`, `syncLockFailIgnore` | FrameSync, once it has an owner |
| `inputIsYpBpR` | `VideoSourceSelection` |
| `webServerEnabled`, `webServerStarted`, `allowUpdatesOTA`, `enableDebugPings`, `printInfos`, `freezeAutomation`, `boardHasPower`, `isInLowPowerMode`, `extClockGenDetected` | `RetroScaler` |

Only the last row is root configuration, and `boardHasPower` is in it under
protest -- it is a latched failure rather than a live reading, which *The rule
for every step* covers.

**The root is already improvising inside the struct**, which is the tell that it
is missing rather than optional:

    // The display clock ... lives here because both reach it;
    // Tv5725::VideoPath is handed a reference.
    Tv5725::DisplayClock displayClock;

That is composition, in a state bag, with a comment explaining why.

**Build it when it has a job, not before.** A root class created ahead of the
owners is a fresh place to put things, and the discipline erodes exactly as it
did in `rto`. Its first two jobs are holding the framing table -- which it
already persists and round-trips through the engine -- and constructing
`VideoSourceAcquisition` at step 7.

**`uopt` is not the same problem.** Persisted user options, coherent, with a
file format. It stays as it is.

### Stateless where it solves, stateful where it drives

Not every class flattens, and the line is what the class is for:

| | |
|---|---|
| **stateless** -- a solver called with everything it needs | `CaptureWindow`, `OutputRaster`, `Scale`, `RasterFit`, `BlankingTiming` |
| **stateful** -- a driver holding a level, a ramp or its own last answer | `DisplayClock`, which ramps the Si5351 over time; `SyncOnGreen`, which holds the separator level and deliberately separates the level CHOSEN from the level in force; `VideoPath`, which holds the raster, the scales and the framing one solve leaves for the next |

`VideoPath` sits in the second row because a solve reads what the one before it
produced. It calls the first row; it is not one of them.

### The divider needs the output, which is why it moves up rather than down

`solveScanMode()` decides line doubling from `rasterMode_->frameLines()` -- the
resolution the user picked, not the source:

    output choice -> raster frame lines -> line doubling -> divider -> capture window
                                                ^
                                          line count, measured

So the divider is not derivable from the source alone, and the party deciding it
has to know what the chosen output can show. **That argues for the move rather
than against it**: line doubling asks whether this source can be shown at this
output, which is the same question `bypassCanBeDisplayed()` asks and is policy,
not geometry. The present placement already carries the apology -- `solveScanMode()`
has to read `rasterMode_` BEFORE `solveRaster()` runs, because the held raster
would otherwise be the resolution being left. Move the decision and the apology
goes.

What it costs is one read-only fact published by `VideoPath`: how many source
lines the chosen output can show.

### poll() inverts, it does not vanish

A mode change needs the engine to act TWICE with a measurement in between,
because a count taken through the previous mode's divider is not the source's:

    event  ->  put the chip on a reference sampling clock
           ->  measure through it                            <- the handoff
           ->  solve everything from the reading

That is what `poll()`'s early returns already do, hidden. So it goes away as a
self-driving loop and its stages become named calls `VideoSourceAcquisition` makes in
order, with the sequence readable at the call site rather than inferred from
where the refusals land. `holdReferenceSampling()` and `writeSampling()` go with
it: `PLLAD_MD`, `IF_HSYNC_RST` and `SP_RT_HS_SP` are one quantity in three
registers that `SourceMeasurement` already owns, so the writes travel with the
class rather than staying behind.

**A refusal becomes a return value**, not a flag re-checked on the next tick.
`solvePending_` exists because the engine had a tick to retry on; the caller has
one instead.

**And the tick belongs to the caller.** `poll()` self-gates on its own
`DetectionIntervalMs`, so a layer with a cadence of its own puts two clocks in
the loop -- the ladder's and detection's. That is how `noSyncCounter` and
`idle_` came to count runs of different lengths. Whether the escalation wants
a slower cadence than detection is a bench question, and asking it at all
requires one owner of the tick.

## Why the classification went

gbs-control was written for retro consoles, where the source is one of a short
list of known standards, and `videoStandardInput` was that list. A machine that
programs arbitrary modes -- a RISC PC does, over a monitor definition -- does not
fit it, and the failures show up as a source filed under a standard whose branch
configures the chip for something else. The chip's own Mode Detect block is no
better: `MD_HD720P_CNTRL` and the rest are a fixed table of PC and broadcast
standards, and an arbitrary RISC OS raster matches none of them.

So the direction is not to classify better. It is to derive each thing the
engine needs from what it measures. **Fifteen values carry five unrelated
facts**, which is why one number reaching two subsystems means two owners:

| fact | concept it belongs to | replacement |
|---|---|---|
| which mode is on air | input, measured | `SourceKey` |
| line rate and frame rate | input, measured | `SourceMeasurement` |
| interlaced against progressive | input, measured | the engine's scan mode solve |
| colour space, YPbPr against RGB | input, selected | the input selection |
| scaling against bypass, 14 and 15 | output, chosen | `OutputChoice`, `OutputBypass` |
| an SD output resolution | output, chosen | `PresetPreference` |
| SD/HD input branching | neither | nothing -- one algorithm for every source |
| PAL against NTSC | output, chosen, optional | `matchPresetSource` rate matching |

**THE VALUES ARE NOT ALTERNATIVES TO EACH OTHER, WHICH IS THE FLAW UNDER ALL
OF THE ABOVE.** "RGBHV" names the signal arrangement on the plug -- R, G, B and
separate H and V. "Progressive SD" names a timing -- a line count, a field rate
and a scan mode. A source is one of each at once, not one or the other: the
bench RISC PC at 320x256@50 arrives over RGBHV *and* runs progressive at an SD
line rate, and a Wii at 480p is the same timing over component. So a byte where
3 means progressive SD and 14 means RGBHV is not a list of cases to pick from;
it is two orthogonal axes flattened onto one, and a load can only leave one
answer in it.

That is why no assignment to the byte is the right one, and why the fix is
never a better number. Three independent facts are involved and the firmware
already holds two of them properly:

| axis | what it answers | where it lives |
|---|---|---|
| the plug | which input is selected, and whether it is component | `VideoSourceSelection`, `ADC_INPUT_SEL`, `inputIsYpBpR` |
| the timing | line count, field rate, scan mode | `SourceMeasurement`, `SourceKey` |
| the route | scaled, HD bypass, RGBHV bypass | `OutputChoice`, `OutputMode` |

`inputIsYpBpR` is read off the ADC mux rather than off the byte and is already
right. `sourceIsRgbhv()` is the one that is not: it is spelled as a question
about the plug and derived from a byte that, once it holds 14 or 15, is
answering about the route.

**A video standard becomes two concepts that do not meet:** what the input IS,
measured (`SourceKey` -- line count and bucketed field rate -- with `SyncType`
and the scan mode solve carrying the rest), and what output was CHOSEN
(`OutputChoice`, `PresetPreference`, `OutputBypass`). Nothing derives the second
from the first. PAL against NTSC survives only as the option where the user asks
for the output frame rate to match the source's.

**`SourceKey` cannot separate two very different sources on its LINE COUNT.**
The sync processor counts FIELDS, so a 576i console reads 310 against the RISC
PC's progressive 311 at the same 50 Hz.

**INTERLACE IS MEASURED ON THIS BOARD.** `SourceMeasurement::scanType()` reads
the half line an interlaced field carries out of `VPERIOD_IF`, against the line
doubling the engine holds, and the motion-adaptive deinterlacer switches on and
off from it. The half line that separates the two is not too small to read; it
IS the reading, and it needs no table of broadcast totals.

What bounds it is the sync route rather than the raster: `VPERIOD_IF` is a
measurement only with the sync separator in the path, so on separate sync the
scan type has no source. That is what stops the key carrying interlace for every
source. `docs/investigations/vperiod-if-follows-the-sync-route.md`, `docs/bench-sources.md`.

**`sourceIsRgbhv()` is circular if defined over the output.** It also answers
*is this source RGBHV at all*, which detection establishes before any output is
chosen, and which gates the block setting `rto->isValidForScalingRGBHV` -- the
input to the flag the output half would read. Rebased that way the bench source
is classified as PAL SD within a minute, with a picture that still looks right.
So the input half needs a home BEFORE the byte can stop carrying 14.
`docs/investigations/the-rgbhv-question-is-two-questions.md`.

### What the classifier is asked, and what answers instead

| shape | sites | answers instead |
|---|---|---|
| `== 0` / `> 0`, is there a signal | ~11 | a VALIDATED measurement -- see the warning below |
| `== rto->videoStandardInput`, has it moved | 2 | `VideoPath::sourceMoved()`, which holds the solved count and rate |
| the held-standard fallback | was 3 | nothing: no caller names a standard to load |
| selects a preset | 3 | `SourceKey` and `OutputChoice` |
| a label to print | 5 | the measured pair |

On an RGBHV source it is not a classifier at all: that branch reads two
`STATUS_16` bits and returns the held byte back unchanged, or 0. A sync-present
test wearing a classifier's return type -- and when those bits go quiet under a
source the sync processor is still counting, the sketch walks a locked source
off its settings.
`docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`.

### A range check is not a signal-present test

`SourceMeasurement::countIsSource()` only asks whether a count falls in
200..1300, and **an unlocked sync processor produces garbage counts inside that
range**. Measured: gating the no-sync branch on it suppressed recovery for 80 s
while the source was genuinely unlocked and the counts read 216, 271, 276, 312,
305 -- every one in range, every one meaningless. Reverted. The narrower use in
`updateSpDynamic()` stands, because that gate withholds a sweep rather than the
whole recovery.

So each site needs its replacement chosen by what it gates:

- **withholding a sweep or a tweak** -- a live count is enough; being wrong
  costs one pass
- **withholding recovery** -- needs a count that is steady AND agrees with what
  the engine last solved against

`VideoSourceAcquisition::sourceIsPresent()` is that second answer, and it is the
gate both of the sync watcher's surviving branches read. The rule it follows is
the one already recorded for `sampleSteady()`: **publish the answer, do not
recompute it** -- `countHeld()` mutates `idle_`, so a second caller
double-advances the run the first depends on. That gives the sketch three states
where it has two:

| the engine sees | the sketch should |
|---|---|
| a steady count matching the last solve | never run recovery |
| a count no source runs, 97..137 | re-probe the sync type, which it now does |
| no count at all, 0 | run recovery |

### The threshold dither is not carried forward

`getVideoMode()` writes twelve Mode Detect threshold registers dithered by
`random(-2, 2)` around a static captured on its first call. The technique is
sound in principle -- a measured period sitting exactly on a threshold never
latches, so moving the threshold lets it fall clearly to one side -- and it is
deleted anyway, because a getter that writes twelve registers is a second owner
of them against `ModeDetect::init()`, and it leaves the threshold off-centre by
up to two wherever a mode does latch.

**Its regeneration argument covers composite and S-Video only.** Those decode
through the ADV7280 and re-encode through the ADV7391, so they arrive
standard-conformant and cannot sit on a threshold boundary. **YPbPr, RGBs and
RGsB are direct analog paths** -- `adv_sw` false -- so nothing reconstructs
their timings and a console can carry whatever it carries. What the deletion
costs is therefore testable rather than theoretical: a YPbPr source exercises
the branch directly.

## Why the watcher had to go rather than be tidied

`runSyncWatcher()` kept a **parallel model of the source** and called into the
engine once. Two owners of one model is the register problem one level up, and
every symptom this fork has chased is a case of it:

- `SP_H_PULSE_IGNOR` had two owners. `SyncProcessor::applyForSyncType(false)`
  writes 255 on every mode change and the sketch's search wrote 2 on its own
  schedule; the register reads 2 on a locked source and the screen is black.
  Both writes are `SyncProcessor`'s now, so the class arbitrates them -- what
  remains is the sketch deciding WHEN to hunt.
  `docs/investigations/the-sketch-hunts-while-the-engine-is-locked.md`
- The **ADC PLL group had owners on both paths**. `Adc::applySampleRate()`
  derives `PLLAD_KS` from the measured line rate, and the 900 ms check inside
  the RGBHV block wrote the whole triple from a band index of its own. That
  index is `Adc`'s now, and so is the scaling path's own charge pump. What is
  left outside the class is `setResetParameters()` and the HD bypass switch. A
  PLL left unlocked shows as a scrambled picture with every config register
  correct.
  `docs/investigations/the-no-sync-branch-is-the-only-escape.md`
- The steadiness of the source was counted twice, as `rto->noSyncCounter` and
  `rto->continousStableCounter` in the sketch and as `idle_` in the engine, and
  the two disagreed about whether a source was there.

So the fault was not that the code was old. It is that a second owner of a
register cannot be made correct by improving either owner.

## The shape

**The engine DEPENDED on the watcher**, which is why it could not simply be
deleted. Each row was a step: the engine takes the job over and the sketch's
copy goes.

| the engine needed | the sketch supplied |
|---|---|
| to be told a preset load happened | `applyPresets()` calls `modeChanged()` |
| the latched source disturbance | the watcher called `sourceInterrupted()` |
| a sync type probe | `useSyncTypeProbe(sourceHasOwnVsync)` |
| a source acquired well enough to measure | the SOG, coast and clamp routines |

**The tick is a parameter wherever it is owned.** A `millis()` reached for
inside a class is a hidden input the host tests cannot set, and every cadence
here is a test case. That is what lets the clock change owner at step 7 without
changing meaning.

**What acts outside `Tv5725::` is injected where it is an ACTION, and reported
where it is a DECISION.** Probing the sync type is a TV5725 operation that
happens to live in the sketch, so it arrives as a function pointer. Selecting an
input is not: `ADC_INPUT_SEL` is the TV5725's mux but `ASW_01`..`04` are the
HC32F460's, write-only over a UART, and `VideoSourceSelection` already lives outside
`Tv5725::` for that reason. An engine handed a `selectInput` callback is
deciding to move a mux on another chip -- the dependency disguised rather than
removed. It reports *not acquired, and out of what I can do alone*; what that
means is policy.

**`SyncRecovery` is outside `Tv5725::` by the same argument, and moved there at
step 7.** Ten of its eleven rungs are TV5725 operations, which is why it read as
marginal, but the eleventh changes the input and the list as a whole is policy
about the board. It sits in `src/videosource/` beside `VideoSourceAcquisition`.

## The ladder

Every distinct thing the watcher does becomes a method named for it, on the
class owning those registers. A step that cannot be stated as one of these rows
has not been understood yet. Most of the owners already exist, so this is mostly
a move rather than a design.

**Detection**, once per idle pass, off one measurement:

| operation | replaces |
|---|---|
| `sourceIsPresent()` | `getVideoMode() == 0`, `getStatus16SpHsStable()` |
| the steadiness run | `noSyncCounter`, `continousStableCounter`, `RGBHVNoSyncCounter` |
| `sourceMoved()` | the `newVideoModeCounter` debounce |
| the latched disturbance | `takeSourceDisturbed()`, already one claimant |

**Acquisition**, when detection says the source is not yet usable:

| operation | replaces |
|---|---|
| acquire the sync type, PER SOURCE MODE CHANGE | `sourceHasOwnVsync()`, and the two places that guess |
| acquire the sync separator level | `optimizeSogLevel()`, `fastSogAdjust()`, `tuneSogLevelPreemptively()` and every ratchet |
| acquire the coast window | `updateCoastPosition()`, minus its writes to the ADC PLL |
| acquire the clamp window | `updateClampPosition()` |
| acquire the sampling phase | `optimizePhaseSP()` |

**The sync type is acquired per source MODE change, not per input.** A source
can change its sync type without the mux moving -- a RISC PC sets it from CMOS.
Reacquisition costs 2-3 ms on a source with its own V sync, and the full window
only on a genuinely composite one where the timeout is the right answer.
`docs/sync-type-selection.md`.

**Maintenance**, while a source is acquired:

| steer the deinterlacer | the `VPERIOD_IF` motion-adaptive and scanline state machine |
| steer the HD bypass vsync window | `steerHdBypassVsyncWindow()`, already extracted |
| steer the ADC PLL | the band index and its `PLLAD_KS`/`FS`/`ICP` writes |

### The rungs, in the order they are tried

`SyncRecovery::stepAt()` answers which rung a pass is due, and
`VideoSourceAcquisition::runRecovery()` dispatches on the answer. Each step fires
once at its position; the list cycles at 451, because a source that has been
switched off and on again needs it to.

| position | step | extra condition | operation |
|---|---|---|---|
| 0, 1 | `None` | -- | the free first pass: one dropped measurement is not a source going away |
| 2 | `LiftSogFloor` | no mode change in flight, serrated sync | `SyncOnGreen::liftOffFloor()` |
| 8 | `CoastWindow` | -- | default coast window, widen if serrated, forget positions |
| 27 | `SyncProcessorDynamic` | -- | `updateSpDynamic(1)` |
| 32 | `ReleaseCapture` | `STATUS_SYNC_PROC_HSACT` 1 | `FrameBuffer::releaseCapture()` |
| 34 | `HoldClamp` | YPbPr, `Info_sate` 0 | hold clamp, forget positions |
| 38 | `NudgeModeDetect` | -- | `ModeDetect::nudge()` |
| 48 | `HsyncOverflowProtect` | csync | `SyncProcessor::toggleHsyncOverflowProtect()` |
| 150 | `FullReset` | -- | clear overflow protect, default coast and clamp, `updateSpDynamic(1)`, nudge Mode Detect, re-acquire the SOG level, reset the sync processor, reset Mode Detect |
| 151 | `ReprobeSyncType` | -- | `VideoPath::reacquireSyncType()`; parks at `0x07fe` if it finds V sync |
| 413 | `ToggleInput` | `detectionMayChangeInput()` | `Adc::selectOtherInput()`, kept only if it locks within 210 ms |
| 450 | `ReopenSogSeparator` | -- | `SyncOnGreen::reacquire()` with the separator reopened |

**NO STEP MAY SIT AT 63.** The board-power check rewrites `noSyncCounter` to it
when the check at 61 passes, and the next pass increments past, so 63 is never
a count a step is asked for.

Two positions were compounded and are now separate. `ReopenSogSeparator` was the
`% 450` argument handed to `SyncOnGreen::reacquire()` from inside the reset
block, which is an escalation hiding in another step's parameter.
`ReprobeSyncType` shared 150 with `FullReset`, so the probe now applies its
answer after the reset rather than before it.

**What the ordering does NOT settle is the gate.**
`sourceIsPresent()` lets the counter ADVANCE where it used to sit pinned at 150,
and a list tried once per cycle is what makes that survivable rather than what
makes it safe -- measured before the list, the gate ended at `SP_SOG_MODE` 1
against a held sync type of separate, `SP_VTOTAL` 97, and no way back.
`docs/investigations/the-gate-runs-a-ladder-that-is-not-safe-yet.md`.

### Six of the eleven are contained in a seventh

`FullReset` does all of this in one pass:

    setHsyncOverflowProtect(false)   undoes HsyncOverflowProtect
    applyDefaultCoastWindow()        CoastWindow
    applyDefaultClampWindow()
    updateSpDynamic(1)               SyncProcessorDynamic
    ModeDetect::nudge()              NudgeModeDetect
    SyncOnGreen::reacquire(...)      LiftSogFloor and ReopenSogSeparator
    SyncProcessor::reset()
    ModeDetect::reset()

So the ladder tries FRAGMENTS of one acquisition, one at a time, and eventually
tries all of it -- and the order the fragments come in is historical rather than
principled, because they are not alternatives to each other. Only four rungs are
a genuinely different act: `ReleaseCapture`, which is frame buffer state rather
than a measurement; `HoldClamp`, which is not the default clamp window;
`ReprobeSyncType`; and `ToggleInput`, which is an input event and leaves the list
entirely.

**The engine does not work in fragments.** A source event measures everything
from scratch, which is what makes it possible to say what the engine believes
and why. So the destination is about three states rather than eleven: acquire;
acquire with the blocks reset first; change the input. `SyncRecovery` preserves
the eleven deliberately, because a list reproducing today's positions is
reviewable against today's behaviour -- an intermediate, not the destination.

**The argument for keeping cheap early rungs is weaker than it looks.** The
branch runs only when the source is ALREADY unlocked, so there is no picture
being protected, which is the usual reason to prefer a nudge over a re-acquire.
What survives of it is the free first pass -- one dropped measurement, not a
strategy.

### `0x07fe` is a signal, not a park -- and it needs no replacement

The two sites that write `rto->noSyncCounter = 0x07fe` are commented as stopping
the escalation before it reaches the input toggle. **That is not what it does.**
A block further down reads the value:

    if (rto->noSyncCounter >= 0x07fe) {
        rto->noSyncCounter = 0;
        printf("No Signal Out\n");
        rto->HdmiHoldDetection = true;
    }

So the write is a MESSAGE: announce, set the flag, and **restart the run from
zero**. The ladder does not stop -- it begins again and reaches the input toggle
in another 413 passes. It has a second trigger nobody wrote, the counter
reaching 2046 by counting, about 41 s at the 20 ms tick. One value carries a
trigger reached two ways, a report, and a state change.

**DECIDED: "no signal" is a STATE, and the search never stops.** A unit with
nothing to show keeps looking for a displayable input for as long as it has
none, because the user may plug something in or switch the source on at any
moment. There is no terminus to design.

That dissolves the value rather than replacing it. Both of its triggers stop
being events:

| today | becomes |
|---|---|
| the ladder ran out | the cycle wraps, and the input policy gets its turn |
| a rung found something | `sourceState()` says acquired; there is nothing to promote |

**`HdmiHoldDetection` goes with it.** Traced: set true in that block alone,
cleared by `inputAndSyncDetect()` on finding a source, and read in exactly ONE
place -- the RGBHV limit-no-sync branch, where it suppresses
`setResetParameters()`, `prepareSyncProcessor()` and `SyncProcessor::reset()`.
Its meaning is *we have already given up, stop tearing the chip down again*, and
an ordered list tried once per cycle runs the destructive rung once by
construction. The name is also wrong: nothing about it concerns HDMI.

**And the cadence question evaporates.** It looked like a product judgement --
41 s today against a 451-pass cycle of about 9 s, so a naive move tells the
television four times sooner. It tells the television nothing: `"No Signal Out"`
is a console string, and no OSD, web UI or output path renders a no-signal state
at all. Whenever one is wanted it reads the state rather than catching an event.

**One thing is left to decide, and it is not mechanical:** whether rung 0's
early return survives. It makes the first failed pass free, so a single dropped
measurement costs nothing -- but against a run counted in detection passes
rather than 20 ms ticks, one pass is a different amount of time, and the
debounce may want to be the run's own.

## The order

**WHAT THE STEPS ARE FOR: the best picture with the least mechanism.** Not a
faithful refactor -- a faithful refactor of an accident preserves the accident.
Where two paths do the same job differently, the question is *why*, and "nobody
knows" means collapse them. An unexplained divergence is not a risk to preserve
carefully; it is the complexity these steps exist to remove, and preserving it
because removing it might change something is how it survived. Keep a difference
that has a reason and write the reason down; delete one that does not.
`CLAUDE.md`, *Conventions*.


**Three stages landed before this list**, and they are what made the rest
possible: the engine could not be given the watcher's job while the sketch still
owned the scan mode and the mode-change event.

### The engine owns the scan mode

`SourceMeasurement::lineDoublingFor()` reads the line count the engine already
measures; `VideoPath::solveScanMode()` holds it and writes the four registers.

Derived **before** `poll()`'s measurement gates. The input formatter's own
measurements are meaningful only once its scan mode matches the source, so a scan
mode left wrong makes the gates fail and one derived after them is never reached.
The sync processor counts the source directly and is indifferent to the scan mode.

### The engine owns the mode-change event

`VideoPath::sourceMoved()` remembers the line count the last solve ran against and
re-arms when a settled count differs, so a solve that completed against a
mid-transition count is corrected rather than left.

**It keeps its own steadiness count, and must.** Reusing
`sampling_.sampleSteady()` fills the run the measurement's own gates read, so a
count taken mid-transition is treated as a measurement on the first poll of the
next mode change.

### The chip's own interrupt triggers it

`sourceMoved()` reports `interrupt` as well as `count` and `rate`. The line count
stays the confirmation of what the mode changed to and when it settled.

### Retiring the byte is spread across the list, not a step of its own

**Entirely, everywhere.** There is no second role it keeps. Every reference that
feeds geometry is a classification standing where a measurement belongs, and
each becomes a derivation from something the engine measures, as the scan mode
did -- so the references come out with the branches that read them rather than
being unpicked one at a time.

Landed so far:

- the sync-processor sweep reads no classification at all: both of its
  negatives are measurements, and `SyncSearch::shouldSweepSyncProcessor()` is
  deleted with them
- the three copies of the held-standard fallback are one
  (`standardForPresetLoad()`)
- the Mode Detect threshold dither is deleted
- both of `updateSpDynamic()`'s hunt branches read one `searching` value, so
  neither fires on a source the sync processor is counting
- **the question those branches ask is the acquisition layer's**, published as
  `VideoSourceAcquisition::sourceIsSearching()`, and the clamp placer reads it
  too. `updateClampPosition()` was the last live-loop gate on the classifier,
  and on an RGBHV source that gate was the trap the classifier's own entry
  describes: `getVideoMode()` returns the held byte or 0 off two `STATUS_16`
  bits, so a source the sync processor is counting got no clamp window placed
  for as long as those bits stayed quiet
- `VideoSourceAcquisition::sourceIsPresent()` is the no-sync gate, wired
- no detection path and no live-loop gate reads the classifier. The OLED
  menu's three calls read nothing; the sync-processor, medium-resolution and
  YPbPr searches ask `SourceMeasurement::countIsSource()` instead; and the
  medium-resolution one no longer walks `MD_HD1250P_CNTRL` behind
  16 x `delay(30)` on every RGBHV pass
- `presetIdFor()`'s PAL bit is gone, so no classification shapes a register
  value or a preset id
- **every gate on whether a standard is HELD is gone with `standardIsHeld()`.**
  The window placers ask one question between them -- `sourceIsSearching()`,
  which is the placement's own rule that a live count is enough -- while the
  extra wait on a preset load, the sync-on-green tuning window, the settled arm
  of `updateSpDynamic()` and the clamp placement gate ask `sourceIsPresent()`.
  The auto-gain gate and `loop()`'s coast gate lost the term outright:
  `acquiredPasses()` above a threshold already says the run is unbroken
- **`updateSpDynamic()`'s two hunt branches are one.** What told them apart was
  whether the byte had ever named a standard, and the engine keeps no such fact;
  what is left is the caller's request for the hunt configuration, so
  `applyForSearch()` reaches a source that was never named -- the case it exists
  for. What that changes is measured harmless on both bench inputs, and the
  threshold question it raises is below

**The count was not the progress**, because what landed is structural. The
references left in two blocks, at steps 10 and 12: the byte was deleted LATE,
once the RGBHV block had moved and nothing read it, rather than being unpicked
reference by reference from inside a function that was going anyway.

**Steps 1 to 9 and 11 have landed, and with them every bounded ownership
move.** `SyncOnGreen` owns the separator level and its acquisition,
`SyncProcessor` the coast and clamp windows, `Adc` the sampling phase and the
ADC PLL band, `FrameBuffer` freeze and unfreeze, `Deinterlacer` the filtered
scan type and the motion-adaptive engage, and `VideoSourceAcquisition` the
escalation ladder, with `SyncSearch` beside it
under `src/videosource/`. What is left in front of them is
`optimizePhaseSP()`'s HD range, which is the byte's and goes at step 12.

**Step 4 has landed, gates included.** `rto->noSyncCounter` and
`rto->continousStableCounter` are gone; `VideoSourceAcquisition` counts both
halves of the run -- `acquiredPasses()` and `unmeasuredPasses()`, one of them
always zero -- and every reader reads those. The run advances on the detection
cadence rather than per poll, which is what keeps every threshold tuned against
a 20 ms pass meaning what it meant. `sourceIsPresent()` and
`sourceIsSearching()` are what the window placers, the tuning window and the
auto-gain, coast and clamp gates ask, so no gate compares a held standard.

**The watcher had no arm left by the time it went**, and its four parts are
gated on the engine's run rather than on what the source is called:

| part | whose |
|---|---|
| the interrupt hand-off, the sync-on-green tuning | nothing: every one of them reads the engine's run |
| the no-sync branch | `!sourceIsPresent()` is the gate, for every source |
| the stable branch | `sourceIsPresent()` is the gate; `SourceMaintenance` holds its cadence |
| the channel sync polarity and SOG-bad acknowledgement | `isHdBypassChannel()` is the gate |

**THE CADENCE IS A CLASS, AND THE TICK IS ONE TICK.** `SourceMaintenance` names
what a settled source is due -- the capture hold, the dynamic sync-processor
write, the separator level, the sampling phase, the window re-place, the SOG-bad
acknowledgement, the deinterlacer steer -- and performs none of it, the shape
`SyncRecovery` already uses one level up. What it replaces is fourteen literal
pass counts inside the stable branch, where the cadence could only be read by
collecting them.

And it runs **once per count**. `loop()` used to gate the watcher on a 20 ms
timer of its own beside the acquisition tick's, so the two drifted: a count could be
answered twice or not at all, which is why the long-absence restore was
reachable only through a window of one detection interval.
`VideoSourceAcquisition::runAdvanced()` says which pass advanced the run, and
that is the gate.

**THE SEARCH CONFIGURATION PICKS ONE OF THE THREE SYNC ARRANGEMENTS AND DOES
NOT ASK.** `SP_H_PULSE_IGNOR` follows what the source's sync carries -- 0xFF
where there is a V sync line of its own, 0x02 for composite sync without
serrations, 0x6B for serrated composite -- and `applyForSearch()` writes 0x02
whatever the sync type says. The hunt therefore looks for every source as though
it were unserrated composite.

**What that costs is measured only where the two values are
indistinguishable.** 0x02 written onto the locked 15 kHz separate-sync bench
source leaves `STATUS_SYNC_PROC_VTOTAL` at a steady 311 in 10 of 10 samples over
9 s, with `HSACT` 1 throughout and 0xFF restoring identically -- which
RD-5725-1.1's wording explains, the counter starting "when sync large different"
and so applying where the separator tells pulse widths apart inside one
composite stream. The serrated case is where the table says it bites: 0x02 on
the Wii's 576i counts 315/316 against the 310 the source runs, four to six lines
high and perfectly steady, which no steadiness run can see.

**There are three arrangements, not four**, because serration is a property of
composite sync: a dedicated H line keeps sending line syncs through the vertical
interval, so nothing needs chopping and no widths need resolving. The firmware
already reads `serrated` only under `csync`, so what the search has to establish
is two answers rather than three.

**The resolution is the derivation, not a fourth constant.** The value was once
computed from two measurements -- `HPERIOD_IF` for the line and
`STATUS_SYNC_PROC_HLOW_LEN` against `HTOTAL` for the sync duty -- and that
computation is what produced the 0x6B the Wii needs; the per-standard fixed
bytes that replaced it are guesses. So the search wants the same three-way
answer a settled source gets: the sync type from `SyncMeasurement::probe()`,
which is 2-3 ms, and the serration from the duty rather than from
`sourceLowLineRate()`, which infers it from the line being slow.
`docs/investigations/the-pulse-ignore-value-is-measured-not-chosen.md`.

**THE HD BYPASS VSYNC STEER IS DELETED, BECAUSE ITS TWO GATES ARE MUTUALLY
EXCLUSIVE.** It placed the sync separator's regenerated vsync from the source's
own line count -- bypass solves no raster to take it from -- and required
`isHdBypassChannel()` and `sourceLowLineRate()` together. Every route onto the
channel is gated on `rateCanBypass()`, **26000 Hz**: the engine's own
`passThroughSuitsSource()`, and both manual entries through
`bypassCanBeDisplayed()`. `lowLineRate()` is **below 20000 Hz**. Six kilohertz
apart, so no source reaching the channel can satisfy it.

The floors were not always apart. `BypassMinLineRateHz` is measured, bracketed
between 21.8 kHz giving no signal and 26.6 kHz locking, and before it existed a
15 kHz source could reach the channel and did need the steer. Nothing went back
for it when the floor closed that off. **The disjointness is asserted in
`test_source_measurement.cpp`**, because bringing the floors together would
revive the shape silently.

**And a slow source could not be carried anyway.** CEA-861's 240p and 288p modes
exist only pixel-repeated, to clear HDMI's TMDS clock floor; bypass hands the
encoder the source's analog timing with nothing in the path to repeat pixels,
and no way to ask for it -- the MS9288A configures itself from mask ROM with its
I2C pins unconnected. `docs/rgbhv-bypass-trap.md`.

**One further act did not survive the move**, and it is recorded because a reader
will look for it: `ADC_UNUSED_67::write(0)` at forty-five passes. It is s5 0x67, 16
bits undescribed in RD-5725-1.1, written in three places and read in none --
unlike `ADC_UNUSED_69`, which `checkBoardPower()` round trips. The other two
writers clear 0x64..0x67 together; this one cleared 0x67 alone.

**THE ARM IS RETIRED, AND NEITHER OF ITS ACTS WAS RGBHV'S.**

`updateSpDynamic(1)` at six settled passes could not reach the branch the `1`
selects: that branch requires `searching`, which requires `!sourceIsPresent()`,
and the call site required `sourceIsPresent()`. It was therefore the same act as
the `updateSpDynamic(0)` the stable branch already runs, and it joins that
branch's cadence.

The 900 ms pair moved out whole. **The acknowledgement is the polarity step's
freshener** -- `STATUS_INT_SOG_BAD` latches, so it reports NOW only for a reader
that clears it, and both readers here want that: the polarity gate beside it and
the auto-gain gate in `loop()`. Neither is RGBHV's, and on YPbPr, where the
sync-on-green tuning is skipped, nothing else clears the bit repeatedly at all.

**The widening is measurable on the Wii.** The arm kept the polarity step off a
YPbPr source, so the channel's hsync pair stayed as `applyComponent()` wrote it.
It is now ordered from the measured polarity like every other channel source --
`HD_HS_ST` 164 / `HD_HS_SP` 40 with `SP_HS2PLL_INV_REG` 1 against a negative
`STATUS_SYNC_PROC_HSPOL`, steady in 12 of 12 samples over 30 s, picture clean and
full screen. There is no host seam for the sketch's watcher, so the guards below
are what this rests on.

**The polarity act is placed.** `HdBypass::applyChannelSyncEdges()` owns it, and
`updateHVSyncEdge()` is deleted. What it was doing is the engine reading a
register as an input: it compared `HD_HS_ST` against `HD_HS_SP` and swapped
them, on registers `HdBypass` writes itself, when two registers cannot say which
of the values in them is the start. Every writer of either pulse now records the
pair as well as writing it, so the ordering works for the arms' pulses and the
computed path's alike.

Its gate is `VideoRoute::isHdBypassChannel()` rather than `rgbhvBypass()` --
these are the CHANNEL's emitted pulses, so whether the channel is in circuit is
the question. And the vertical half needs no sync-type gate, because
`STATUS_SYNC_PROC_VSACT` reads 0 on the composite-sync path and so already
answers what `isCsync()` was standing in for there.

**The new-mode branch is gone.** It classified every pass, compared the answer
against the held byte, counted up, re-read the classifier thirty times to
confirm, and called `applyPresets()` -- all of it a second detector beside
`VideoPath::sourceMoved()`. Both surviving branches gate on the engine's run,
so **no part of this function reads the classifier**.

**Step 10's arm is gone, so step 13 is unblocked.** Steps 9 and 10 are
part-landed in that `Deinterlacer`, `OutputChoice` and `RgbhvOutput` all exist
and the sketch still steers them.

**THE DIVIDER CLOBBER THAT BLOCKED THIS IS FIXED.**
`VideoPath::prepareToMeasure()` used to apply the reference sampling clock
unconditionally -- right when the sampling in force cannot measure the source,
wrong when it can. On the pass-through route the chip samples at the CHANNEL's
divider, `HdBypass::dividerFor()` capped at 2039, and nothing put that back
after a measurement: 800x600 came back at `PLLAD_MD` 1124 against
`HD_HSYNC_RST` 2047, left third of the panel black. It now returns before the
reference when the output is passed through, and `configurePassThrough()` holds
the channel's divider so the engine and the chip agree. Measured across a
1024x768 change taken while passed through, the divider holds at 2039 and the
picture is full screen.
`investigations/the-reference-clock-is-applied-to-a-working-picture.md`.

**AND A COUNT ALTERNATING BY ONE IS NO LONGER A MODE CHANGE.** `SteadyRun`
treats a pair differing by one as agreeing -- an interlaced field carries a half
line -- and `countMoved` compared raw reads instead, so a source counting
308/309 armed a mode change, a probe and a re-solve on half its polls, for ever.
`SteadyRun::agree()` is that rule made public and both callers use it.

**AND THE LADDERS HAVE MERGED.** Both `!rgbhvBypass()` gates are gone and so is
`RGBHVNoSyncCounter`, so `SyncRecovery`'s eleven rungs are the only recovery
there is. Measured with the Wii unplugged, which is what the reproduction needs:
before the change an empty `rgbs` ran `RGBHV limit no sync` every ~33 s and
**the rungs never ran at all**; after it the ladder walks the separator
(`SP_SOG_MODE` 1, `ADC_SOGCTRL` 4) and `vga` recovers to acquired in under ten
seconds. A passed-through 800x600 held `PLLAD_MD` 2039 against `HD_HSYNC_RST`
2047 for 90 s with the ladder able to reach it, which is the case the exclusion
protected.

Each step below extracts one named operation, merges it into the idle pass, and
deletes the sketch's copy in the same commit.

**1. One owner for the sync separator level.** `Tv5725::SyncOnGreen` holds the level and
owns `ADC_SOGCTRL`; `rto->currentLevelSOG` and `setAndUpdateSogLevel()` go. No
policy moves. It separates the two facts that variable carried — the level
*chosen* for a source the ADC has not been brought up for, and the level *in
force* — which is why a straight substitution would have been wrong.

**2. The detection cadence.** *(Landed, and step 7 takes the clock off it again
-- the cadence has to be a parameter before it can change owner.)* `poll()`
takes the clock and the idle detection
pass runs on `VideoSourceAcquisition::DetectionIntervalMs`. The steadiness run
is counted in detection passes and `loop()` goes round far faster than the sync watcher's
20 ms tick, so without this a run counted per pass is not the same length as one
counted per tick and every threshold keyed on it means something different.
Nothing else can read the engine's run until it counts in the units the sketch's
counters did.

**3. Acquire the sync separator level.** *(Landed.)* `fastSogAdjust()` and
`tuneSogLevelPreemptively()` are gone and `SyncOnGreen` carries `acquire()`,
`acquireCoarse()`, `liftOffFloor()`, `reacquire()` and `tune()`.
`optimizeSogLevel()` stays in the sketch deliberately -- it is the walk that is
handed IN, and what it knows that the class must not is `rgbhvBypass()`.

**It must ask whether sync on green IS the sync source, and two of the four do
not.** The separator only reaches the sync processor with `SP_SOG_MODE` 1, which
follows the sync type, so on a separate-sync source the level is inert -- and
`fastSogAdjust()` and `FullReset` walk it anyway. `SyncOnGreen::inSyncPath()`
is that question, asked of held state (`SyncMeasurement::isCsync()`) rather than read
back.

**THE OWNERSHIP LANDS HERE; THE CALL SITES TRAVEL WITH STEP 4.** The pre-emptive
pass needs a gate the engine does not have yet -- in the sketch it runs behind
`sourceDisconnected` and `syncWatcherEnabled`. Moved without one it runs
acquisition throughout a recovery, walking the level down on a source that
cannot answer: failed twice, once from an unconditional walk and once from a
gate keyed on `rto->boardHasPower`. And the gate is not a formality, because the
pass is PRE-EMPTIVE -- it earns its keep exactly as sync degrades, which is when
`sourceIsPresent()` goes false. Whether it should run while the run is broken is
step 4's question.

This step must still land before the gate: the recovery is the only thing that
leaves the black state a round trip can produce, so a gate in front of it is a
gate in front of the only exit.
`docs/investigations/the-no-sync-branch-is-the-only-escape.md`

**AND THE WALK IS HANDED IN, NOT CALLED.** The handover runs
`optimizeSogLevel()`, which REFUSES to walk under `rgbhvBypass()`. Calling
`acquire()` from inside the class looks equivalent and ratchets the level to its
floor and pins it there, which no host test catches and no ESP restart recovers.
`docs/investigations/refusing-to-walk-is-part-of-the-walk.md`

**What it leaves behind:** `tuneSogLevelPreemptively()` also calls
`updateSpDynamic()`, stamps `lastVsyncLock` and clears `rto->phaseIsSet`. Those
three are claimed at steps 5, 11 and 6 -- the intermediate *The rule for every
step* describes, not a decision that they belong to the sketch.

**THE INTERRUPT RE-ARMS ARE A RETRY LOOP, NOT WASTE.** One source mode change
produces three solves and three sync-type probes, and removing the repeats looks
like free speed. It is not: the latched disturbance is what re-arms
`establishSyncType()`, and the repeats are what let the probe converge. Consume
the latch on a completed solve and the unit sits on the csync path indefinitely
-- measured at 74 s with `SP_VTOTAL` 97. **The waste is after convergence**, so
the check is whether anything differs before re-solving, not whether a solve has
happened since the latch was set. No host test reaches this: one pinning an
interrupt after a solve passes either way, and a 36-change mode soak passes too
because every change holds the sync type constant.

**4. One steadiness run, WHICH IS THE NO-SYNC GATE.** `noSyncCounter`,
`continousStableCounter` and `RGBHVNoSyncCounter` become reads of the engine's
own run, and `VideoSourceAcquisition::sourceIsPresent()` replaces the classification at
the
gate. The gate itself is right -- measured, the engine calls the bench source
present while `getVideoMode()` calls it absent, and the engine is correct.

**STEP 7 HAS TO COME FIRST**, for the reason under *the positions are not an
order* above: wiring the gate lets the branch advance into rungs that never ran.

**AND STEPS 3, 4 AND 7 ARE ONE CHANGE.** The run is over the same measurement
the mode-change check takes, so a `noSyncCounter` that reads it never advances
on a source the engine calls present -- the ladder is withheld exactly as wiring
the gate withholds it. Splitting them buys nothing, because the sketch's counter
would still be gated on the engine's answer, which is the whole risk.

The ladder's moduli are NOT the run. `noSyncCounter` also carries an escalation
position and a control latch the sketch writes -- `0x07fe`, `0x05ff`, `63`, `1`
-- and those stay a local index until step 7 replaces them. What moves here is
the run.

**5. Acquire the coast and clamp windows**, to `SyncProcessor`. *(Landed.)*
`SyncProcessor::acquireCoastWindow()`, `acquireClampWindow()` and
`adoptClampPlacement()` own them, and the ADC PLL writes were deleted rather
than moved because `Adc` owns that group. What is left in the sketch is an `if`
whose body is empty: the gating asks the acquisition layer, `rgbhvBypass()`
excepted, and that term is the route rather than a classification.

**6. Acquire the sampling phase**, to `Adc`. *(Landed.)* `Adc` holds both
phases, refuses one past the five-bit field, and runs the search.
`rto->phaseSP`/`phaseADC` are gone, and with them the site that took `PA_ADC_S`
back into the held value -- `Adc` is the only writer of either phase register,
so that read could only ever return what it had written.

**The search is handed its readings**, and the two things that looked like
blockers had one answer. `lineSamples` is the sync processor's count, another
block's register; `feedWatchdog` is the platform's, and `test/fake/Arduino.h` is
deliberately not a general shim. Both arrive as function pointers, the way
`SyncOnGreen::acquire()` already takes `nowMs` and the walk -- and that is what
makes the search host-testable at all, because a stub answering from the phase
actually latched answers the SEARCH rather than a script of it.

**A failed search leaves the phase where the WALK stopped.** The sweep latches
each phase to score it, so a refusal still leaves the last one tried, two steps
on from where it started. Preserved rather than repaired, and the test says so:
a failed search leaving the phase somewhere nobody chose wants a bench reading
behind the fix, not a move.

What the sketch keeps is not leftover: the divider-latched gate stays in front
of the call, so `Adc` needs no `SourceMeasurement`. The half-sample nudge for
`videoStandardInput >= 5 && <= 7 && osr == 2` is deleted -- one rule at every
exit of the search, because the ADC's phase is not what the sweep scores.

**7. `VideoSourceAcquisition`, and the escalation list it holds.** The ordered set of
named recoveries has replaced `% 27`, `% 32`, `== 38`, `% 150` and `% 413`, and
the list lives above `Tv5725::` with the class that holds the tick. **Step 4
waited on this**, because wiring the gate is what lets the branch advance far
enough to reach these.

**THE BENCH CANNOT REACH THE LADDER ABOVE POSITION 8**, so the upper rungs are
covered by host tests for which step fires and by inspection for what it does.
Measured on an `/input?src=rgbs` excursion with nothing attached: the counter
runs 0..14 and is reset continuously by the disconnected-source path, never
approaching 27.

**This is the step that inverts the call.**
`VideoSourceAcquisition::poll()` takes the tick, drives `SourceMeasurement`, and runs
a rung when the source is not acquired. `VideoPath::poll()` does not move -- its
stages become named calls made in order.

**What state travels with it is decided one field at a time**, by the test in
*What moves is what another class reads*. `solvedLines_` and
`solvedLineRateHz_` did; nothing else in `VideoPath` does, so the rest stays
with the solver that derives it.

**It is the largest step on the list and it does not have to land at once.**
The order inside it is: create the class with the tick and the ladder; move
`SourceMeasurement` to it; then the flags that make `poll()` disappear. Each is
a solve that still writes the same registers, so the bar below applies to every
one of them rather than only to the last.

Landed of it so far: the class, with `loop()` calling it and it calling
`VideoPath`; the ladder as an ordered list the sketch dispatches on; the count
and line rate the last solve ran against; the detection clock and the cadence; the run gate, which follows the
tick; the three publishers of what the source is running; and the idle pass --
`sourceMoved()`, `rateMoved()`, `countHeld()`, the run they advance and the
`SourceState` they publish. `SourceMeasurement` is held by the root and passed to
both, as the display clock already is: this class is constructed after
`VideoPath`, so it cannot yet own a collaborator `VideoPath` needs at
construction.

**The idle pass is what split `poll()`**, at its top-level branch. The layer asks
whether the source moved and arms the change itself, so what is left below is the
solving half alone -- and `VideoPath::poll()` takes no clock-shaped argument at
all any more. The reference-sampling handoff sits inside that remaining half and
is next.

**`poll()` answers with an outcome, because the caller holds the run.** The run
is seeded by a mode change that completed and broken by a solving pass that could
not measure, and one `false` cannot tell those from each other or from a pass
with nothing to do. A deferred retry is a fourth answer: it re-reads no count, so
there is no run to seed from it.

**THE STEADINESS RUN HAS ONE ADVANCER.** The moment two callers advance it,
`idle_` double-advances and the run both readers depend on is no longer over
consecutive polls -- which is the fault `sourceIsPresent()` was written around.
That is a rule about one field with two owners, and it is why `sampling_` is
held by the root and passed to both rather than copied.

**The framing table is the one piece that does not wait**, because it moves to
the root rather than to this class: the root already persists it and round-trips
it through the engine, so holding it is an ownership move with no dependency on
the ladder, provable by host tests and a pad press over HTTP.

**Every rung names an operation, and no rung writes a register itself.**
The one read still taken raw is `STATUS_SYNC_PROC_HSACT` in front of the
unfreeze, which travels with step 8.

**`Adc::bounceInput()` is NOT `ToggleInput`**, and the two must not be merged.
The bounce takes the input away and puts the SAME one back, to clear a railed
`HPERIOD_IF`; the step moves to the OTHER input and keeps it if the source locks
there. Nothing calls the bounce, and wiring it as an automatic recovery puts a
green screen -- for as long as the input is away -- on every solve that reaches
that position.

**`FullReset` is the compound one and it is where the harm was.** Its sync-type
correction used to be one-directional: it could move a held csync to separate
and never back, and it never reconciled the register with the held value, so a
source counted through the wrong path with the held type already right had no
route out. `VideoPath::reacquireSyncType()` is that rung — it applies the probe's
answer to the chip whatever the held value says — and it also stops the recovery
probing an input whose connector settles the sync type.

**THE OLED MENU KEEPS ITS OWN COPIES OF TWO OF THESE.**
`OLEDMenuImplementation.cpp` has private `resetSyncProcessor()`,
`resetModeDetect()` and `resetSyncProcessor_yuv()` pulsing the same
`SFTRST_*_RSTZ` bits, the last two of them never called. They are a second
writer and they go with step 10, where the menu stops writing registers at all.
The live one bundles `LoadDefault()`, so it is not the same operation and cannot
be substituted blind.

**A rung that reads a register to judge a measurement needs a bus that can
move.** `SyncOnGreen::reacquire()` decides between walking the level and parking
it on whether `STATUS_SYNC_PROC_HLOW_LEN` changes across a run of reads, and a
fake register holding one value can only reach the frozen branch. `FakeTwoWire`
has `drift()` for that, and any rung judged on movement rather than on a value
will want it.

**8. Freeze and unfreeze**, to `FrameBuffer`, which owns capture already.

**9. Steer the deinterlacer**, to `Deinterlacer`. *(Landed.)*
`Deinterlacer::steer()` holds the filtered scan type, the motion-adaptive engage
and release, the scanlines and the re-lock in flight. The two acts outside
`Tv5725::` -- FrameSync and the clock generator -- are REPORTED, the shape
`SyncOnGreen::tune()` already uses, rather than injected.

**The delayed re-lock's `enableFrameTimeLock` gate is gone**, because
`FrameSync::reset()` with no standing correction only clears `syncLockReady` and
`delayLock`, so the gate withheld nothing that the other re-lock path did not
already do unguarded. One report, one response.

**10. The RGBHV block**, to `PresetLoad` and `OutputChoice`, with the preset load
becoming an injected action. The largest single piece, and the one that carried
most of the standard byte.

**THE RASTER A SOURCE IS RUNNING HAS TO BE ESTABLISHED BY THE MEASUREMENT, NOT
BY THE SOLVE.** `HdBypass`'s arms each carried a vertical blanking constant per
standard, and 720x480p's was `0x40` where the raster puts active video at line
36 -- 28 lines of picture off the top. The replacement is `SourceTiming`, which
is already keyed on three MEASURED values and states where active video starts.

**It reaches the caller.** The match is resolved in `VideoPath::sourceMeasured()`,
before the layer decides the route, and `CaptureWindow` is handed the answer --
it used to be built inside `CaptureWindow::readRasters()`, which only the
SCALING solve runs, so a source handed to pass-through arrived with the line
still zero. Measured on the Wii at 480p, `HD_VB_SP` reads the 36 the published
raster puts active video at.

A source matching no published raster leaves the window alone rather than
writing zero: blanking nothing could not be distinguished on hardware from the
source's own screen dimming, so it is not a safe default for the channel.

**Its two entries are one call now.** Leaving bypass and crossing into another
preset's bucket ran thirty byte-identical lines each, so every register in that
sequence had two writers. The sync processor's share is
`SyncProcessor::applyForScalingRgbhv()` and the ADC's is
`Adc::applyScalingChargePump()`. **Neither has a caller**: both were extracted
from `loadScalingRgbhvPreset()`, which went with the preset tables, so what they
do happens nowhere -- and whether each was a put-back that dies with the tables
or a behaviour to restore is undecided.

Three of the four are placed. The option bit is `loadComputedPreset()`'s. **The
external clock generator is placed**: the `!scalingRgbhv()` exclusion in
`loop()`'s handoff stood in front of nobody once `loadScalingRgbhvPreset()` was
deleted, and is gone. **The line counter's start needs nothing placed**: the
scaling RGBHV arm used to write `IF_INI_ST` 16 over the load's 0, and the two are
indistinguishable -- measured A/B/A/B on the bench source at the default framing,
the picture's left edge sits at photo column 93, 93, 93, 92, which is the
camera's own spread. So the load's one value for every source stands and the 16
is not a behaviour that went missing.

The standard byte's round trip through `applyPresets()` went with step 12.

**AND THE ARMS ARE ALL GONE.** `HdBypass::applyForSource()` is the whole of the
entry: the sampling group from the divider and rate the engine holds, the SD
vertical sync position, and the channel's vertical blanking from the raster the
measurement matched.

`applyHd()` froze a divider, a crossover row, a clock tap, a decimator pair, a
raster and both sync windows per published mode -- the computed path's job with
constants substituted for the derivation. The constants could not be right:
`applyHd(5)` wrote `PLLAD_MD` 2474 where `dividerFor()` caps at
`MaxChannelLine - RasterGuardSamples` = 2039 and the channel counter caps
`HD_HSYNC_RST` at 2047, so the line was always past what the channel can play
out. `investigations/the-bypass-divider-is-capped-by-the-channel-counter.md`.

**`applySd()` COULD NOT BE ENTERED.** Every route onto the channel is gated on
`rateCanBypass()` at 26000 Hz and interlaced SD runs 15.6 kHz -- the same
disjointness `test_source_measurement.cpp` asserts for the retired vsync steer.
It also narrowed the analog corner to 40 MHz and inverted three sync polarities
and Mode Detect's two, with nothing putting them back for the source after it.

**`applyProgressive()` DIFFERED IN TWO PAIRS.** The channel's own vsync pulse
was 6/0 against the computed path's 2/7 -- both five or six lines within ten of
the frame's start, which is the argument that retired the HD arms' pulses -- and
it named `SP_SDCS_VSST`/`VSSP` per standard, 520/522 for 480p and 48/46 for
576p, where 48 lands in active video rather than the vertical interval.

**`applyComponent()` WAS SECOND OWNERS THROUGHOUT.** It wrote `SP_PRE_COAST` and
`SP_POST_COAST` 4/4 over what `applyForSyncType()` had established a few lines
earlier, and `SP_DLT_REG` 0x70 over `applyPulseWidthDifference()`'s. It held the
sync type csync AFTER the sync processor had been configured from the held
answer, and it called the RGB patches on a COMPONENT source, undoing the YUV
patches the bypass entry had just applied. Its sampling was `PLLAD_MD` 512 with
the crossover row picked off the line count, in place of the derivation.

**The HD arms went by inspection, the rest by measurement.** The bench has no
720p, 1080i or 1080p component source -- the Wii tops out at 480p -- so those
three are unreachable here and the argument is the arithmetic. The progressive
arm was A/B'd on the Wii at 480p in pass-through: every other register identical
either side, `HD_VS_ST`/`SP` 6/0 to 2/7 and `SP_SDCS_VSST`/`VSSP` 520/522 to
14/11, picture clean and full screen both ways.

Two things the arms wrote that the computed path did not are replaced rather
than dropped. `HD_VS_ST`/`HD_VS_SP` is one pair of constants beside
`HD_HS_ST`/`HD_HS_SP`, which is where the horizontal equivalent already lived;
every arm placed a pulse of five or six lines within ten of the frame's start,
so there was no raster property behind the differences. And the SD vertical sync
window, which nothing on this route writes otherwise, is
`SyncProcessor::applySdVsyncPosition()` -- one value for every source, as the
scaling path has. Without it a source reaching the computed path inherited
whatever the last entry left, the defect `enterHdBypass()` already puts the four
sync polarities back for.

13 keeps its own arm. It runs the component patches, the sync-type hold, the
coast pair and `SP_DLT_REG` as well as an ADC group, and the crossover row it
picks off the source's line count folds into it.

**Both live arms of the dispatch now have a bench guard**, so a change to
`applyPassThroughSampling()` has something to reproduce:

| | the computed path | `applyProgressive(3)` |
|---|---|---|
| source | RiscPC 1024x768@60 on `vga` | Wii 480p on `ypbpr` |
| `OUT_SYNC_SEL` | 1 | 1 |
| `PLLAD_MD` / `HTOTAL` | 2039 / 2039 | 2039 / 2039 |
| `HD_HSYNC_RST` | 2047 | 2047 |
| `HD_HB_ST` / `HD_HB_SP` | 2039 / 144 | 2039 / 144 |
| `HD_HS_ST` / `HD_HS_SP` | 40 / 164 | **164 / 40**, `SP_HS2PLL_INV_REG` 1 |
| `HD_VS_ST` / `HD_VS_SP` | 2 / 7 | 6 / 0 |
| `SP_SDCS_VSST` / `VSSP` | 14 / 11 | 520 / 522 |
| `HD_VB_SP` | 20 | 20 |
| `STATUS_SYNC_PROC_VTOTAL` | 805 | 524 |
| `HPERIOD_IF` | 255, and the IF is out of the path | 214 |
| picture | clean, full screen | clean, full screen |

2039 is the channel bound and 2047 is `2039 + RasterGuardSamples`. The two arms
differ only in the vertical pair and the SD window, which is what is left of the
progressive arm.

**The Wii's hsync pair is ordered rather than as written, and the polarity is
why.** `STATUS_SYNC_PROC_HSPOL` reads negative there, so
`applyChannelSyncEdges()` hands the channel the held pair the other way round and
raises `SP_HS2PLL_INV_REG`. It reads as a difference between the arms and is not
one: the computed path's source has a positive hsync, and the same step runs on
both. While the watcher's scaling-RGBHV arm existed the step never reached a
YPbPr source at all, so this column used to read 40 / 164.

**`HD_VB_SP` read 20 on the Wii, not the 36 above.** 20 is `enable()`'s resting
value, which is what `applyVerticalBlanking(0)` leaves -- so the active start
line handed in was zero on that entry, taken by changing input from a scaled
`vga` rather than by a source mode change. Which entries resolve the raster
match in time is open.

**AND PASS-THROUGH HAS ONE OWNER.** `VideoSourceAcquisition::passSourceThrough()`
is the only caller that decides it, because it is the only one holding a
measurement to decide it on. Three others used to: `doPostPresetLoadSteps()`
armed a deferred switch whenever `uopt->preferScalingRgbhv` was clear,
the watcher's new-mode branch chose on the same preference, and
`applyPresets()` chose on the standard byte for results 5/6/7/13 and for
`BypassRgbhv`. Each entered the channel before the new source had been
measured, so the channel raster was sized from one divider against a count and
rate held from the source before it -- measured, a `vga` to `ypbpr` change
entered carrying 311 lines at 15625 Hz with `STATUS_SYNC_PROC_VTOTAL` at 97,
and the first reading of the Wii arrived twenty seconds later.

A source asking for pass-through now loads the scaled path, which shows any
rate, and the measurement that follows moves the route. That closes the step-12
table's rows for 5, 6, 7 and 15.

**The second entry may not survive the step it is waiting on.** It exists to
reload a different preset when the source's line count crosses 280 or 380, which
is per-standard preset selection over tables that no longer exist -- so what it
still does is set an output resolution preference and take the byte round trip
again. Whether anything is left once `OutputChoice` answers instead is step 12's
question, not a sequence to preserve on the way there.

**And the byte's two largest branches come out here**, because this is the
block that reads them.

### What decides bypass

**A source at or above 640x480 is passed through; anything below is scaled.**
`SourceMeasurement::bypassSuitsCount()` is that rule: a source the line doubler
is not needed for, whose rate reaches the sink -- `LineDoubleBelowLines` and
`BypassMinLineRateHz`, both measured constants. It puts 240p, 288p, 480i and
576i on the scaling path and every VGA-class raster through. Measured on the
bench panel: 800x600, 1024x768 and 1280x1024 all display in passthrough.

**It reads a COUNT and never the held rate.** Bypass measures nothing, so the
held rate goes on naming the mode bypass was entered on and a source that slows
underneath would keep reading as displayable for ever.

**The layer asks it in both directions, and no classification is consulted.**
Every measurement re-answers pass-through, so a source that stops qualifying is
returned to the resolution chosen rather than stranded. The new-mode block that
also read `presetPreference == OutputBypass` -- pass-through wearing a
resolution's clothes -- is deleted with the branch.

**PASS-THROUGH IS NOT DISPLAYING AT EVERY RATE THAT QUALIFIES, AND THE CAUSE IS
OPEN.** Measured on the RiscPC: 640x480 passes through and displays; 720x576
passes through and the sink shows nothing. `HdBypass::dividerFor()` returns
**2039 for both**, because both reach the channel cap rather than the clock
bound, so the channel raster is identical across the two and the difference is
the field rate and the line count. Two explanations are refuted. The sink is
not refusing the mode -- 576p50 is CEA-861 and the same set takes 640x480 raw.
And `STATUS_MISC_PLLAD_LOCK` is not the discriminator: it reads 0 at 720x576 on
the SCALING path too, where the picture is correct.

**AND THE SYNC TYPE IS A SECOND WAY IN, ON ONE MODE, WITH EVERYTHING ELSE HELD.**
The RISC PC sets its sync type from CMOS, so 640x480@60 can be presented twice
over one cable with only that moving. Separate sync passes through and fills the
screen, colours correct and the card's gratings resolved. Composite sync at the
same mode gives no signal, and the sink says no signal rather than naming a
stale mode:

| at 640x480@60 in pass-through | separate sync | composite sync |
|---|---|---|
| `PLLAD_MD` / `STATUS_SYNC_PROC_HTOTAL` | 2039 / 2039 | 2039 / 2039 |
| `STATUS_SYNC_PROC_VTOTAL` | 524 | 98, 235, 884, 1852 -- never 524 |
| `SP_SOG_MODE` | 0 | 1 |
| the panel | full screen, sharp | no signal |

**The separator level is REFUTED as the cause.** `ADC_SOGCTRL` was held at 13,
the default, with the sync processor still failing to count and the panel still
dark -- and the same source at composite sync on the SCALING path displays with
the level walked to 24. So what fails is the sync processor's count on the
pass-through route, not the level it is fed at.

**`rateCanBypass()` is a HARD GATE on the whole choice, not half of the
default.** Passthrough is not offerable where the rate cannot reach the sink,
and a stored preference is re-checked on apply rather than trusted. `SourceKey`
determines the line rate, so the gate answers the same at selection and at
apply.

**The user overrides it per mode, and the override is stored the way the framing
is** -- same `SourceKey`, same record, same lifecycle, found at the moment the
decision is needed. `preferScalingRgbhv` is the interim stand-in for it; it goes
with the policy it encoded, because a global boolean cannot express a per-source
choice and neither of its two answers is right for every source.

**AND IT IS NOT RGBHV-SPECIFIC, WHICH THE NAME SAYS AND THE WIRING DENIES.**
`inputAcquisition.allowPassThrough(!uopt->preferScalingRgbhv)` is one gate in
front of `passThroughSuitsSource()`, ahead of both measurement gates, so it
refuses pass-through for EVERY source. Measured on the Wii at 480p, which is
component rather than RGBHV: 524 lines at 59.8 Hz clears `bypassSuitsCount()`
(not line-doubled, 31335 Hz against `BypassMinLineRateHz` 26000) and clears
`rateCanBypass()` at a held 31395 -- and it is scaled, because the boolean is
stored as 1. `loadDefaultUserOptions()` writes 0, so a unit that has never been
toggled behaves the other way; byte 10 of `/preferencesv2.txt` is which.

**The reason is not preference, it is what the capture can carry.** The write
limit bounds a window at about 1024 IF units however it is placed, so a source
wider than about 512 active pixels cannot be sampled above Nyquist whatever the
divider does -- 1280x1024 cannot carry every pixel at all. Fine vertical detail
on a VESA-class source aliases on the scaling path, and passing it through is
the only way it arrives intact. `../capture-limits.md`.

**It is not a general answer about displays, and cannot be.** EDID is
unreachable -- the encoder is on no MCU's bus -- so what a sink accepts is
unmeasurable from here and the default is one panel's answer. That is what the
per-mode override is for, and why a bypass entered automatically needs a way
back that does not depend on the picture: the removed 535-line gate had none.

### `applyPresets()` is an output selection wearing a preset's name

There are no preset tables, and the three-way dispatch has collapsed to one row:

    result 15               folded to 14, isValidForScalingRGBHV set
    result 1..13, 14        outputChoiceFor() -> loadComputedPreset()

Pass-through is not a preset, so a source asking for it loads the scaled path --
which shows any rate -- and the measurement that follows moves the route. What
is left is a function that holds a standard, loads a computed output, and calls
`doPostPresetLoadSteps()`. `OutputChoice` already carries the output and
`OutputMode::isBypass()` already names the sentinel the engine holds for
pass-through, so the byte is only how the caller says which.

So this function is not renamed, it is dissolved: once the caller passes an
output rather than a standard, what is left is `loadComputedPreset()`. It goes with the byte
rather than before it, because the dispatch is the byte's last real reader.

### Bypass is ONE output mode, and it picks its own register path

**Bypass is a single concept: take the source and leave the scaler out of it.**
Whether that ends in the HD path or the RGBHV path is an implementation detail
decided from what the source measures, not two output modes for a caller to
choose between.

**`Tv5725::HdBypass` is NOT that class, and renaming it to `Bypass` is refused.**
The HD bypass channel is a block RD-5725-1.1 names -- in the descriptions of
`SFTRST_HDBYPS_RSTZ`, `DAC_RGBS_BYPS2DAC`, `DIGOUT_BYPS2PAD`, `DIGOUT_ADC2PAD`,
`OUT_SYNC_SEL` and `OUT_BLANK_SEL_1` -- named for what it is for: carrying a high
resolution around the scaler. The class owns s1 0x30..0x55, which is that block.
The two routes are different silicon, not two configurations of one block:

| | video path | the HD bypass channel |
|---|---|---|
| bypass | `DAC_RGBS_BYPS2DAC` 1, HD bypass channel to DAC | carries the video |
| retired | `DAC_RGBS_ADC2DAC` 1, "ADC (with decimation) to DAC" | **not in the video path**, and no converter is |

`bypassModeSwitch_RGBHV()` releases the block and sets `OUT_SYNC_SEL` to 1 all
the same -- "H/V sync output are from HD bypass" -- so on that route the block is
the output sync generator and nothing else. What made the class look general is
`applyRgbhvPll()`, which writes `PLLAD_KS` and `PLLAD_FS`: ADC registers and
nothing of this block, so they belong to `Adc`.

**`applyRgbhvPll()` is NOT `Adc::postDividerFor()` under another name, and
collapsing them would change the values.** `postDividerFor()` reads
RD-5725-1.1's KS crossover table against a frequency; `applyRgbhvPll()` picks a
`(KS, FS)` pair off the measured line count at 532 and 810. Against the divider
standard 13 installs immediately before it the two disagree across the whole
532..809 band -- KS 2 against KS 3 -- and no divider maps those counts onto the
datasheet's bands at all: 20 MHz would fall at 532 lines only with MD 627, and
40 MHz at 810 only with MD 823. `FS` is a VCO gain rather than a divider and
moves 1, 0, 1 across the three rows, which no frequency table produces. Where
the thresholds came from is unrecorded.

What is still split:

Nothing is split any more: the classification and the path choice were both the
standard byte, and `HdBypass::applyForSource()` is one path for every source.

The two entry points have merged: `VideoPath::setOutputMode()` is the one call,
and `setOutModeHdBypass()` and `bypassModeSwitch_RGBHV()` are gone.

**THE ROUTE IS ONE.** Both former entries drove the HD bypass channel and
`DAC_RGBS_ADC2DAC` is retired, so what is left above is a refactor over a single
route rather than a change of route.

**WHICH ROUTE CARRIES THE VIDEO HAS ONE OWNER.** `Tv5725::VideoRoute` holds it
and `Tv5725::Chip`'s three route methods record what they have just written, so
the held value and `s0_4b` cannot disagree. Which SOURCE is on it is
`VideoSourceSelection::isRgbhv(Info)`, the connector.

**The path choice went entirely, because one route serves both.** The HD bypass
channel carries an arbitrary RGBHV source -- measured, RISC PC on `vga` at
800x600@60 -- with its raster derived from the divider the engine holds rather
than frozen per standard.
`docs/investigations/one-bypass-route-carries-rgbhv.md`.

That makes `ADC2DAC` the one to retire rather than the one to generalise: the HD
channel is the only route with a matrix and a dynamic range converter in
circuit, so it is the only one that can carry a component source at all.

**The passthrough preference used to reach only one half, which is the bug this
shape removed.** `presetPreference == OutputBypass` was read in the sync
watcher's new-mode block and entered the HD path always, while the RGBHV path
was reached from the standard byte holding 15 and nowhere else -- so a user
asking for pass-through on an RGBHV source got the other one. The new-mode block
is deleted and `VideoSourceAcquisition::passSourceThrough()` decides both.

**And it dissolves `sourceIsRgbhv()`.** That predicate exists mostly to decide
scaled against bypassed for a source with no preset. Once the user chooses
bypass and `bypassCanBeDisplayed()` says whether the display can show it, there
is nothing left for it to decide.

### The one call: `VideoPath::setOutputMode()`

One call says what the output should do, and it is the only one:

```cpp
bool setOutputMode(const OutputMode *mode);   // ModeBypass, a resolution, or 0
```

**An asymmetry between entering and leaving is what lets one of them acquire
steps the other lacks.** That is not hypothetical: the leave path was missing the
bring-up, the block restart and the colour matrix, and each was found separately,
two of the three by photographing the television.
`investigations/pass-through-holds-the-only-field-rate-instrument.md`. So both
directions are arms of this one call, and a resolution arriving from the user
leaves pass-through by exactly the route the measurement does -- which it did not
before, and the raster it solved landed on a chip whose VDS was still held.

`ModeBypass` is a real `OutputMode` with `frameLines() == 0`, so pass-through is
expressible as an argument without going back into `PresetPreference`, where it
destroyed the resolution the user chose. **Nothing holds it separately**: the
mode says it, which is what `passedThrough()` reads.

**A null argument is the third state**, and it is not pass-through: a custom
preset names no resolution, so there is no raster to solve and the bytes on the
chip stand. That is why the argument is a pointer.

**Leaving never solves**, whichever end asked. The rate held is the one
pass-through was entered on, so the call configures the chip and returns false;
what solves the output is the measurement that follows -- the caller's next one
where a mode change is already armed, or the preset load the false sends it to.

**And the decision is not here.** `passThroughSuitsSource()`, the user's veto and
the route switch are `VideoSourceAcquisition`'s: pass-through is a statement
about the measured source, so the layer that measures answers it and `VideoPath`
is told. Which leaves `setOutputMode()` a configuration call with no policy in
it at all -- what it does depends on the mode handed to it and the mode already
in force, and on nothing else.

**The resolution the user chose is held up there too**, because it is an input to
that decision rather than to the configuration: pass-through suspends it, and the
way back is `setOutputMode(resolution)`. Held in `VideoPath` it is a second
answer to "what is the output doing" standing beside the mode in force, which is
the pair that let a 1024p choice come back as 1080p.

#### Where it lives, and the two homes that are wrong

**Not `Tv5725::HdBypass`.** That class owns s1 0x30..0x55 and its own reset bit
-- one block, the one RD-5725-1.1 names. The operation calls into eight classes,
of which `HdBypass` is four of nineteen calls:

```
SyncProcessor 4   HdBypass 4   Chip 3   SyncOnGreen 2
SyncMeasurement 2   Adc 2   PresetLoad 1   BringUp 1
```

Putting it there makes a leaf block class write Chip's, SyncProcessor's, Adc's
and BringUp's registers, which inverts the dependency and ends single ownership.

**Not `VideoSourceAcquisition`.** That layer sits above `Tv5725::` and no rung of
it writes a register.

**Not a new class either**, which is the easy mistake: the LEAVE path already
lived in `VideoPath`, delegating to block classes, so the enter belongs beside
it. `VideoPath` is where a chip-wide route switch already is.

#### What blocks moving `enterHdBypass()` in

Of its 53 lines most are already `Tv5725::` calls. Four clusters pin it:

| blocker | where | clears with |
|---|---|---|
| `FrameSync::cleanup()`, `externalClockGenResetClock()` | the display clock and the Si5351 | step 11 |
| `adco->r/g/b`, `uopt->enableAutoGain`, `uopt->wantOutputComponent` | `applyStoredAdcGain()`, `applyRGBPatches()` | ADC gain ownership |
| `rto->boardHasPower`, `autoBestHtotalEnabled`, `presetID` | guards and flags | with their branches |

`rto->inputIsYpBpR` is NOT one of them -- `Adc::inputIsComponent()` already
answers it from held state, and substituting it removes two lines on its own.
Three helpers are already free of sketch state entirely:
`setAndUpdateSogLevel()`, `resetDebugPort()` and `restartAfterBypassSwitch()`.

So the move is incremental rather than blocked: the register-writing core goes
now, and each cluster above later deletes lines from the sketch rather than
requiring the shape to be revisited.

#### The hazard

`enterHdBypass()` carries an ordering constraint -- the PLL latches are last, and
a divider written after `latchPLLAD()` leaves the register reading the new value
while the part clocks the old one, which is a solid green screen with nothing
self-inconsistent to diagnose from. **Any split must preserve sequence exactly.**
What catches a violation is `test_hd_bypass.cpp` plus the three leave-path cases
in `test_video_source_acquisition.cpp`, and a bench round trip, which is cheap:
`printf 'MODE X800 Y600 C256 F60\n' | nc 192.168.88.10 6502` and back.

**11. Steer the ADC PLL.** The band index and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path. **The band moved; the
RATE did not**, and it cannot yet: `getPllRate()` drives the debug pin through
the test bus and counts pulse ticks with FrameSync, which the engine has no
route to. So the sketch measures and the class decides, and the measurement
lands wherever FrameSync does.

**12. Delete `getVideoMode()` and `videoStandardInput`.** *(Landed.)* With them
go `heldStandard()`, `holdStandard()`, `standardForPresetLoad()`,
`rto->notRecognizedCounter` and `applyPresets()`'s argument. Nothing holds a
standard.

**THE UNIT OF REMOVAL WAS THE VALUE, NOT THE FIELD.** A change that moved one
value to another -- 3 to 14, say -- would have entrenched the byte rather than
retiring it, because every reference to every value has to go. What each value
decided, and what decides it now:

| value | meant | decided by |
|---|---|---|
| 0 | nothing recognised | `SyncProcessor::hsyncActive()`, and the engine's run at the gates |
| 1, 2 | interlaced SD | nothing: the channel's floor excludes 15.6 kHz, and the scan mode is derived |
| 3, 4 | progressive SD | nothing: the two pairs that differed collapsed into the computed path |
| 5, 6, 7 | HD | nothing: the three frozen arms, `optimizePhaseSP()`'s half-sample and the sub-coast gate are all gone |
| 8, 9 | medium resolution, and unrecognised but steady | `Adc::postDividerFor()` off the clock the divider and measured rate make |
| 13 | YPbPr passed through | `VideoRoute::isHdBypassChannel()` for the route, `Adc::inputIsComponent()` for the colour path |
| 14 | an RGBHV source | `VideoSourceSelection::isRgbhv(Info)` -- the connector, known before any detection pass |
| 15 | pass RGBHV through | `VideoSourceAcquisition::passSourceThrough()`, from the measurement |

**THE TWO PREDICATES WERE NOT IDENTICAL, which is what made 14 more than a
rename.** The byte said an RGBHV source had been DETECTED; the selection says
one is SELECTED, and on a freshly chosen input with no sync they differ.
`rgbhvBypass()` is where that bites, since `RgbhvOutput::isScaling()` starts
false. Every reader was taken with what it actually wanted: the engine's own
answer sits beside the term at `updateCoastPosition()`, `applyPresets()` and
`loop()`'s coast gate and says the same thing before detection;
`prepareSyncProcessor()` runs after the output has been chosen; and
`optimizeSogLevel()`'s gate is deleted, because `SyncOnGreen::acquire()` already
refuses a separator that is not in the sync path.

**`getStatus16SpHsStable()` IS `SyncProcessor::hsyncActive()`, one register
bit.** Its pass-through branch answered `STATUS_INT_INP_NO_SYNC`, which does not
latch on this board -- 0 of 1486 samples across a real sync loss, with
`INT_ENABLE4` reading 1 and the neighbouring bits latching freely in the same
window -- so it returned true whatever the source did. `STATUS_16` answers on
both routes instead: pass-through does not take the sync processor out of the
video path, and on a passed-through source `HSACT` reads 1 in 489 of 489 samples
with `STATUS_SYNC_PROC_VTOTAL` holding the count the mode is due in all of them.

Its other term required `STATUS_SYNC_PROC_HSPOL` clear where the byte held
`NtscInt` or `PalInt`. **The obvious substitution is refuted**: those are Mode
Detect's 50/60 Hz interlaced buckets, which a 15 kHz progressive source lands in
too, so the term reads like `sourceLowLineRate()` spelled in the byte and is
not. Measured on the bench RiscPC at 320x256@50, `HSPOL` is 1 in 6 of 6 samples
while the source is acquired and the picture clean, so keyed on the measured
rate the function would report never stable on the everyday bench source. What
the term withheld on any source that could reach it is nothing: every source in
those buckets sends a negative-going hsync.

**A PRESET LOAD SETTLES ONCE.** The SD branch spun up to four times waiting for
`getVideoMode()` to agree with the byte and for `getSourceFieldRate()` to land
inside a window named per standard. It ran at the end of a load that has already
told the engine the source is about to change mode, so nothing measurable about
the source is true yet and the acquisition layer is what waits for it. A second
wait below it asked `getVideoMode()` to name something within 1505 ms of the
same start as the horizontal-sync wait above it, which on an RGBHV source is
that line re-asked.

**`applyPresets()` TAKES NO ARGUMENT.** Four decisions were keyed on it. The
sync-type probe ran where the byte arrived as 14 and was skipped where it
arrived as 15 -- the same source with a different output chosen for it -- so a
source passed through never had its sync type established there. The three soft
resets were withheld for 5, 6, 7, 13 and 15, which are exactly the values that
used to be routed to the channel from there, and a block left held shows nothing
whatever the preset writes. The 0 branch is "no horizontal sync". And the load
sat behind a dispatch on eleven of the fifteen values, which after the other two
folds covered everything that could reach it.

What `holdStandard()` did beside writing the byte was steer
`Tv5725::RgbhvOutput`, so its call sites take `chooseBypass()` or
`chooseScaling()` directly.

**THE REGISTER-WRITE TRACE WENT WITH IT.** `/trace/standard` forced a standard
so a per-standard branch could be diffed on a bench with no source for it, and
there are no per-standard branches left. `docs/testing.md` keeps what the
facility learnt that is true of any trace comparison.

**`Tv5725::PresetLoad` DID NOT SURVIVE AS A CONCEPT**, only as one flag. Keeping
it with a byte-free signature would have preserved the idea that a load is
chosen by classifying the source, which is the thing being retired. What is left
is `scalingRgbhvInForce()` and its two setters -- engine mode state that belongs
to `VideoPath`, and **not** `RgbhvOutput::isScaling()`, which says what the
source is entitled to where this says what the last load enabled; the
bypass-refused path sets them opposite. The 280/380 line buckets, the count a
preset was chosen for and the instance half went earlier, none of them load
bearing.

**The engine no longer reads `PLLAD_MD` as an input.**
`SourceMeasurement::adopt()` was the one place it did; it is `holdDivider()`
now -- told, not read. Registers are not storage: a value the firmware needs to
know is held, and a register is where it is written to.
`investigations/the-bypass-divider-is-capped-by-the-channel-counter.md`

**`Tv5725::SourceStandard` IS DELETED.** Everything it wrote is derived from
something measured:

| what it wrote | owner now | derived from |
|---|---|---|
| `IF_SEL_WEN`, `IF_HS_SEL_LPF` | `InputFormatter::applyScanMode()` | the line doubler, which the engine measures |
| `VDS_V_DELAY` | `VideoProcessor::applyScanMode()` | the same |
| `MADPT_Y_DELAY` | `Deinterlacer::applyScanMode()` | the same |
| `IF_HS_Y_PDELAY`, `VDS_Y_DELAY` | the two `applyScanMode()`s | the doubler and `Adc::inputIsComponent()` |
| `IF_HS_TAP11_BYPS` | nothing -- deleted | always 0, which `InputFormatter::init()` leaves |
| `ADC_FLTR` | `Adc::init()` | nothing: one corner for every source |
| `IF_PRGRSV_CNTRL`, `IF_HS_DEC_FACTOR`, s1_02 | the two `applyScanMode()`s | the line doubler |
| `SP_SDCS_VSST`/`VSSP` | `SyncProcessor::applySdVsyncPosition()` | nothing: one value for every source |

**The two policy questions it was waiting on were settled by measurement rather
than by choosing.** The wanted oversample is `Adc::OversampleAsClockAllows` on
every path, so the arms' 4-on-interlaced-SD and 2-on-progressive had nothing to
replace. And the analog filter corner is one value opened widest by
`Adc::init()`: `ADC_FLTR` is an anti-alias low-pass in front of the sampler and
so does work only below Nyquist, which the narrowest corner the part offers is
never at -- 40 MHz against a Nyquist of 17.3 MHz on the bench's 15 kHz source
and 38.6 MHz on its fastest. Swept across all four corners at both clocks,
measuring grating modulation on PM5544 with the same corner shot twice as the
control, the control's own repeat spans the whole spread and the order is not
monotonic. `docs/investigations/the-analog-filter-corner-is-above-nyquist.md`.

**THE HD ARM COST A SECOND OWNER.** It wrote `ADC_FLTR`, `IF_PRGRSV_CNTRL`,
`IF_HS_DEC_FACTOR`, `VDS_Y_DELAY` and a whole-byte 0x74 into s1_02, every one of
which has an owner. The byte was the one that disagreed -- `IF_SEL_WEN` 0 against
`applyScanMode()`'s 1, `IF_HS_TAP11_BYPS` 1 where `InputFormatter::init()`
leaves 0 -- and which owner won was an ordering accident, because
`solveScanMode()` early-returns unless the line doubling moved and only a source
mode change clears `scanModeApplied_`. The test had recorded the conflict rather
than catching it: 5, 6 and 7 were excluded from the three loops asserting that
no standard writes what the scan mode decides.

**THE SD VERTICAL SYNC POSITION IS NOT A RASTER PROPERTY.** `SP_SDCS_VSST` is
where the block asserts the vertical sync it regenerates out of composite sync,
so it is where the captured frame begins -- a vertical pan, linear at one source
line per count from 4 to 84 with no saturation, with the capture window the
engine solves unmoved beside it. It reaches the picture only where `SP_SOG_MODE`
is 1, so the byte was selecting on a fact the register does not ask about: a
composite-sync RGBHV source kept 4 with the window live while a component 480p
source got 14. It does not belong in `SourceTiming` either -- there is no
published raster fact to derive, and a second vertical placement the engine
cannot see only puts its model and the picture out of step. The constraint that
survives is a bound, not a target: the value must land inside the source's
vertical blanking, 45 lines being the shortest here.
`docs/investigations/the-sd-vsync-window-follows-the-sync-type.md`

**13. Delete `runSyncWatcher()`.** *(Landed.)* What was left of it is
`VideoSourceAcquisition::keepSourceComing()`, beside the run the whole of it
counts in.

**THE TICK IS ONE TICK.** It runs on the pass that advanced the run rather than
on a timer of its own: two 20 ms cadences beside each other drift until a count
is answered twice or not at all, which is what made the long-absence restore
reachable only through a window of one detection interval.

**Each act joined the class that owns its registers**, rather than the loop
being moved with a callback handed back per act -- the sketch only shrinks.
`optimizePhaseSP()` is `acquireSamplingPhase()`, `optimizeSogLevel()` is
`acquireSeparatorLevel()`, `runRecoveryStep()` is `runRecovery()`,
`setAndUpdateSogLevel()` is `SyncOnGreen::putInForce()`, and
`updateSpDynamic()`, `updateCoastPosition()` and `updateClampPosition()` are
`applySyncProcessorDynamic()`, `placeCoastWindow()` and `placeClampWindow()`.
`steerHdBypassVsyncWindow()` was deleted outright.

**What each act needed from the sketch is a peer's answer now.** Which source
is selected is `VideoSourceSelection`'s -- it cannot be measured, because half
the input path is the HC32F460's analog switches and those cannot be read back.
Whether there is anything to write to is `Chip::hasPower()`, one scratch byte
round-tripped and recorded. Whether the input is component is
`Adc::inputIsComponent()`, whether the phase search found anything is
`Adc::phaseFound()`, and what the user asked of the deinterlacer is held by
`Deinterlacer` rather than handed in on every pass.

**THREE ACTS ARE REPORTED RATHER THAN DONE.** The frame time lock and the
external clock generator live above this layer, and two flags are the rest of
the sketch's, so `poll()` leaves a `Report` behind -- cleared at the start of
every pass, so a caller reading it once a pass sees each decision once. That is
the shape `Deinterlacer::steer()` already used.

**`frameTimingMoved` AND `vsyncLockStale` ARE NOT THE SAME FIELD.** Collapsed
into one they reset the frame time lock on every pass a source is unsettled, so
it never establishes at all: measured, a clean acquire at 15625 Hz with every
output register correct and a black panel. The reset is the deinterlacer's act
and the long-absence restore's; the stamp is what the separator tuning and the
recovery branch leave.

**The two outer gates are `allowMaintenance()`.** Off while a source is
disconnected, because detection owns the input then and runs a heavier search of
its own, and off while the user has the automatic path switched off. Told every
pass rather than at every writer, so neither can go stale.

**WHAT IS LEFT IN `loop()` IS THE PLATFORM.** `FrameSync` and the Si5351 are
above `Tv5725::` and the sketch still owns them, so `loop()` calls
`inputAcquisition.poll(millis())` and acts on the report. Collapsing that last
block needs the frame time lock and the clock generator under the engine, which
is where step 11's rate is already waiting.

## Input selection is the same collapse, one level up

`applyInputSelection()` is already the single path the OLED menu and
`/input?src=` share. What it is missing is the engine: it writes the input
registers, resets the sync processor, raises `rto->sourceDisconnected` and
returns, so the engine learns the source moved only when detection eventually
runs and something calls `applyPresets()`.

**Selecting an input is a source event, so it belongs to the same entry point as
every other source event.** The engine is told, it re-acquires and it solves --
rather than the sketch poking registers and leaving the engine to notice. That
is what makes the code tractable to reason about: one path in, whether the
request came from the menu, the remote or HTTP.

**What it fixes is a RACE, not a missing call.** The sync type IS re-established
after an input change -- the latched-disturbance re-arm gets there. But the probe
runs 2 ms after the switch, against a separator the ladder is still cycling on
from the absent input it just left, so it can answer a question about the
previous input's state: measured once in three returns, a separate-sync source
classified as csync, recovered only because the re-arm fired three more probes.
Remove those and the state is the latch. An input event should probe from a
known state instead of racing an acquisition loop that does not know it
happened, and ordering those two is what the layer is for.
`docs/investigations/the-sync-type-probe-races-the-separator-walk.md`

### The input toggle is an input event, not the ladder's last rung

Every other rung means *try harder on this input*. `ToggleInput` means *give up
on this input and look at another*, which is literally an input change. Four
things this page separately recorded as awkward all follow from where it sits:

- **It is the only rung gated on user intent.** `detectionMayChangeInput()` asks
  whether the user chose this input -- policy about commands, not acquisition.
- **It is the only rung that must stop the ladder when it SUCCEEDS**, which is
  the whole reason a stop-escalating concept was wanted.
- **It is out of order.** `ReopenSogSeparator` sits after it at 450, so the mux
  moves before the cheaper separator reopen is tried. Remove the toggle and the
  list is cleanly cheapest to most expensive.
- **It is why `0x07fe` had to be a message.** The ladder had no way to promote a
  decision except by writing a value another block reads.

The engine already names three ways the problem moves, and only one is missing:

    inputMuxChanged()      the input the source arrives on is now unknown
    inputTimingsChanged()  same input, the source moved
    setOutputMode()        the output should do this: a resolution, or pass-through

`VideoPath::inputMuxChanged()` is the entry point the sketch lacks -- called by
`applyInputSelection()` and by `VideoSourceAcquisition` when the ladder exhausts. It
forgets the sync type, because a different input shares none of it; the
escalation position is `VideoSourceAcquisition`'s to forget.

**NOTHING TERMINATES, which dissolves the cadence question.** A unit with no
detectable source keeps looking, so there is no end state to design and no
sentinel to write:

    nothing chosen    cycle the ladder on this input; when it exhausts, move to
                      the next source and start again. Sweep for ever.
    input chosen      cycle the ladder on that input for ever. Never move.

The second row is the rule that an explicit selection is a command: a chosen
input is selected whether it has a signal or not, so there is nowhere to promote
to. `detectionMayChangeInput()` is already that distinction. **"No signal out"
is a report of state, not a terminus** -- where nothing is chosen it falls at
the end of a full sweep, which is what the 2046-pass expiry approximated.

**And the sweep is over SOURCES, not the two ADC inputs.** `selectOtherInput()`
moves `ADC_INPUT_SEL` alone, so it can only try the other half of one mux; the
HC32's analog switches do not move with it and five of the six inputs need the
frame sent to them. A sweep goes through `applyInputSelection()`, which is also
what `applySavedInputSource()`'s silent `default:` branch should do when nothing
was ever stored.

**Not measured; a design note rather than a finding.** What supports it is that
four separately-recorded awkwardnesses have one cause.

## The bar

**Observable picture behaviour, on the paths the bench can exercise** — not
binary equivalence and not code equivalence. Code with no observable effect on
the picture is deleted rather than preserved, and which branch a step is judged
against is decided by which source is plugged in: `docs/bench-sources.md`.

Two reproductions reach most of this and are scriptable from a session:

- a sync-type round trip, `SYNC 1` then `SYNC 0` over ModeServ, which exercises
  the sync-type probe, the coast and clamp windows and the sync separator level
- an input with genuinely no signal, which is what a step that withholds
  recovery has to be checked against

**AND THAT SECOND ONE IS NOT `/input?src=rgbs` WHILE THE Wii IS POWERED.**
Selecting `rgbs` came back `state: acquired`, 310 lines x 50.24 Hz,
`ADC_INPUT_SEL` 0 -- the Wii's own signature, on the input nothing is plugged
into. Selecting an input the HC32 routes elsewhere does not disconnect what the
ADC is already looking at, so the reproduction needs the console unplugged and a
session that assumes `rgbs` is empty is testing the Wii. Absent looks like
`state: absent`, `VTOTAL` 0, `DAC_RGBS_PWDNZ` 0.

**And a dwell-based reproduction is not one.** The separator level CYCLES on an
absent source rather than settling, so what a round trip does depends on where in
that cycle it lands, not on how long it lasted: 8 s, 90 s and 90 s again gave
right, wrong, right.
`docs/investigations/the-sync-type-probe-races-the-separator-walk.md`

The two cover different arms, and the SD one is not optional here: the RISC PC
over ModeServ covers arbitrary rasters, both sync types and progressive, while a
Wii on YPbPr covers sync on green, interlace and component colour -- the only
source here that can judge the luma delay `Adc::inputIsComponent()` now decides.

**WHICH ONE A STEP MUST RUN IS NOT THE AUTHOR'S CHOICE.**

| a step that touches | must run |
|---|---|
| how the engine re-arms, or anything the sync type is derived from | the sync-type round trip |
| a recovery, a gate in front of one, or the no-sync branch | `/input?src=rgbs` with nothing attached |
| the sync separator level, coast, clamp or phase | the round trip, whose csync leg is the only thing here that uses them |
| geometry, the raster, the windows | a mode change, and the picture judged against `docs/bench-sources.md` |

**A MODE SOAK IS NOT A SUBSTITUTE FOR THE ROUND TRIP, and it looks like one.**
Cycling every mode the source offers holds the sync type constant throughout, so
36 changes across nine timings pass while the sync type is left on the wrong
path indefinitely. That is a measured miss, not a hypothetical: it is how a
change that consumed the latched disturbance reached the bench and sat on the
csync path for 74 s with `SP_VTOTAL` reading 97.

**And a host test that passes either way proves nothing about it.** The one
covering that change pinned an interrupt arriving AFTER a solve, which fires
whether the latch is consumed or not. Where a change alters WHEN something is
re-tried rather than what it does, the host layer cannot see it and the bench
reproduction is the whole of the evidence.

## Not in scope

- `detectAndSwitchToActiveInput()` and `inputAndSyncDetect()`. They run at boot
  and on an input change rather than periodically, so they are not the watcher;
  they come after it, if at all.
- The `HPERIOD_IF` railing state, which is its own investigation.
- The reference sampling clock, which `VideoPath::prepareToMeasure()` installs
  so a source can be measured through a divider that is not the last mode's.
  The trap it escapes is real; what is open is that it installs over a picture
  that was working, which is what still gates steps 10 and 13.
  `investigations/the-reference-clock-is-applied-to-a-working-picture.md`.


## The standard bits are vertical-period buckets, not an interlace measurement

`STATUS_IF_INP_NTSC_INT` and `STATUS_IF_INP_PAL_INT` classify the vertical
period into a standard's bucket. The `INT` names which standard -- an interlaced
broadcast one -- and says nothing about whether this source is interlaced.

The thresholds `ModeDetect::init()` writes give it away, against a vertical
detect unit of field lines over about 8.2:

| register | value | x 8.2 | what it buckets |
|---|---|---|---|
| `MD_NTSC_INT_CNTRL` | 32 | 262 | a 525/60 **field** |
| `MD_PAL_INT_CNTRL` | 38 | 312 | a 625/50 **field** |
| `MD_NTSC_PRG_CNTRL` | 65 | 533 | a 525/60 **frame** |

Measured: the RISC PC at 640x200@60 is **progressive** and reads
`STATUS_IF_INP_NTSC_INT` 1, unchanged when interlace is switched on. The same
holds for `PAL_INT` at 320x256@50. Any raster whose vertical period lands in an
interlaced bucket is reported as that standard, and a 15 kHz progressive source
always will, because its field period is inherently in the range of an
interlaced standard's field.

**So the Wii at 480i reading `NTSC_INT` is not evidence the chip detects
interlace.** It means only that the vertical period is near 262 lines, which a
240p source satisfies equally. There is no source on which these bits have been
shown to measure scan type, and the pair `NTSC_INT`/`NTSC_PRG` separates 480i
from 480p only because one is a 262-line field and the other a 525-line frame.

Ignore them for scan type. `docs/investigations/interlaced-source-measurement.md`


## The steadiness runs: two instances, one policy

There are two runs over `measureSourceLines()`, with the same threshold,
advanced on the same poll:

| where | asks |
|---|---|
| `SourceMeasurement::sampleSteady()` | is the count worth paying 250 ms for a field rate |
| `VideoSourceAcquisition::countHeld()` | has the source moved |

**The instances must stay separate.** Filling the idle one while the engine is
idle leaves the next mode change's first poll believing a count from the mode
before it, which is why the source-moved gate has its own.

**The policy must not be.** Teaching one of them to accept an interlaced count's
alternation left the other blocking, and an interlaced source still never
acquired -- measured on the bench, the fix looked complete and changed nothing.
`Tv5725::SteadyRun` holds the definition once and both delegate.

**Two more runs of the same shape remain**, over the field rate rather than the
count: `VideoSourceAcquisition::rateMoved()` and
`SourceMeasurement::rateSettled()`. They compare with a tolerance rather than for
equality and use different thresholds -- 4 against 8 -- so folding them is a
separate question, and the first thing to ask is whether three different
questions should share `SteadySamples`.

**`countHeldStill()` is deliberately not one of them.** It blocks for 30 samples
at 10 ms before a preset load, with a plus or minus 3 tolerance, because a load
is expensive and a source mid-change gives a count that is wrong AND steady for a
few samples.
