# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

### The write floor's artefact is graded, and it has three terms

**Not a binary fault with exceptions.** 78 framings swept on the fixed build,
`VDS_HSCALE` 344 down to 256 on `X720 Y576 C256 F50`, one photograph each,
scored on how far the frequency wedge's duty cycle wanders between neighbouring
bar pairs. That measure orders every framing an eye has judged -- 0.117 at the
clean 320, 0.165 where the bars showed occasional splits, 0.216 where they
split throughout -- so it stands in for the verdict on the markers a camera can
resolve.

| term | size | evidence |
|---|---|---|
| **magnification** | `duty = 0.111 x mag - 0.174` | 60 points at gcd <= 4, r = 0.763 |
| **the write floor itself** | about **-0.034** above it | 8 points, every one below the floor's trend |
| **the interpolation phase period** | up to **-0.148** | residual falls monotonically as the period shortens |

The phase repeats every `1024 / gcd(VDS_HSCALE, 1024)` output pixels, and the
residual after the magnification trend is removed follows it:

| gcd | period | n | residual |
|---|---|---|---|
| 1 | 1024 | 30 | +0.000 |
| 2 | 512 | 20 | +0.002 |
| 4 | 256 | 10 | -0.006 |
| 8 | 128 | 5 | -0.014 |
| 16 | 64 | 2 | -0.008 |
| 32 | 32 | 1 | -0.039 |
| 64 | 16 | 1 | -0.050 |
| 256 | 4 | 1 | -0.148 |

**So "clean only at the multiples of 64" was a binary reading of a gradient.**
320 and 256 stood out because they carry the two largest phase corrections, not
because everything else is broken: 256 scores 0.121 at magnification 4.0 where
its neighbours score 0.240 and 0.212, which is the whole of the effect that
made the scale-clamped zone look like a clean island. The three top gcd rows
are one point each.

**And the old evidence for that rule was contaminated.** Every mark below
`VDS_HSCALE` 308 was taken with `Memory::FetchFloor` pinning the fetch at 150,
so the playback artefact was in the picture as well -- it takes the same
measure to 0.32.

**The floor term is still confounded with magnification.** All eight
above-clamp points lie between magnification 2.98 and 3.05, which is where the
clamp falls on this source, so a step at the clamp and a kink in the curve at
3.05 fit them equally. `Geometry::solveRaster()` sizes the raster per source,
so another source mode puts the clamp at a different magnification and
separates them. That is the measurement this entry now waits on.

**It is a defect, not the source running out of detail.** Correct
interpolation of a magnified source gives a blurry but CONSISTENT upscale: bars
of equal source width come out equal, and an edge lands within half an output
pixel of where it belongs. What the photographs show is bars dividing into two
hairlines and neighbours of equal source width coming out visibly unequal,
which is a sample dropped or repeated. So every term here is wrong sample
SELECTION, including the magnification one.

**Where the wrong samples do NOT come from: the end of the line.** Against the
residual, the distance of `produced` from a whole number gives r = +0.068 and
the samples left unused when the played-out line has consumed what it needs
r = +0.060 -- nothing either way, over 70 points. The phase period gives
r = +0.490. So the line has the samples it needs and the fault is in which one
is chosen, not in running out.

**The damage does not accumulate along the line.** Rounding that builds up
per output pixel has to do more harm at the right-hand end than the left. The
wedge scored in six bins across the line, each bin taken against the same bin
on the framings that resample most nearly exactly, gives a departure of +0.088,
+0.141, +0.163, +0.116, +0.113, +0.083 from left to right -- a hump in the
middle and, against bin index, **r = -0.271** over 70 framings. If anything the
left is worse. So an accumulating phase error is refuted and what is left is a
per-phase one: particular phase values pick the wrong sample, and a longer
period visits more of them.

Two limits on that: the reference is only the three framings with gcd 32 or
more, and the wedge's own bar width changes across the line, so the metric is
not equally sensitive in every bin. The absence of a left-to-right ramp is
robust to both; the middle hump is not.

**And there is no phase-step count that explains it.** A phase with N steps
would make every scale divisible by N exact and leave the rest rounding, so the
improvement would stop once N is passed. It does not: the mean residual keeps
falling through N = 8, 16, 32 and 64, and those sets are nested, so what looks
like a threshold is the same gradient restated. Only three scales in the band
divide by 32 at all.

`Scale::Min` is clean for the phase reason and not because it is the end of the
travel. Once the scale pins at 256 the magnification is exactly 4.0 for every
further press: measured across eight of them, corner residual and overrun
+0.000 at every one while the capture shrank 423 -> 409. That zone is also why
zooming there PANS instead of magnifying -- the scale cannot move, so only the
capture does.

