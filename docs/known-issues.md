# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

### The default framing loses the source's outermost COLUMN on every mode

**`PATTERN CARD`'s one-pixel green frame does not reach the emitted frame at the
left, on 28 of 28 stock AKF50 modes.** It reaches it at the right, at the top and
mostly at the bottom. Measured with `card_edges.py` against the USB capture:
`across` reports "one edge: the window is shifted off the picture" on 26 of the
28, and `down` reports flush or two columns on most.

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


**The measurement's own order is ruled out.** The concern was that
`card_edges.py` changes the mode, which re-acquires, and THEN resets the framing,
so where a framing was stored for that key the sink placed its window before the
framing moved. Resetting first and re-locking with `gbs_unit.mode_round_trip()`
does not move the verdict: each order repeats exactly over two runs, and the two
orders agree on all five usable modes. What the test reports is the window the
engine solved.


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


### Sync on green does not follow the source until the ladder's SOG rungs run

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

**The rungs that help are all gated behind `FirstAcquisitionGraceMs`**, which is
15 s, so the SOG floor and the coast window -- the two settings that decide
whether the sync processor follows a sync-on-green source -- cannot be reached
any earlier however wrong they are. The engine is waiting for a recovery to do
what the selection should have configured.

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

### `/preferencesv2.txt` keeps a stale tail, because the save does not truncate

The file is **51 bytes** where `saveUserPrefs()` makes exactly **39 live
one-byte `f.write()` calls** — no loops, no multi-byte writes — through
`LittleFS.open("/preferencesv2.txt", "w")`.

**The save works; the truncation does not.** Toggling `preferScalingRgbhv` with
`/sc?K` flips byte 10 from `0` to `1` and the preference survives a restart,
while bytes 39..50 come back byte-identical across the save. So 39 bytes are
written over a 51-byte file and the 12 beyond them are left from whatever wrote
it longer.

**The live cost is small but real.** Nothing reads past byte 38, so the stale
tail is inert today — until the preference set grows back into it, at which
point a boot reads another firmware's bytes as settings. And
`test_firmware.py::test_the_reserved_preferences_byte_holds_its_place` and
`::test_preferences_survive_a_round_trip` both fail on it, asserting
`PREFS_BYTES` 39 against the 51 they read.

**What is not established** is why `"w"` leaves the tail. The boot gate is
`f.size() >= PREFS_BYTES`, which 51 passes, so nothing refuses the read, and
`prefsAreSuspect` is not involved — a suspect boot would have refused the write
that byte 10 proves happened. Truncating explicitly, or writing the length the
reader expects, is the obvious repair and neither is tried.

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
raster left at 1125 lines with `/preferencesv2.txt` byte 0 already `'3'`
(720p): the console printed nothing for the request where an honoured one
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

**So freeze before going to the bench.** What this fault shows frozen is the
source counted exactly, and that is enough on its own: a count that matches the
source over repeated samples, with `STATUS_SYNC_PROC_HTOTAL` holding the
divider, says the signal arriving is intact whatever else is true. The HC32
fault has not been read frozen -- its entry is written from the ladder running --
so the pair has been separated from this side only.

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
group and the sync-type decision all read exactly what a healthy unit reads --
`SP_SOG_MODE` 0, coast 0/0, `SP_H_PULSE_IGNOR` 255, divider correct and latched,
`own V sync: yes` probing to separate H/V. The engine is measuring faithfully;
what it is measuring is half a signal.

**The discriminator is the other input.** `ypbpr` acquiring on the same board at
the same moment -- 525 lines at 31468 Hz, held over five samples -- is what
separates a board fault from a signal-path one, and it costs one `/input`
request. Reach for it before any firmware hypothesis.

**It does not separate this from the livelock above, and `/freeze?on=1` does.**
`ypbpr` acquires in both, so a board proven good still leaves the two open.
Frozen, this fault has no horizontal edges and the livelock counts the source
exactly -- so take that reading before concluding the HC32 needs power.


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
| horizontal | `VideoSourceLine::SeparatorOriginPerThousand` | 70 | a fraction of the LINE |
| vertical | `VideoSourceLine::SeparatorFrameLeadLines` | 16 | a count of the source's LINES |

