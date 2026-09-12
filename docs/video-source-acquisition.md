# Video source acquisition

Goal: `runSyncWatcher()` and `rto->videoStandardInput` are both deleted, and the
responsibility they share -- keep video coming, and know what is coming -- has
one owner instead of none.

**`VideoSourceAcquisition`** sits ABOVE `Tv5725::`. It owns the tick, coordinates the
measurement and decides. `Tv5725::SourceMeasurement` reads the source for it and
`Tv5725::VideoPath` is handed the answer and writes registers. Each of the three
holds what it alone reads, which is the rule the whole page turns on.
`loop()` ends up with one call where it has two:

```
inputAcquisition.poll(millis());
```

`VideoPath::poll()` does not survive: it is turned inside out, not moved.

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

**The table is the TARGET, not the tree.** `VideoSourceAcquisition` reads the
divider today only to log it and to check the latch; the ladder is still
`runSyncWatcher()`'s. What each party holds is settled; where the code is
against it is the list of steps below.

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

**Today that is a three-call handshake rather than an event.**
`prepareToMeasure()`, `pollDeferred()` and `solveFromMeasurement()` are the
acquisition layer driving one solve in stages, which is the polling relationship
the target removes.

### The measurement is the acquisition layer's, so the timings arrive known

`VideoPath` is called once the source HAS been measured, with the timings it
needs, rather than holding the class that takes them. Of the nineteen places it
reaches `SourceMeasurement` today, thirteen are reads of a measured value -- the
line count, the field rate, the line rate, the divider, the scan mode, the hsync
width and its polarity -- and those become what the event carries. Seven are
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
one: the classes under `src/` against the legacy sketch -- `runSyncWatcher()`,
`rto` and the globals. `VideoSourceAcquisition` is engine, and so is every `Tv5725::`
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
`docs/investigations/vperiod-if-on-rgbhv.md`.

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
| `choice_`, `rasterMode_`, `modePending_`, `solvePending_`, `usableHorizontal_`, `usableVertical_` | **stays.** Published through accessors; the class derives from them |

**Keep the state that describes the video output nearest the class that solves
it.** Gathering all of it into `VideoSourceAcquisition` does not remove state -- it
relocates it and adds a parameter, because a signature taking it as separate
arguments is unusable, so it becomes one struct another class mutates. That
leaves the data in one class and the behaviour that owns it in another, and it
makes the acquisition layer a fresh place to put things: the accretion `rto` is,
with a new destination. The acquisition layer has no use for a porch stop.

**The fault behind the rule is duplication, and it does not generalise.** Two
callers advancing one steadiness run leaves `idleRun_` double-advanced and the
run no longer over consecutive polls. That argues against two owners of one
fact, which is what `solvedLines_` was, and says nothing about a solver holding
what it derived.

**A move still costs something, so it is worth being the only one.** Bypass used
to zero the count directly in `enterBypass()`; the reader now forgets it when
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
| `noSyncCounter`, `continousStableCounter`, `notRecognizedCounter`, `failRetryAttempts`, `sourceDisconnected`, `syncWatcherEnabled`, `isValidForScalingRGBHV`, `HdmiHoldDetection` | `VideoSourceAcquisition` |
| `videoStandardInput`, `osr`, `presetID`, `presetDisplayClock`, `presetVlineShift`, `presetIsPalForce60`, `applyPresetDoneStage` | the value handed to `VideoPath` |
| `phaseSP`, `phaseADC`, `phaseIsSet` | `Adc` |
| `motionAdaptiveDeinterlaceActive`, `deinterlaceAutoEnabled` | `Deinterlacer` |
| `medResLineCount` | `ModeDetect`, which already has `applyMedResLineCount()` |
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
`idleRun_` came to count runs of different lengths. Whether the escalation wants
a slower cadence than detection is a bench question, and asking it at all
requires one owner of the tick.

## Why the classification goes

