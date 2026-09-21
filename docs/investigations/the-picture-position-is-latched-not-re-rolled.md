# The picture position is chosen downstream of the board, and it is LATCHED rather than re-rolled

> ## The "re-roll" is REFUTED, and the landing is deterministic
>
> This page was written as `the-picture-position-is-re-rolled-by-the-sync-pad.md`
> and its central word was wrong. **Nothing here ever measured a random draw.**
> What it measured was several landings that differed, and inferred a re-roll
> from the difference. The landing is chosen, and what chooses it is now
> measured.
>
> **The window the picture is shown through takes its origin from OUR blanking
> at the moment the link locks, and then holds it.** Which side holds that
> window -- the board or the encoder -- is NOT settled here; the overlay
> measurement separates them and has not been run on it. Measured on the bench
> unit, automation frozen,
> the origin found by creeping `VDS_DIS_HB_SP` until the panel's left edge
> starts to move:
>
> | | origin |
> |---|---|
> | raster 1790, as found | 420.8, against our window at 425 |
> | raster 1440, as found | 376 |
> | raster 1790, after a `PAD_SYNC_OUT_ENZ` toggle with `VDS_DIS_HB_SP` = **300** | **300.3** |
>
> The origin followed the register to 0.3 units. So two landings that differ are
> two different blanking values in force at lock -- not two draws.
>
> **And it can be pulled earlier but not pushed later.** The same toggle at
> raster 1440 with our blanking at 402, LATER than the 376 already latched, left
> it at 376. Black inside the window it already holds is no cue; content earlier
> than the window is. That asymmetry is what makes a late window a permanent bar
> and an early one harmless.
>
> **Within one acquisition the window does not move at all.** `VDS_DIS_HB_SP`
> swept over 126 units moves the picture 0 px, `r = 1.0000` across the
> right-hand 55% of the frame, with only a strip of blanking changing. So a pair
> of frames taken either side of an acquisition is the ONLY way this ever looked
> random, and the difference between them is the blanking, not chance.
>
> The measurements are in
> [`the-shown-window-is-latched-at-lock.md`](the-shown-window-is-latched-at-lock.md).
> **Do not describe this as a re-roll, and do not treat a landing as unrepeatable
> until the blanking at lock has been ruled out.**


## What this still applies to

**The scaling path, on an output raster that states no standard.** Every mode
`Tv5725::OutputMode` offers now carries one -- CEA-861 for 1080p, 720p, 480p and
576p, VESA DMT for the rest -- and the displacement below does not appear on
them. A sink locking to a mode it recognises places it the same way every time.

**It is not recorded as happening WITHIN pass-through**, and the two claims that
sound alike are different. What is recorded is ~150 photo columns adrift across
a pass-through ROUND TRIP -- an excursion out of the scaling path and back --
which says the mapping does not survive the excursion. It does not say the
picture moves between two acquisitions while pass-through is held.

That distinction decides how pass-through is measured. A blanking edge found
there is a real edge, and a second acquisition is not needed to confirm the
picture did not move underneath it. Re-calibrate across an excursion, not
within one path.

The measurements below stand as taken. What has changed is the raster they were
taken against.

## The finding

The picture lands on one of a small set of positions on the panel, and which one
it lands on changes across a transition. **A full 1536-register snapshot taken at
three of them is byte-identical**, and no write on the part moves it except by
disturbing the output.

**THE BOARD IS EXONERATED, AND IT IS MEASURED RATHER THAN INFERRED.** The
television's own menu is drawn by the STV9426 from `HS_OUT`/`VS_OUT` and keyed
into the video at U13, downstream of the VDS -- so it rides the SYNC timebase
while the picture rides the VDS's. Photographed at two landings the two move
**together**, which they cannot do if the scaler moved video relative to its own
sync. The analog frame leaving the board is identical at both landings and the
displacement is added after it. `#the-overlay-and-the-picture-move-together`.