Measured on one cable and one raster with the sync type the only variable:

| mode | line | separate - composite | fraction | frame rows | source lines |
|---|---|---|---|---|---|
| 320x256@50, doubled | 1100 | 76.5 | **6.96%** | 61 | **16.6** |
| 640x480@60, undoubled | 1446 | 103, 109 | **7.1%, 7.5%** | 34 | **15.2** |

Horizontally a count fitted to either mode is 40% wrong on the other;
vertically a fraction is 80% wrong. The two axes need not agree -- one is a
delay through a PLL locked to the line, the other through whatever counts lines.

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

**THE COST IS GONE EVEN THOUGH THE FAULT IS NOT.** With the reference pair at
1400 undoubled the first ACCEPTED sample is the correct one, so `ypbpr` solves one
divider per selection instead of three and no line counter is refused at all.
`Adc::BringUpDivider`.

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

### `test_capture_origin.py` compares a live sync width against a latched one

`VideoSourceLine::forDuty()` takes `ceilf(units * duty)` from the
`STATUS_SYNC_PROC_HLOW_LEN` reading the engine held **at solve time**, and the
test reads the register **now**. One ADC sample of drift between the two puts
the expected first capturable unit one out, and the test reports a defect that
is not there.

Measured at 320x256@50, `PLLAD_MD` 2200, `IF_HSYNC_RST` 1100: `HLOW_LEN` reads a
steady 156 over six samples, which is 79 units, while the engine reports a first
capture of 100 -- the 78 that 155 gives. `/sc?U` re-solves from the source as it
reads now and both tests pass, `capturableOn` moving 999 -> 998.

So a failure here is only a finding if it survives a re-solve. The fix is for
the test to take the engine's own reading rather than a fresh one, which
`/geometry` does not currently publish.

### Three hardware tests carry their own copy of a firmware constant

`test_if_head_blanking.py` declares `LINE_DOUBLE_RESET = 272` where
`InputFormatter::LineDoubleReset` is **160**, so
`test_head_blanking_follows_the_scan_mode` fails on the bench's line-doubled
source: `IF_HBIN_SP` reads 160 and the test wants 272. The firmware moved the
doubled path's origin and the copy did not, which is the hazard a copied fact
carries -- the test reads as a firmware defect and is a stale constant.

`test_capture_origin.py`'s two failures are beside it and are NOT the same
thing: the first capture it computes is **3 units** past what the engine reports
(101 against 98, `HLOW_LEN` 156, `PLLAD_MD` 2200), where the entry below
attributes a **one** unit disagreement to a live sync width read against a
latched one, and `/sc?U` does not clear it. Which of the two numbers is right
has not been measured.

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

**What is left is vertical.** With the clock right the field-rate measurement
reads 3937.58 Hz against a line rate of 31500, which is the line rate over eight
-- a line-derived signal reaching a measurement that wants vertical sync.

**The sync-type probe is NOT why**, and an earlier reading of this row saying so
is withdrawn. Forced to re-probe, it answers correctly on both sync types of the
bench source. What puts a composite source on the separate-sync configuration is
that a sync-type change arms no re-probe, so the held answer stands.
`investigations/a-stale-sync-type-leaves-a-composite-source-uncoasted.md`.

Three findings from the same window:

- `SP_H_PULSE_IGNOR` is found at 255, which is `OwnVsyncPulseIgnore`, the
  separate-sync value, inside a `SP_SOG_MODE` 1 configuration: two writers
  disagree. Moving it off 255 takes `VTOTAL` from 498 to 521 at once, and 107,
  51, 16 and 2 are indistinguishable from each other.
- The separator level matters more than the coast window. `ADC_SOGCTRL` 1 gives
  a `VTOTAL` wandering 534..543; 4 and above give a steady 522. The recovery
  ladder walks that level down, so it manufactures the instability it is
  climbing to fix. Coast pairs of 7/3, 9/9, 3/3, 1/1 and 0/0 move it by at most
  one line; 16/16 destabilises it.

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

### The defaults signature cannot tell a wiped preferences file from a chosen one

`test_firmware.py::test_bootlog_reports_the_preferences_read` fails on the bench
unit, and the guard rather than the unit is what needs deciding.