gbs-control was written for retro consoles, where the source is one of a short
list of known standards, and `videoStandardInput` is that list. A machine that
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
source. `docs/investigations/vperiod-if-on-rgbhv.md`, `docs/bench-sources.md`.

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
| the held-standard fallback | 1, was 3 | `standardForPresetLoad()` |
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

`VideoPath::sourceIsPresent()` is that second answer and it has landed, with no
caller, because the gate it is for cannot move until step 7. The rule it follows
is the one already recorded for `sampleSteady()`: **publish the answer, do not
recompute it** -- `countHeld()` mutates `idleRun_`, so a second caller
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

## Why the watcher has to go rather than be tidied

`runSyncWatcher()` keeps a **parallel model of the source**, and calls into the
engine once (`geometry.sourceInterrupted()`). Two owners of one model is the register problem one level up, and
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
- The steadiness of the source is counted twice, as `rto->noSyncCounter` and
  `rto->continousStableCounter` in the sketch and as `idleRun_` in the engine,
  and the two disagree about whether a source is there.

So the fault is not that the code is old. It is that a second owner of a
register cannot be made correct by improving either owner.

## The shape

**The engine currently DEPENDS on the watcher**, which is why it cannot simply
be deleted. Each row is a step: the engine takes the job over and the sketch's
copy goes.

| the engine needs | the sketch supplies |
|---|---|
| to be told a preset load happened | `applyPresets()` calls `modeChanged()` |
| the latched source disturbance | `runSyncWatcher()` calls `sourceInterrupted()` |
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

**`Tv5725::SyncRecovery` is in the wrong namespace by the same argument.** Ten
of its eleven rungs are TV5725 operations, which is why it reads as marginal,
but the eleventh changes the input and the list as a whole is policy about the
board. It moves out of `Tv5725::` at step 7.

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

`Tv5725::SyncRecovery::stepAt()` answers which rung a pass is due, and
`runSyncWatcher()`'s no-sync branch dispatches on the answer. Each step fires
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
`sampling_.sampleSteady()` also fills `unmeasurableRun_`, the gate holding
`recoverDivider()` back, which makes the recovery fire on the first poll of the
next mode change and infer a divider from a count that was never a measurement.

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

- the sync-processor sweep no longer fires on the classification alone
  (`SyncSearch::shouldSweepSyncProcessor()`)
- the three copies of the held-standard fallback are one
  (`standardForPresetLoad()`)
- the Mode Detect threshold dither is deleted
- both of `updateSpDynamic()`'s hunt branches read one `searching` value, so
  neither fires on a source the sync processor is counting
- `VideoPath::sourceIsPresent()`, the measurement that replaces the
  classification at the no-sync gate -- the method, not yet the wiring

**The count is not the progress.** 86 occurrences on 76 lines is roughly
where it has sat, because what has landed so far is structural -- one
held-standard fallback where there were three, the threshold dither deleted,
`sourceIsPresent()` written. The references leave in two blocks, at steps 10 and
12: the byte is deleted LATE, once the RGBHV block has moved and nothing reads
it, rather than being unpicked reference by reference from inside a function
that is going anyway.

**Steps 1, 2, 3, 5 and 6 have landed, and with them every bounded ownership
move.** What is left is not extraction: steps 4 and 7 are one change, and 8 to
13 follow the byte out. `SyncOnGreen` owns the separator level and
its acquisition, `SyncProcessor` the coast and clamp windows, `Adc` the sampling
phase -- leaving `updateCoastPosition()`, `updateClampPosition()` and
`optimizePhaseSP()` as the GATES in front of them, which is step 4's to replace.

**Step 7 is in flight.** The ladder is an ordered list and the sketch dispatches
on it, which is what step 4 was waiting for; what is left of step 7 is the
OWNER -- the list still lives under `Tv5725::` and the sketch still holds the
counter that indexes it.

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
pass runs on `VideoPath::DetectionIntervalMs`. The steadiness run is counted in
detection passes and `loop()` goes round far faster than the sync watcher's
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
own run, and `VideoPath::sourceIsPresent()` replaces the classification at the
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
than moved because `Adc` owns that group. What is left in the sketch is the
GATING -- `standardIsHeld()`, `getVideoMode()`, `rgbhvBypass()` -- which is step
4's to replace, and an `if` whose body is empty.

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