Measured on `vga`, RiscPC at 640x480@60, output raster 1600 x 1126 at 108 MHz,
forced 100% framing.

## The observable

Photo column profiles cross-correlated against one reference frame, searched
over shift and scale together. The instrument's floor is the same state
photographed twice: **0.00 px at r = 1.000**.

| | shift | scale |
|---|---|---|
| control, same state twice | 0.00 photo px | 1.000 |
| the other two positions | -59.1 and +44.7 photo px | 1.002, 0.998 |
| vertical, all positions | 0 photo rows | — |

No rescale, which rules out the television changing its overscan or aspect
handling.

**The "no vertical component" row does not generalise.** The landing pair with
right edges 1550 and 1450 carries **+26.4 photo rows** alongside its -101 px,
measured with the camera controlled (the surround correlates at lag 0,
r = 0.998, so the rig did not move). Both readings are of real pairs, so the
vertical component is a property of WHICH pair rather than absent; a landing set
described as purely horizontal is describing the pairs it happened to sample.

**Classify by the content's own edges, not by the correlation.** The card's
screen border flips cyan and magenta twice a second, so two frames at one
position differ in their outermost columns and the fit is pulled a few pixels;
the first and last lit column do not care. Measured that way the clusters are
unambiguous, and they separated a fourth value the correlation had folded into
the reference:

| position | output px | content, photo columns | landings |
|---|---|---|---|
| right | +41 | 89..1546 | 4 |
| reference | 0 | 44..1501 | several |
| small left | -7 | 44..1493 | 1 |
| left | -55 | 42..1442 | most |

The left edge saturates at the panel's first painted column, 43, so any position
at or left of the reference reads 42..44 there and is told apart by its right
edge.

**The right-hand position is the good one.** At +41 the whole picture including
both screen borders sits clear inside the panel's 43..1558; the reference just
fits; only -55 clips, losing about 53 output px off the left.

## It is not in the registers

`snapdiff.py --save` covers all 1536 addresses, against the 608 a config dump
reads. Taken at three of those positions:

| comparison | bytes differing |
|---|---|
| reference against the -55 position | **0** |
| reference against the +41 position | **0** |
| reference against another frame at the reference position | 1 (`SP_H_CST_SP`, segment 5) |

`VDS_HSYNC_RST` 1599, `VDS_HS_ST` 0, `VDS_HS_SP` 32, `VDS_HB_SP` 8,
`VDS_DIS_HB_SP` 89, `VDS_DIS_HB_ST` 1583, `VDS_HSCALE` 974, `IF_HB_SP` 72,
`IF_HB_ST2` 1493, `PLLAD_MD` 1494 — all identical at all three, along with
everything else on the part.

## The scaler's own blanking edge moves with the picture

`VDS_DIS_HB_ST` blanks the output raster, so differencing a frame taken at one
value against a frame taken at another makes the difference **the strip that
register blanked** -- a feature of the raster the scaler emits rather than of
the picture carried in it. Taken at two positions on one source, automation
frozen, 640x480@60, the same 220-unit step at each:

| position | picture, photo columns | `VDS_DIS_HB_ST` 1583 -> 1363 blanks |
|---|---|---|
| right | 51..1545 | 1416..1547 |
| left | 40..1445 | 1316..1447 |

The blanked strip moves by **the same 100 columns as the picture**. So the
picture sits in the same place within the raster the scaler emits, and what
moves is where that whole raster is painted. That is the same conclusion the
pass-through calibration reaches further down, measured directly and in both
states inside one window.

## The byte-identity holds when it is CONSTRUCTED, not only when it is found

A position reached by a pass-through round trip differs from one reached by an
ESP restart in 26 config bytes: the HD channel's window and gains, the two phase
adjuster enables and `PA_SP_S`, `SP_H_CST_SP`, `PLL_R`/`PLL_S`, `MD_SEL_VGA60`,
`MADPT_Y_MI_*` and the three polarity autodetect enables. None of them carries
the position. Automation frozen throughout, 640x480@60:

| written | result |
|---|---|
| eleven fields one at a time, each to the far state's value | every one within **0.13** photo px |
| all 26 bytes together, so `snapdiff.py --save` reports **0 bytes differ** over all 1536 addresses | **0.19** photo px |
| then `resetVideoBlocks()` + `ResetSDRAM()` + `restartPhaseAdjusters()`, then `SyncProcessor::reset()` | **0.20** photo px |

So it is not enough to say no two positions happen to differ in a byte: a part
**made** byte-identical to the far position, and then reset through every block
that reaches the output, still paints the picture where it was.

## A pass-through round trip is not a provoker

Twenty-two round trips out of `preferScalingRgbhv` and back, each waited out to
the route bit and photographed, across both a default build and a
`GBS_TRACE_WRITES` one: **the picture moved once**, and that once was the first
round trip after an ESP restart. The other twenty-one sat within 0.53 photo px
of where they started.

A run polling a register through the transition and a run reading nothing until
after the photograph give the same answer, so the deferred-read load inside
`loop()` is not what settles it either.

## It is not the display clock

`/framesync` reports where the Si5351 has been steered to, which no register
dump carries. Across eight round trips:

| | value |
|---|---|
| seed | 133 in 8 of 8 |
| target | 108000000 Hz in 8 of 8 |
| steered to | 107996832 .. 107997000 Hz |

A spread of 1.6 ppm, and it does not sort with position: the lowest reading sits
at the reference position and the shifted frame sits at a mid value.

## It is not the frame buffer restarting

A source mode change runs `holdMemoryBlocks()` and `releaseVideoBlocks()` —
`SFTRST_DEINT_RSTZ`, `SFTRST_MEM_FF_RSTZ`, `SFTRST_MEM_RSTZ`,
`SFTRST_FIFO_RSTZ`, `SFTRST_OSD_RSTZ`, `SFTRST_VDS_RSTZ` — so the playback FIFO
restarts against a capture side on another clock, which is the shape a
line-offset race would have. It is not the cause. Five hold-and-release cycles
with automation frozen and nothing else written:

| cycle | shift |
|---|---|
| 1..5 | 0.00, 0.00, -0.05, 0.00, -0.05 photo px, r = 0.9999 |

## `PAD_SYNC_OUT_ENZ` alone CAN move it, and usually does not

Automation frozen, the source untouched, the only write being `s0_49` bit 2 set
to 1 and back to 0. That bit is a pad output enable — "HSOUT/VSOUT control When
= 0, HSOUT/VSOUT output enable" — so it drives no internal timing and leaves the
analog video output untouched. What changes is whether the two sync pins are
driven.

| drop held for | trials | position moved |
|---|---|---|
| 2.5 s, back to back | 5 | **1**, the first; the next four left it where that put it |
| 0.3, 0.8, 1.5, 2.5, 4.0, 6.0 s, twice each, after a round trip | 12 | 0 |
| 3 s, from three different positions, 640x480@60 | 9 | **2** |

The nine add a third landing and say what the move is: one took the right
position to the left one, and a later one took the left position to a middle
one, right edge 1504 against 1444 and 1545. **The pad re-rolls the landing, it
does not repair one** -- a single trial that lands somewhere better reads as a
fix and is not one. The three right edges are 41 and 60 photo columns apart,
which is the same spacing as the +41 and -55 offsets above.

Only three of those seventeen started from the reference position, and the one
that moved is one of the three. From the -55 position nothing moved it at any
dwell up to six seconds, so the two are not symmetric: **-55 behaves as an
attractor and the reference as the metastable state.** The round trips those
twelve trials were interleaved with landed on -55 ten times and on the
reference twice.

So one write no register dump distinguishes is enough to move the picture a full
position, which puts the choice **downstream of the scaler's output pins**. But
a bare drop of up to six seconds leaves the position alone in sixteen of
seventeen trials, so it is not by itself the trigger a source mode round trip
pulls.