**The corner is refuted from both ends.** `VDS_DIS_HB_SP` was jogged alone at
two write-floor framings with `VDS_HB_SP` pinned at 8: at scale 320 and capture
534, where the write origin is exactly 143, the picture is clean at every value
from 139 to 146; at scale 316, where the origin is 144.0127, it is corrupt at
every value from 144 to 150. An exact framing survives the corner moving four
units off the origin and a fractional one is rescued by no corner value at all.
`sessions/creep_corner-2026092*.json` has the 24 marks.

**`produced` is refuted too.** Capture 534 at scale 320 parts it from the
corner -- corner exactly 143, `produced` 1708.8, memory window odd so the
parity rule is not in play -- and that state is clean.

**Backing the window off the floor does not clear it**, but the jog is not an
above-clamp solve: `VDS_HB_SP` and the corner moved together from 8 to 16 at
scale 316, corrupt at all 12 marks, while a full diff of a floor solve against
an above-clamp one differs in seven fields with the capture and the scale among
them.

**A corrupt verdict is several features of the card at once.** These five are
the easily located ones rather than the whole set, and they are instances of
one thing, so scoring any one of the three a camera resolves stands for the
verdict:

| feature | where | a camera can score it |
|---|---|---|
| a yellow curve | before the colour blocks, on the circle | yes |
| a grey curve | before the frequency wedge starts | yes |
| the wedge | bars split and wander in width | yes |
| vertical black lines | through the label text | **no** |
| a thin vertical line | down the right-hand orange/cyan bracket | **no** |

**The camera under-samples the last two.** `tv-snap` rectifies to 1600 px
across a 1920 px picture, so a feature one or two output pixels wide is below
what it resolves -- the label text reads as broken at a framing marked clean
and at one marked corrupt alike.

**Temporal measurement is at the camera's noise floor.** Forty frames at each
of a clean and a corrupt framing give per-pixel standard deviations that do not
separate -- median 0.51 against 0.50, 7501 pixels over 4 levels against 8062 --
and the map of what moves highlights every edge in both.

**FIXED by bounding the zoom at 3.0x.** `Scale::Min` is 342 -- `1024 / 3` is
341.33, so 342 is the largest magnification at or under 3.0 -- and both axes
read it. Nothing can now solve a scale that pins the memory window at the write
floor, which both sources entered at `VDS_HSCALE` 334.

The rationale is what the zoom is FOR: bringing a source's active picture up to
full screen. That is reached well inside 3.0x, and the range beyond it only
crops further into the source. Measured across the whole zoom, `produced` holds
near 1715 of a 1920 raster from the default framing down to the clamp -- the
picture does not grow, the capture shrinks -- so the bound costs no picture
size, only crop depth.

**It gives up the clean 4.0x zone**, which measures 0.121 and 0.124 on the two
sources because it carries the largest phase correction there is. That zone is
reachable only through the damaged span, so keeping it means keeping the span.

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
`investigations/hscale-tearing-characterisation.md` -- and it is roughly
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
`Scale::Min` -- capture 396, fetch **99**, below both constants that were
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

At `Scale::Min` with a small capture the outward zoom stops being applied:
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

**Settled: there is one name and it is `Scale::Min` 342.** The floor was carried
under two names holding one value, split on the grounds that `Scale::Min` was
"the register's own limits" and the axis floor a picture-quality judgement. There
is no register limit at the bottom to name -- RD-5725-1.1 gives only
`HSCALE = 1024 x in / out` and the field is 10 bits -- so that was one fact
described twice. `Max = 1023` genuinely is the 10-bit field and stays where it
is; all three bounds now sit together in `Scale.h`.

What remains open is WHERE the floor should sit, which no measurement settles.

**4.0x is not a wall: 4.25x works.** Built with the floor passed to the two
`Axis` constructors lowered to 128 and flashed, the engine solves `VDS_HSCALE`
241 at capture 396 and the picture holds -- colour bars, grey bands and the
circle's arcs all clean, with no data corruption. What degrades is sharpness:
the wedge aliases and the label text smears, which is the source running out of
detail to magnify rather than the part failing. So the ceiling is the
picture-quality judgement the entry says it is, and where it sits is the user's
to find by zooming. How far past 4.25x it stays acceptable is unmeasured.

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
`investigations/the-shear-follows-the-produced-widths-parity.md` has the
measurements and the two refuted rules, which must not be reinstated.

**The bias is a bias, not a cure.** Nothing explains why an even width shears, so
anything that later does should be expected to replace it rather than build on
it. Two things are open and each is one bench session:

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

### The picture falls up to two lines short of the vertical active region

The output raster opens the display window at the back porch its `OutputMode`
states, which stopped the picture's landing re-rolling -- 12 trials within
0.52 photo px against 4 controls at 101 px, every trial with the sink dropping
the link and re-acquiring.
`investigations/the-picture-position-is-re-rolled-by-the-sync-pad.md`.