It reads `presetPreference=5 frameTimeLock=0 suspect=0` and asserts that the
pair 5/0 must never appear with `suspect=0`, on the grounds that 5/0 is the
defaults signature and a clean read should not produce it. But **`Output1080P`
IS 5 and it is also `OutputChoice::ScaledDefault`**, and `enableFrameTimeLock` 0
is the default too -- so 5/0 is equally what a unit deliberately set to 1080p
with frame time lock off holds. The signature cannot separate the two.

What the file actually holds, read at boot: 39 bytes of 39, `plausible=1`,
`first=[35 30 41 30]` -- ASCII `5`, `0`, `A`, `0`. So byte 0 genuinely is 5,
byte 1 is 0 and the slot is the default `A`. `SeleInputSource` reads 2, from the
same block of the same file, so the read is faithful rather than defaulted.

`loadDefaultUserOptions()` does not touch `SeleInputSource` -- it is a global of
its own, not part of `userOptions` -- so a saved input cannot be used to prove
the rest was not defaulted.

What would settle it: set a preference that is NOT the default, cold boot, and
see whether it survives. If it does, the file is sound and the guard needs a
discriminator that is not a value every correct unit may hold.

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

## Fixed, kept here until the next session has seen them

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

## Costs time rather than correctness

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

### The OSD and the web status report Bypass at 576p

`presetIdFor()` gives `Mode576p` the code `0x07`. The OSD's resolution display
tests `0x04` for 720x480 and `0x14` for 768x576 and falls through to an `else`
that draws **Bypass**; the websocket status switch has no `0x07` either and
sends `'0'`, the default the web UI renders the same way. So a unit scaling
correctly to 576p says it is passing through, while the registers say
`DAC_RGBS_BYPS2DAC` 0, `OUT_SYNC_SEL` 0 and a solved raster of 2070 x 625.

**It reads as a fault in the video path and sends a session after one.** The
board is the only instrument that reports which route is in circuit, so a wrong
answer there costs whatever is spent before the registers are read directly.

Two encodings for one fact are live: `loadComputedPreset()` is called with the
old table id `0x14`, and `changeOutputResolution()` overwrites it with
`presetIdFor()`'s `0x07`. The id's high nibble used to be the source standard,
which is why 480p and 576p have both `0x04`/`0x14` and `0x07` in circulation.

`RgbhvOutput` reports bypass on a unit that is scaling, below, is a second
route to the same wrong answer by a different mechanism -- that one is
`printInfo()`'s `m:15` from `RgbhvOutput::isScaling()`, this one is the id the
two display sites switch on. They are independent and both have to go.

**The fix is for the OSD and the status to ask `VideoPath::outputMode()`**,
which is the single owner of what resolution is being emitted, and for
`presetIdFor()` and the table-shaped id to go with it.

**`rto->presetID` cannot be retired on its own.** `/preferencesv2.txt` is
positional, so the field's meaning is pinned by the file layout, and the bypass
sentinels `PresetHdBypass` and `PresetBypassRGBHV` share the field with the
resolution. **TODO: replace `/preferencesv2.txt` with a named-key preferences
file, then retire `presetID`.** Until then the reporting can be corrected
without the id going, by reading the output mode at the two display sites.

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

## The frame time lock arms itself on a cold boot with the option off

**Measured once, on a true cold boot, and not explained.** `/framesync` came up
`ready:true` and the console printed corrections at the design cadence, so the
option was enabled in RAM -- and the boot log says it was not:

    PREFS: LittleFS.begin()=1 at t=1737ms (took 1ms)
    PREFS: attempt 1 t=1738ms open=1 size=39 got=39 plausible=1 first=[35 30 41 30]
    PREFS: loaded presetPreference=5 frameTimeLock=0 slot=65 SeleInputSource=2 suspect=0
    BOOT: reason='External System'

**The read was clean**, so the power-up race on the SPI flash is refuted here:
39 bytes asked for and 39 got, plausible, not suspect. `/preferencesv2.txt`
reads `50A000000111` and index 1 is `enableFrameTimeLock`, verified against the
WRITE order in `saveUserPrefs()` rather than assumed.

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
`/framing.txt` is silent the way a short `/preferencesv2.txt` read is.

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