What a round trip does that a bare drop does not is change the output timing —
the 50 Hz leg is a different pixel clock on the same raster, so `encoderMoved_`
is set and `VideoSourceAcquisition` holds the pad away for `EncoderRelookMs`
= 300 ms on each leg. The encoder therefore re-derives against a timing that has
genuinely moved, rather than being asked to re-acquire the timing it already
holds. `encoder-stale-timing.md` records the same asymmetry from the other side:
a stuck encoder needed a 2.5 s drop and 0.4 s did nothing.

## The overlay and the picture move together

The measurement that settles which side of the output pins the choice is made
on, and it needs no probe: the board already carries a second timing reference
that can be photographed.

The STV9426 takes `H-SYNC` and `V-SYNC` on its pins 3 and 2, fed from `HS_OUT`
and `VS_OUT` through 100R, and draws the television's menu from them. U13, a
TS5V330 analog switch, keys that overlay into the video under `FBKG` --
**after** the VDS. So the overlay's position in the analog frame is set by the
sync timebase and the picture's by the VDS's counter, and the two can be
compared in one photograph.

Same menu screen at both landings, correlated band by band:

| | horizontal | vertical |
|---|---|---|
| **STV9426 overlay** | **-100.78 px** (r 0.92) | **+26.58 rows** (r 0.96) |
| picture, rows 400..600 | -101.01 px (r 0.91) | — |
| picture, rows 650..820 | -100.88 px (r 0.90) | — |
| picture, cols 1150..1500 | — | +26.43 rows (r 0.84) |
| **overlay against picture** | **0.23 px** | **0.15 rows** |

The menu box's own edges agree: left -98, right -102, width unchanged at 761
against 757.

**Had the VDS moved video relative to its own sync, the picture would have
shifted and the overlay would have stayed.** They do not separate, in either
axis, to a fifth of a pixel against a hundred. So sync, video and overlay are
mutually locked in what the board emits, the analog frame is the same at both
landings, and the position is chosen downstream of it.

Two things this does NOT establish. It does not separate the MS9288A from the
television, both being downstream -- a second display is what splits them. And
it says nothing about WHY the choice differs, only where it is made.

**The reading it supports** is resampling. The encoder runs off its own
24.576 MHz crystal (Y4), takes a 1600 x 1126 analog raster and transmits
1920 x 1080 -- the television's banner reports that rate -- so it resamples
rather than passing timing through, and where it starts is a phase it picks per
acquisition. That fits the rest: pass-through does not wander because the
source's own 640x480@60 is transmitted as that mode rather than resampled, the
landings are discrete, the displacement is a pure translation, and no register
carries it.

**Why pass-through being stable is not on its own an argument against the
encoder**, which is the inference this measurement replaces: leaving the scaling
path changes what the encoder is given as well as taking the VDS out of circuit,
so the two are not separated by that comparison.

## What is not separated

The MS9288A and the television are both downstream of those pins and either
could be choosing the offset. Nothing on this board can read or configure the
encoder, so the two are not distinguished here; a second display is what would
separate them.

The magnitudes do not settle it either. -52 and +39 IF units have a common
divisor of 13 IF units, and read as 64-bit memory words they are -12.9 and
+9.8, which is close to -13 and +10 but not close enough to carry an argument
through a 2% calibration.

## The -55 position is the one that costs picture

The panel's painted area is photo columns 43..1558, taken from an 800x600
source in pass-through. Read through the calibration:

| position | panel's first painted output px | where the picture's content starts |
|---|---|---|
| reference | 130 | 132 — the first content column is painted |
| -55 | 185 | 132 — **53 output px of content off the left of the panel** |

So the reference position lands the picture's first pixel one column inside the
panel, and the -55 position takes the left-hand border bar and the first
castellations off the screen. The output raster is not what moves; where the
panel's window falls on it is.