Horizontally the picture fills the active region the mode states, 1396 px of
1396. **Vertically it can still fall short, and by how much follows the
framing.** The encoder treats the whole 1080 as active, so the difference paints
black at the bottom.

Measured on the bench, 640x480@60 on `vga` into 1080p, 24 framings swept by one
vertical zoom step each:

| lines painted of 1080 | framings |
|---|---|
| 1080 | 11 |
| 1079 | 9 |
| 1078 | 4 |

What is left is scale granularity alone. One unit of `VDS_VSCALE` is worth 2.2
to 2.4 output lines at these magnifications, and both the window's far edge and
the memory window's near edge are whole units, so the write ends a fraction of a
line early and the display window closes on the floor of it. Nothing can be
recovered there without either a finer scale or cropping the bottom of the
picture, and cropping is the worse trade: a black edge is visible and one press
away, where a cropped one looks like a fault.

**The whole-step loss is closed.** `Axis::fitToRaster()` bumped the scale a
whole step to clear an overshoot of a fifth of a line, which cost 2.1 lines to
save 0.2 -- the default framing solved 1077 of 1080. The guard measures the
overshoot in whole units now, because `Axis::solve()` closes the display window
on the floor of where the write ends and a sub-unit overshoot is blanked there.
The same framing solves `VDS_VSCALE` 455 against 456 and paints 1080 of 1080.

### The output sync pad is raised only on a source-state transition, so it latches down

Measured on the bench after an OTA flash: no picture at all, with
`PAD_SYNC_OUT_ENZ` **1 in 728 of 728 samples over 12 s** and every other
register perfect -- `DAC_RGBS_PWDNZ` 1, `PLLAD_MD` 1446 against
`STATUS_SYNC_PROC_HTOTAL` 1446, `STATUS_MISC_PLLAD_LOCK` 1, `HPERIOD_IF` 213 at
the 524/60 that mode is due, the raster steady at 1599 x 1124, and the engine
still solving. Writing the bit to 0 by hand restored the picture at once and it
stayed 0 for 639 of 639 samples, so nothing was re-arming it: the hold was
issued once and never released.

`VideoPath::showOutput()` has exactly one caller, and it fires only when
`sourceState_` CHANGES:

    if (sourceState_ != was) { ...; videoPath_.showOutput(sourceState_ == SourceAcquired && ...); }

So the only thing that raises the pad is a transition into `SourceAcquired`.
Anything that lowers it without a following transition -- `Chip::outputDown()`,
the bring-up's own static registers, or an encoder relook whose release pass
does not run -- leaves HSOUT/VSOUT down with nothing left to raise them, and a
register dump cannot tell that state from a healthy one.

**Which of those lowered it here is not established.** The relook release lives
in `VideoSourceAcquisition::poll()` past `if (!detectionPass) return solved;`,
and `DetectionIntervalMs` is 20, so it should run within 300 ms; the freeze read
false and a latched `hasPower_` would have printed `power good`. What is
established is that the pad is a level nobody re-asserts, which is the defect
whichever writer lowered it.

The one-line recovery, which needs no reflash and no bench trip:

    python3 tools/gbsc-pro-hwtest/setfield.py --host <ip> --set PAD_SYNC_OUT_ENZ=0

### A junk line at the bottom of the picture

Intermittent. A thin line of scattered bright pixels below the last row of
picture.

**The column that used to sit at the left edge is FIXED and is not this.** It
was the blanking the capture path writes past the hsync pulse on a doubled line,
taken into the window because `VideoSourceLine::DoubledHeadBlankingUnits` was 17
where the bench needs 20. Crept at `PLLAD_MD` 2200 and confirmed after a flash,
so the encoder acquired the raster from cold: 0.0 against 27.0. It cost no
picture -- the guard takes the source's own blanking, which a full framing
reaches into anyway, and the solve refills the same display window.

The bottom line has not been measured the same way, and the shape that fits it
is still the display window exposing memory the playback stage did not write:
`OutputMode.h` records the same thing at the START of a line above a
magnification the part will not state.

**What to run on it is the step that settled the left column**: move
`VDS_DIS_VB_*` by a known amount on a frozen engine and difference the frames,
which says whether the artefact is the panel's or the part's, and then creep the
capture's own far end rather than the output window.

### The capture tail runs a whole sync pulse past the picture

`VideoSourceLine::lastCapture()` is `units - 2`, and `firstCapture()` is
`lag + headBlanking + (syncAtHead ? syncUnits : 0)`. On a low-active source the
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

The tail that stops where the content does is `units - syncUnits + lagUnits`,
1394 here against the 1492 in force. Subtracting `syncUnits` WITHOUT the lag
gives 1320 and costs 70 units of picture, which is the form that was tried and
reverted.

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