What the sketch keeps is not leftover. The divider-latched gate stays in front
of the call, so `Adc` needs no `SourceMeasurement`; and
`videoStandardInput >= 5 && <= 7 && osr == 2` stays, because the ratio cannot
separate that case from a progressive source and what it wants is the line rate.
That one leaves with its branch at step 10.

**7. `VideoSourceAcquisition`, and the escalation list it holds.** The ordered set of
named recoveries has replaced `% 27`, `% 32`, `== 38`, `% 150` and `% 413`;
`runSyncWatcher()` dispatches on `SyncRecovery::stepAt()` and each step fires
once per cycle. What is left is the OWNER: the list belongs above `Tv5725::`
with the class that holds the tick, and `SyncRecovery` moves out of `Tv5725::`
with it. **Step 4 waits on this**, because wiring the gate is what lets the
branch advance far enough to reach these.

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
`idleRun_` double-advances and the run both readers depend on is no longer over
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

**9. Steer the deinterlacer**, to `Deinterlacer`.

**10. The RGBHV block**, to `PresetLoad` and `OutputChoice`, with the preset load
becoming an injected action. The largest single piece, and the one that carries
most of the standard byte.

**Its two entries are one call now.** Leaving bypass and crossing into another
preset's bucket ran thirty byte-identical lines each, so every register in that
sequence had two writers. The sync processor's share is
`SyncProcessor::applyForScalingRgbhv()` and the ADC's is
`Adc::applyScalingChargePump()`; what is still spelled out is the option bit,
the line counter's start, the standard byte's round trip through
`applyPresets()`, and the external clock generator. Those are the four this step
still has to place, and the standard byte's is step 12's.

**The second entry may not survive the step it is waiting on.** It exists to
reload a different preset when the source's line count crosses 280 or 380, which
is per-standard preset selection over tables that no longer exist -- so what it
still does is set an output resolution preference and take the byte round trip
again. Whether anything is left once `OutputChoice` answers instead is step 12's
question, not a sequence to preserve on the way there.

**And the byte's two largest branches come out here**, because this is the
block that reads them.

### What decides bypass, once the block has moved

**A source at or above 640x480 is passed through; anything below is scaled.**
Measured on the bench panel: 800x600, 1024x768 and 1280x1024 all display in
passthrough. Expressed in what the board can measure, that is a source the line
doubler is not needed for and whose rate can reach the sink --
`LineDoubleBelowLines` and `BypassMinLineRateHz`, both already measured
constants. It puts 240p, 288p, 480i and 576i on the scaling path and every
VGA-class raster through.

**`rateCanBypass()` is a HARD GATE on the whole choice, not half of the
default.** Passthrough is not offerable where the rate cannot reach the sink,
and a stored preference is re-checked on apply rather than trusted. `SourceKey`
determines the line rate, so the gate answers the same at selection and at
apply.

**The user overrides it per mode, and the override is stored the way the framing
is** -- same `SourceKey`, same record, same lifecycle, found at the moment the
decision is needed. `preferScalingRgbhv` goes with the policy it encoded: a
global boolean cannot express a per-source decision, and neither of its two
answers is right for every source.

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

There are no preset tables. What the function does now is dispatch on the
standard byte to one of three outcomes -- compute an output and load it, name an
HD standard and return, or switch to RGBHV bypass -- and every one of those is a
statement about the OUTPUT. The byte is only how the caller says which.

    result 1,2,3,4,8,9,14   outputChoiceFor(result) -> loadComputedPreset()
    result 5,6,7,13         hold the standard, setOutModeHdBypass(false)
    result 15               bypassModeSwitch_RGBHV()

`OutputChoice` already carries the first row's answer and `OutputMode::isBypass()`
already names the sentinel the engine holds for the other two, so the dispatch
is a third spelling of a fact those two classes own. What the rows do not share
is WHICH bypass -- HD or RGBHV -- and that is the piece neither class holds.