## The shift is close to the output back porch

`VDS_HS_SP` is 32 and the picture's first written pixel is at 89, so the output
back porch is **57 px**. The measured shift is 54.8..55.3 px, which is 96% of
it — inside the 2% the photo calibration carries.

`display-window-opens-early.md` measured that the encoder holds its sampling
window fixed relative to the output hsync pulse: 62 px of pulse movement slides
the picture 62 px. If that window is normally placed at the end of the back
porch, a re-acquisition that places it at the end of the PULSE instead moves the
picture left by exactly the back porch.

The +41 px position does not fit the same description.

## One mode does it and two do not, and the odd one out is the one with black in its window

Eight round trips landing on each of two other modes, photographed and
correlated the same way. 800x600@60 is the tight comparison: it solves the
**same raster, the same clock, the same field rate and the same `PLLAD_MD`** as
640x480@60, and differs only in the capture width and the scale.

| | 640x480@60 | 800x600@60 | 320x256@50 |
|---|---|---|---|
| output raster | 1600 | 1600 | 1845 or 1919, alternating |
| `PLLAD_MD` | 1494 | 1494 | 2506..2508 |
| magnification | 1.051 | 1.205 | 1.51 and 1.57 |
| output back porch | 57 | 61 | 69 and 70 |
| black inside the window, left | 43 px | 82 px | — |
| **black inside the window, right** | **108 px** | **0 px** | — |
| distinct positions in 8 round trips | **three** | one, r = 1.0000 in 8 of 8 | one, within 4 photo px |

**The right-hand black margin is the only one of these that sorts with the
symptom.** The back porch differs by four pixels, the left margin is larger on
the stable mode, and the raster, clock, field rate and divider are identical —
while the right margin is 108 px against nothing.

Why 800x600@60 has none is the capture: its line is 1056 source px against
640x480's 800, so the same 1494-unit IF line spans fewer of them, and
`lastCapture()` stops **54 source px INSIDE the active picture** rather than 54
past the end of it. The picture runs to the display window's edge because the
window is cropping picture.

So the leading candidate is that the encoder finds the active region by where
the video stops being black, and a picture with a wide black margin inside its
own display window gives it more than one answer. That makes this the same
defect as
[the-capture-tail-overruns-the-picture-by-the-sync-pulse.md](the-capture-tail-overruns-the-picture-by-the-sync-pulse.md):
the tail that runs into the next line's sync pulse is what puts the black there.

**The prediction, and it is one build:** stop the capture where the content
stops — `lastCapture() = units - syncUnits + lagUnits` — and 640x480@60 loses
its right-hand margin and should stop moving. If it still moves with the margin
gone, the margin is not the variable.

## The black margin is REFUTED, and with it the content-detection reading

The prediction above is that a picture with a wide black margin inside its own
display window gives the encoder more than one answer for where the active
region ends. It is tested from the host by cropping the window to the content
instead of changing the capture, which needs no build: frozen, `VDS_DIS_HB_ST`
1583 -> 1480 removes the margin and nothing else, and the picture does not move
when it goes (0.02 photo px), which is itself the check that only black was cut.

Jogged five times from the deterministic start below, margin gone:

| | right edge | moved |
|---|---|---|
| cropped reference | 1544 | — |
| jog 1 | **1503** | -40.97 px |
| jogs 2..5 | 1503 | held |

**The first jog still moves it the full 41 px with no margin to be ambiguous
about**, so the margin is not the variable and the 800x600@60 comparison sorts
with something else about that mode.

A second measurement refutes the same reading independently, and it is already
in the blanking calibration above: blanking 220 units off the line removes the
strip and leaves the picture **exactly** where it was, neither translated nor
rescaled. A stage deciding where active video is by where the video stops being
black cannot take 220 units of content away and place the remainder identically.

## The output sync PULSE WIDTH is refuted