**THE BOARD IS EXONERATED, MEASURED RATHER THAN INFERRED.** The television's
menu is drawn by the STV9426 from `HS_OUT`/`VS_OUT` and keyed into the video at
U13, downstream of the VDS, so it rides the sync timebase while the picture
rides the VDS's counter. Photographed at two landings they move **together** --
overlay -100.78 px and +26.58 rows against the picture's -101.01 px and
+26.43 rows, agreeing to 0.23 px and 0.15 rows, with the camera controlled at
lag 0 and r = 0.998. Video cannot have moved relative to sync inside the
scaler, so the analog frame is identical at both landings and the displacement
is added after it. It does not separate the MS9288A from the television.
`investigations/the-picture-position-is-re-rolled-by-the-sync-pad.md`.

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
trip pulls. `investigations/the-picture-position-is-re-rolled-by-the-sync-pad.md`
carries the measurements and the open candidate, which is that
`EncoderRelookMs` returns the pad 300 ms in, while FrameSync is still steering.

Neither a `PAD_SYNC_OUT_ENZ` toggle nor a source mode round trip re-centres it.

**THE PAD RE-ROLLS THE LANDING RATHER THAN REPAIRING ONE, and a single trial
cannot tell the two apart.** Nine 3 s drops from three different positions at
640x480@60 moved the picture twice: once from the right landing to the left one,
and once from the left landing to a third in the middle, right edges 1444, 1504
and 1545 photo columns. A drop that happens to land somewhere better reads as a
fix, and the next one moves it again.

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

### 640x480@60 is captured too narrow and magnified to fill

`eh` 623 of `ch` 1023 is 61% of the line where the matched raster puts active
video at 80%. The raster match itself is right -- sync duty 11.50% against DMT's
12.00%, inside the 1.5-point tolerance, and `ov` 35 / `ev` 480 exact -- so the
fault is in what the capture window is solved to, not in which raster was
matched.

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
-- the produced picture is wider than the encoder transmits.

### The bottom line of the picture sometimes shows garbage

Observed on the bench at 2026-09-17 across several scaled modes, intermittently:
the last line of the display carries garbage rather than picture or blanking.

Nothing is measured beyond the observation -- which modes, whether it tracks the
capture window's vertical stop, the playback fetch or the output blanking, and
whether it is present in pass-through, are all open. A comparable artefact was
diagnosed before as stale memory read past the end of what was written
(`the-bar-is-stale-memory`), so `IF_VB_ST`, `VDS_DIS_VB_ST` and `PB_FETCH_NUM`
are where to look first.

**Photograph it as a clip rather than a still.** It is intermittent, and a still
that misses it reads as the artefact being absent.

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
4 counts per 25 Hz on a 15.6 kHz doubled line. A rate wobble therefore rewrites
`PLLAD_MD` and re-latches the ADC PLL, which nothing asked for and which the
bench has not been watched for. Undoubled sources are immune where the counter
binds, because the wall does not move with the rate.

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

## Fixed, kept here until the next session has seen them

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

### `Deinterlacer::steer()` gates on `STATUS_IF_VT_OK`

`STATUS_IF_VT_BAD == 0` was proposed as matching the evidence better. The two
are complementary on this bench -- `VT_OK` 1 / `VT_BAD` 0 in 8 of 8 on composite
sync, `VT_OK` 0 / `VT_BAD` 1 in 5 of 5 on separate -- so neither is the wrong
gate on these readings, and the flicker the proposal rested on has not been
reproduced. Open only in that nothing has exercised the gate: the bench RISC PC
is progressive, so the deinterlacer has nothing to engage for even on the
composite leg where the flag lets it through.

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

### A mode change into a taller frame stalls seconds in the field-rate spin

Timed with `SamplingLog` at 25 ms across 311 -> 524: **5.0 s of stall in 21
passes**, individual passes taking 914, 794 and 783 ms. `getSourceFieldRate()`
is a blocking spin with no `yield()`, one field period nominal and up to
3 x 250 ms on retries, and `agreedRate()` wraps that in three more attempts. The
reverse change is 2.89 s.

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

### `applyForScalingRgbhv()` and `applyScalingChargePump()` have no callers

Their only call site went with `loadScalingRgbhvPreset()`. Whether each is a
preset-era put-back that dies with the tables or a behaviour to restore is
undecided. `applyForScalingRgbhv()` overlaps `applyForSyncType()` on
`SP_SOG_MODE` and the overflow protect, so wiring it back as-is would put two
owners on those.

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

## Untried experiments with a known payoff

### The same framing must reproduce at every output resolution

**The framing is stored as PROPORTIONS, so it scales with the raster and must
never clamp.** `Scale::Min` is derived as `raster / maxMagnification`, so the
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