So this function is not renamed, it is dissolved: once the caller passes an
output rather than a standard, the first row is `loadComputedPreset()` alone and
the other two are the bypass switches called directly. It goes with the byte
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
| HD bypass | `DAC_RGBS_BYPS2DAC` 1, HD bypass channel to DAC | carries the video |
| RGBHV bypass | `DAC_RGBS_ADC2DAC` 1, "ADC (with decimation) to DAC" | **not in the video path** |

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

| split | today | belongs to |
|---|---|---|
| two entry points | `setOutModeHdBypass()`, `bypassModeSwitch_RGBHV()` | one `apply()` on the mode |
| the source's classification | `videoStandardInput` 14 and 15 | the measured source |
| the path choice | `applyForStandard()` branching on the standard byte | the measured source |

**WHICH ROUTE CARRIES THE VIDEO HAS ONE OWNER.** `Tv5725::VideoRoute` holds it
and `Tv5725::Chip`'s three route methods record what they have just written, so
the held value and `s0_4b` cannot disagree. What the standard byte still carries
of this is the SOURCE half -- 14 and 15 say an RGBHV source is scaled or not --
and `steerableRgbhv()` is that half plus the route.

**And the path choice can go entirely, because one route serves both.** The HD
bypass channel carries an arbitrary RGBHV source -- measured, RISC PC on `vga`
at 800x600@60 -- once its raster is derived from the divider the engine holds
rather than frozen per standard. Entering that route on such a source today
gives no signal, because `applyForStandard()` has no arm for 14 or 15 and the
block keeps `enable()`'s resting timing.
`docs/investigations/one-bypass-route-carries-rgbhv.md`.

That makes `ADC2DAC` the one to retire rather than the one to generalise: the HD
channel is the only route with a matrix and a dynamic range converter in
circuit, so it is the only one that can carry a component source at all.

**The passthrough preference does not reach both halves today, which is the bug
this shape removes.** `presetPreference == OutputBypass` is read in the sync
watcher's new-mode block and calls `setOutModeHdBypass()` -- the HD path, always.
The RGBHV path is reached from the standard byte holding 15 and nowhere else, so
a user asking for pass-through on an RGBHV source gets the other one.

**And it dissolves `sourceIsRgbhv()`.** That predicate exists mostly to decide
scaled against bypassed for a source with no preset. Once the user chooses
bypass and `bypassCanBeDisplayed()` says whether the display can show it, there
is nothing left for it to decide.

**11. Steer the ADC PLL.** The band index and its `PLLAD_KS`/`FS`/`ICP` writes
become `Adc`'s, so the group has one owner on every path. **The band moved; the
RATE did not**, and it cannot yet: `getPllRate()` drives the debug pin through
the test bus and counts pulse ticks with FrameSync, which the engine has no
route to. So the sketch measures and the class decides, and the measurement
lands wherever FrameSync does.

**12. Delete `getVideoMode()` and `videoStandardInput`**, which by then have
no readers. *What the byte conflates* above has what each of its fifteen values
carried and what replaced it.

**THE UNIT OF REMOVAL IS THE VALUE, NOT THE FIELD.** Deleting the field means
deleting every reference to every value it ever held, so a change that moves one
value to another -- 3 to 14, say, to close the window
`docs/investigations/two-spellings-of-scaling-rgbhv.md` describes -- entrenches
the byte rather than retiring it, and is undone by this step. What each value
costs, measured against the tree:

| value | means | sites | goes when |
|---|---|---|---|
| 0 | nothing recognised | 7, all but one a write; `standardIsHeld()` is the only reader | a validated measurement replaces the no-sync gate -- step 4 |
| 1, 2 | interlaced SD, NTSC-like and PAL-like | 5 | `SourceStandard` is deleted; its SD arm is live on YPbPr |
| 3, 4 | progressive SD, 480p and 576p | 5 | with it |
| 5, 6, 7 | HD, reached through the HD bypass switch | 3 | the bypass entry points merge -- step 10 |
| 8 | medium resolution: mode detect answers only once `MD_HD1250P_CNTRL` is walked onto the source | 6, three of them a live search in `inputAndSyncDetect()` | the search has a measurement to answer it, and the 110 MHz filter arm has an owner |
| 9 | stable but unrecognised -- `notRecognizedCounter` reaching 255 | 5, one its own `return` in `getVideoMode()` | `SourceStandard` is deleted; its arm already measures |
| 13 | the YPbPr arm of that dispatch | 3 | with the dispatch |
| 14 | an RGBHV source, which is what `sourceIsRgbhv()` tests | the three predicates and `holdStandard()` | `OutputChoice` answers instead -- step 10 |
| 15 | `applyPresets()`'s request to pass an RGBHV source through, and never held | 3, all of them calls | with the bypass entry points |

**NEITHER 8 NOR 9 DIES WITH THE DISPATCH, and neither is a `videoStandardInput`
value.** No site reads the field as 8 or 9 -- which is what makes them look free
-- but both are produced by `getVideoMode()` and both carry register policy that
has to land somewhere first.

8 is a SEARCH, not a classification. `inputAndSyncDetect()` walks
`MD_HD1250P_CNTRL` upwards until `getVideoMode()` answers 8, keeps the value
that worked in `rto->medResLineCount`, and `ModeDetect::applyMedResLineCount()`
replays it on every load. Deleting the value deletes the search's only
termination condition. Its `SourceStandard` arm also moves the analog corner to
110 MHz and sets `PLLAD_ICP` to 6, which is the sharpness judgement below.

9 is `getVideoMode()`'s answer for a source it never recognised but which held a
steady line count for 255 consecutive polls -- the route by which an unknown
source is scaled rather than abandoned. Its `SourceStandard` arm is gone: the
octave drop past 650 lines now runs off `sourceIsTall()` alone, which reads
`SourceMeasurement::measureSourceLines()` twice with a settle between. What put
the clock outside its crossover row was always the count, so every progressive
standard gets the divider that fits rather than only the one Mode Detect failed
to name. `SourceStandard` is down to one reference to the value, in
`isProgressive()`, and that one goes with the byte.

**No bench source reaches the threshold**, the tallest mode the RISC PC offers
being 800x600 at 627 lines, so that branch is proven at host level only. What
the bench does prove is the other side: on 320x256@50 the divider, the crossover
row, the analog corner and the whole capture window are unchanged across it.

**A value carrying two meanings is the one that bites.** 3 is 480p NTSC *and*
the standard a scaling RGBHV load is chosen under, so such a source takes
`SourceStandard`'s progressive arm for the length of a load, and it cannot be
retired by choosing a different number for it.

**14 AND 15 NO LONGER DO THAT.** 14 says the source is RGBHV, which detection
establishes before any output has been chosen; `Tv5725::RgbhvOutput` says
whether a scaled mode is established for it. 15 never reaches the byte at all --
it is what a caller passes `applyPresets()` to ask for pass-through, translated
on the way in by `holdStandard()` and reconstructed by `heldStandard()` for the
callers that compare a detection answer against what is held.

**`Tv5725::PresetLoad` does not survive this step, and it goes with the CONCEPT
rather than with the field.** Keeping it and giving it a byte-free signature
would preserve the idea that a load is chosen by classifying the source, which
is the thing being retired; the field is only how that idea is spelled. Its
instance half -- `enableScalingRgbhv()` and `inputIsYpBpR()` -- is constructed
at exactly one site in the firmware, and the first goes with the byte while
`inputIsYpBpR()` is `adcInputSel == 0` and belongs to `VideoSourceSelection`
beside the rest of that table. Its static half -- the
scaling RGBHV state and the two line-count buckets -- is engine state and an
output question, so it goes to `VideoPath` and `OutputChoice`. The class was
extracted so mode state would outlive the preset tables; it has, and there is no
second job waiting for it.