`VDS_HS_ST` 0 and `VDS_HS_SP` 32 at 108 MHz is a 296 ns pulse, and the reading
that it is normal rested on its equalling 1080p60's hsync rather than on any
measurement. It is testable from the host, frozen, the same way the back porch
is: the pulse has to stay inside the blanking, so `VDS_HB_SP` and
`VDS_DIS_HB_SP` move with `VDS_HS_SP` and the blanking is written first, so
sync is never inside active video at any moment.

One trial is a restart and one jog, from the deterministic start below:

| trial | `VDS_HS_SP` | pulse | first-jog shift |
|---|---|---|---|
| control | 32 | 296 ns | -101.06 px |
| control | 32 | 296 ns | -100.50 px |
| wide | 150 | 1389 ns | -100.90 px |
| wide | 150 | 1389 ns | -100.85 px |

A pulse 4.7x wider, and wider than a conventional proportion of a 1600 px line,
**moves the picture by the same amount**. The spread between the two controls
is 0.56 px, larger than the 0.3 px between control and wide. The widened values
read back unchanged after the jog, so the pulse was in force across it.

It also refutes the black margin a second time and from the other direction.
Moving `VDS_DIS_HB_SP` 89 -> 207 puts **118 px of black in front of the
picture**, where the margin test removed black from behind it, and the jump is
unchanged either way.

## Pass-through does not re-roll the landing

The one configuration where the encoder does not resample is the one whose
picture stays put. Frozen, 640x480@60 in RGBHV bypass, the only write being
`PAD_SYNC_OUT_ENZ` to 1 and back:

| | trials | result |
|---|---|---|
| within 0.15 photo px | 18 | the landing held |
| -2.1 and -2.2 photo px | 2 | one input pixel, the panel painting 640 source px across 1385 photo columns |
| discarded | 1 | photographed during re-acquisition, the sink showing no signal |

Against 100 px on the scaling path from the same provoker, with the same dwell,
the same instrument and the same camera position.

**The provoker is working in bypass**, which is what makes the null meaningful:
the television re-announces the input on every one of the twenty toggles and is
absent only from the untoggled baseline, so the sink genuinely re-acquires each
time and still paints the picture where it was.

**Read the banner, not just the picture.** It reports 640 x 480/60Hz in bypass
against 1920 x 1080/60Hz on the scaling path, so what changes between the two is
not only that the VDS leaves the circuit -- the encoder stops resampling and
transmits the source's mode. That is the difference the landing sorts with.

## The emitted raster is 1080p60 in every term the mode states, and in none it does not

`Mode1080p` is `(1080, 296.2963f, 996.6330f, 5, 36, 4)` -- CEA-861 1080p60
exactly, sync and back porch in nanoseconds so they survive a change of clock.
The encoder takes analog RGB with `HS_OUT`/`VS_OUT` and samples it on its own
clock, so what reaches it is TIMES rather than pixels, and the clock the VDS
counts in is invisible to it. Measured against the standard on the scaling path,
640x480@60, default framing:

| | emitted | 1080p60 | 1080p60 at 108 MHz |
|---|---|---|---|
| line | 14815 ns | 14815 ns | 1600 px |
| sync | 296 ns | 296 ns | 32 px |
| **back porch** | **528 ns** | 997 ns | 108 px |
| **active** | **13833 ns** | 12929 ns | 1396 px |
| **front porch** | **141 ns** | 593 ns | 64 px |
| vertical total | 1125 | 1125 | — |
| vsync | 5 lines | 5 lines | — |
| **vertical back porch** | **-2 lines** | 36 lines | — |
| vertical active stop | 1121 | 1121 | — |

The line time and the sync width already match to the nanosecond. **What does
not reach the chip is the back porch, on either axis.** `OutputMode::solve()`
computes `activeStart` = 140 and `activeLinesStart` = 41 from the mode, and
`VideoPath` keeps only `activeStop` and `activeLinesStop`: the near bound is
computed and dropped. `Axis::maxDisplayWindow()` takes an `activeStart` and
`Axis::maximumCapture()` passes 0 for it, so nothing places the display window
inside the porch the mode declares.

**`VDS_DIS_VB_SP` is 3 while vsync runs 0..5, so the display window opens
INSIDE the vertical sync pulse.** Active video starting within vsync is not a
thing a sampler can resolve unambiguously.

The far bound was a separate decision, on the reading that the encoder generates
its own HDMI blanking and never sees ours so conforming bought nothing. **That
reading is refuted below**: the landings stop moving when the porches are the
mode's, so the analog timing between `HS_OUT` edges is exactly what the encoder
reads.

**The framing is not the cause.** The porches read the same at `/framing/full`
on and off, because the porches are never programmed: `VDS_DIS_?B_SP`/`ST` ARE
the display window, written from the picture's placement, and a porch is
whatever is left either side of it.

## Emitting the mode's own porches stops the landing moving

Built into the firmware rather than written over HTTP, because **a poke cannot
test this**: the landing is a state the encoder settles into at acquisition, and
a register written afterwards makes the timing arrive mid-flight, so the encoder
never acquires the raster under test from cold. Every register result on the
scaling path carries that confound.

`VideoProcessorTimings` now receives the near bounds `OutputMode::solve()`
derives, and the front porch is a time. Measured on the bench, no pokes, the
same restart-and-one-jog protocol and the same instrument:

| | first-jog shift | trials |
|---|---|---|
| window unbounded at the near end | -101.06, -100.50, -100.55, -101.11 px | 4 |
| **window at the mode's porches** | **+0.28, +0.07, -0.01, -0.02, -0.15, -0.52 px** | **6** |

Three row bands agree within 0.03 px on each of the first three, so it is the
picture holding still rather than one band's fit.

**The blanking costs no picture, and a first reading that it did was wrong.**
The encoder maps the active region it is given onto the screen, so widening the
porches loses sampling density and not picture: measured by the outermost luma
gradient the picture is 1336 -> 1346 photo px across the change, unchanged. The
threshold-based extent that first said otherwise was reading the card's screen
border, which flips cyan and magenta twice a second, and was comparing two
different landings as well.

What DID cost picture was the window falling short of the active region the mode
states. `Axis::fitToRaster()` charged the write origin against the room the
picture gets, which is right on the write floor and wrong behind a porch wide
enough to hold it, so the window ran 1366 px of 1396 and the encoder painted the
30 px gap black. Now 1395 of 1396.

**Vertically three lines remain**, 1077 of 1080, and it is scale granularity
rather than the origin -- one unit of `VDS_VSCALE` is 2.37 output lines at that
magnification. `known-issues.md`.

## The provoker is deterministic when it is conditioned properly

A rate quoted over round trips hides the structure, because the landings are
attractors: 1503 held through 8 pad toggles and 4 VDS resets, and 1444 through
6 pad toggles. What moves is the state reached by an ESP restart.

| step | outcome |
|---|---|
| `/restart`, then `/sc?B`, then `/framing/full?on=1` | right edge **1544**, 4 of 4 |
| any one output disturbance after it | leaves 1544, 3 of 3 |
| a further disturbance from 1503 or 1444 | 0 of 18 |

So a trial is a restart and one jog, about a minute, and it is deterministic --
against the 2-in-8 a source mode round trip was budgeted at. The disturbance can
be a `PAD_SYNC_OUT_ENZ` toggle, a `SFTRST_VDS_RSTZ` hold and release, or a
pass-through round trip; all three behave the same, which is also why none of
them separates the VDS from what is downstream of it.

## What the sink reports, and why it keeps the question open