The byte goes with the function, and nothing holds a standard afterwards. Two
things come out with it.

**`SourceMeasurement::adopt()`** is the only place the engine reads `PLLAD_MD`
as an input. Custom presets are gone and bypass *chooses* 1856 as a literal, so
it becomes `hold(divider)` -- told, not read.

**`Tv5725::SourceStandard` has one caller**, and it is the one piece of this
step that is not mechanical. `doPostPresetLoadSteps()` constructs it from the byte and calls `apply()`, which
branches into an SD, progressive or HD arm. `apply()` also reads `PLLAD_KS` back
off the chip to pass as its own argument, which is the register-as-input
anti-pattern in miniature.

**Its SD arm is live on YPbPr, so it is not dead code**, and its progressive arm
is overwritten on RGBHV. Measured on a Wii at 576i against the RISC PC on the
same build:

| field | RISC PC | Wii | `applySd()` YPbPr branch writes |
|---|---|---|---|
| `IF_HS_Y_PDELAY` | 3 | **2** | 2 |
| `VDS_Y_DELAY` | 2 | **3** | 3 |
| `IF_HS_TAP11_BYPS` | -- | **0** | 0 |

The RISC PC holds the bring-up values and the Wii holds the arm's. On RGBHV the
progressive arm's writes are not in force at all -- `IF_SEL_WEN` reads 0 where it
writes 1, with no other writer -- so **it is dead on one path and live on the
other**, and deleting the class changes the component picture.

**And two more effects survive on every path.**

Everything else it writes has a later owner. `PLLAD_KS` is overwritten by
`Adc::applySampleRate()`, reached from `inputTimingsChanged()` a few lines after
`apply()` returns, which derives the post divider from `divider x lineRate` --
the measurement, correctly. The IF and VDS delays are overwritten by bring-up.

**THAT OVERWRITE IS CONDITIONAL, so the writes are dead on a solved source and
live on an unsolved one.** `SourceMeasurement::applySampling()` returns before
it without a usable measurement, and `Adc::applySampleRate()` skips the
`PLLAD_KS` write entirely on a zero line rate rather than pick a crossover row
by arithmetic on a zero. A load with nothing measured yet is exactly the case
that reaches `apply()`, so its post divider is what the ADC runs on until the
first solve lands.

What is left:

| effect | who needs it | note |
|---|---|---|
| `rto->osr`, the returned oversample | `geometry.inputTimingsChanged(osr)` reads it | a real input to the engine |
| `ADC_FLTR` | nothing else writes it on this path | the analog corner, 40 MHz on both sources |
| the YPbPr luma/chroma delays | the component picture | live, measured above |

So deleting the class means giving those two an owner, and both are **policy
questions with picture consequences rather than derivations**:

- **The wanted oversample.** The arms ask for 4 on interlaced SD ("least
  horizontal detail, so the most room to oversample"), 2 on progressive, 2 by
  default. Keyed to the line rate instead, the bench source at 15.6 kHz would ask
  for 4 where the standard-3 branch currently gives it 2. That changes sampling
  density on the one path that can be judged.
- **The analog filter corner.** 40 MHz is the narrowest the part offers and
  110 MHz is what a line carrying HD detail needs. The corner properly follows
  the sample clock, but where it should step is a sharpness judgement.

Neither should be invented. `docs/capture-limits.md` covers the trade `PLLAD_MD`
makes between sampling density and reaching the end of the line, and the picture
is the instrument for both.

**13. Delete `runSyncWatcher()`**, and `loop()` calls
`inputAcquisition.poll(millis())` alone. `VideoPath::poll()` is gone by then,
so there is one tick in the firmware and one owner of it.

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
    outputModeChanged()    the user picked a different output resolution

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
Wii on YPbPr covers sync on green, interlace and component colour -- which is
the arm `SourceStandard::applySd()` is live on and step 12 has to account for.

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
- `recoverDivider()` and its gates. Measured working, and the trap they escape
  is real. They come out only when a replacement is shown to clear the same
  trap.


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