The television's own banner reads **1920 x 1080/60Hz** while the scaler emits
1600 x 1126. So the encoder is resampling rather than passing a raster through,
and a resampler chooses an origin. **The encoder does NOT do the same mapping
job in pass-through**, which is the comparison this used to rest on: measured in
one session on one sink and one cable, the banner reads 640 x 480/60Hz there and
1920 x 1080/60Hz on the scaling path. So it transmits the source's own mode and
leaves the scaling to the television, and the configuration that does not
resample is the configuration whose landing does not move.

The two are separated by one measurement and not by any register: HSOUT against
the start of active video on the analog output, taken at two landings. A VDS
blanking edge is the marker to use rather than picture content, since
`VDS_DIS_HB_ST` puts a hard step at a known column. At 108 MHz one output pixel
is 9.26 ns, so the landings are **~380 ns** apart -- a division at 200 ns/div.
A move of `VDS_DIS_HB_ST` by 100 columns must read 926 ns, which calibrates the
measurement before it is trusted.

## 320x256@50 does not do it at all

The back porch is not a constant: `VDS_HB_SP` is 8, the write start is
`VDS_HB_SP + 55 + 25 x magnification` and `VDS_HS_SP` is 32, so

    output back porch = 31 + 25 x magnification

Eight round trips landing on 320x256@50, photographed and correlated the same
way:

| | 640x480@60 | 320x256@50 |
|---|---|---|
| magnification | 1.05 | 1.51 and 1.57 |
| output back porch | 57 | 69 and 70 |
| output raster | 1600 in 13 of 13 | **1845 or 1919**, alternating |
| distinct positions in 8 round trips | three | **one**, every frame within 4 photo px, r >= 0.9986 |

So the mode with the wider back porch is the stable one, and the mode whose
solved raster is itself unstable is the one whose picture does not move. That
is the opposite of what a race in the solve would give, and it leaves the back
porch as the difference that tracks the symptom.

It also settles a side question: a raster changing from 1845 to 1919 — 4% more
line — moves the picture neither in position nor in scale, so the panel maps the
active region it is given rather than counting pixels.

**A 57 px back porch is short.** A 1600 px VESA-style line carries 150..200,
and it is short here only because `VDS_HB_SP` is a constant 8 and the rest is
pipeline lag. If what the encoder needs is a back porch it can find active video
after, then raising `VDS_HB_SP` so the solved raster carries a conventional one
is the fix.

**It cannot be tested from the host.** A write after the solve moves the picture
without making the encoder look again, and the solve overwrites it on the next
round trip, so the wider porch has to be in force AT acquisition — which means a
build. The cheap form is to give `Axis` a minimum output back porch and let
`VDS_HB_SP` carry it, which costs picture only where the raster has no room for
it.

**What would refute it before spending a build:** a third mode whose back porch
falls between 57 and 69. If the instability tracks the porch there is a
threshold between them; if 800x600@60 comes out at 60-odd and is stable, the
porch is not the variable and the difference is the field rate or the raster.

## The clock is not still settling either

The obvious form of a race — the pad returning at `EncoderRelookMs` = 300 ms
while FrameSync is still steering, so the encoder acquires against a line time
that is still moving — is **refuted**. `/framesync` polled every 0.5 s through
the whole transition reports **one distinct value across 30 seconds**, first
sample onwards.

There is nothing for it to converge to: `Geometry::solveRaster()` sizes the
raster so both modes land on the same 108 MHz seed, so 320x256@50 runs
1916 x 1126 and 640x480@60 runs 1600 x 1126 at the same pixel clock. The
encoder is re-acquiring a genuinely different line rate — 56.4 kHz against
67.5 kHz — but not a moving one.

## What this refutes

`docs/known-issues.md` carried the sampling clock as the hypothesis, on the
evidence that `PLLAD_MD` takes different values on one unchanged source. At
640x480@60 it does not: `PLLAD_MD` is 1494 at all three positions, with zero
bytes differing anywhere on the part, so the divider cannot be the carrier
here.

It also recorded that a full dump either side of a round trip had never been
taken. It has now, three times, and it differs in nothing.
