# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

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

The horizontal capture window opens 174 units earlier on composite sync than on
separate sync, and the picture sits right on screen. RISC PC on `vga` at
800x600@60, one cable, one raster, sync type the only variable:

| field | separate | composite |
|---|---|---|
| `IF_HB_SP2` | 294 | **120** |
| `IF_HB_ST2` | 1384 | **1210** |
| `STATUS_SYNC_PROC_HLOW_LEN` | 176 | 173 |
| `STATUS_SYNC_PROC_HSPOL` | 1 | 0 |
| `SP_HS_INV_REG` | 1 | 0 |

Same width, and a return to `SYNC 0` gives 294 exactly. Everything else in the
placement chain is identical -- both scales, both memory windows, both display
windows, both output sync pulses, the vertical pair and the divider.

**The 174 is one whole pulse, taken from the polarity bit.**
`VideoSourceLine::forDuty()` gets its origin end from `HsyncPulse::syncAtHead()`,
which is a copy of the polarity `normalisePolarity()` read -- and on csync that
bit reports the signal arriving BEFORE the separator, where the separator
regenerates H. The duty is not the variable: 176/1438 and 173/1438 are 0.122 and
0.120, both accepted, so neither state is on `FallbackDuty`.

**NEITHER VALUE IS CORRECT, WHICH IS WHY A CORRECTED BIT IS NOT THE FIX.**
Frozen, with the separate-sync window forced onto composite, the picture moves
too far the other way -- black at the right and the leftmost castellation column
cut, where its own window leaves black at the left. Both bands are roughly equal
by eye, about 180 and 190 px of a 1500 px picture, putting the truth near the
midpoint of 120 and 294 -- about half a pulse, at neither end.

What would settle it: creep `IF_HB_SP2` from 120 upward with automation frozen,
one unit a press, width held, and read the boundary off the picture.
`creep_window.py` is the pattern. If the answer is the midpoint, a separator
phase shift is the explanation and no choice of pulse end reproduces it.

`investigations/the-composite-capture-window-sits-between-two-wrong-values.md`

### The sample-clock group has two writers, and they are the same function twice

`PLLAD_MD`, `IF_HSYNC_RST` and `SP_RT_HS_SP` are one quantity in three
registers, and two functions write all three in the same order:

| | |
|---|---|
| `VideoPath::applySampling(divider)` | `Adc::applySampleRate` -> `writeLineCounter` -> `writeRetimeStop` |
| `gbs-control.ino`'s `applyScalingSampleClock(divider, oversample)` | the same three, same order |

Neither knows about the other. The sketch copy is reached from the serial
command that sets the sample clock by hand, so a divider applied that way does
not go through the engine's own path and nothing reconciles the two afterwards.

It is the standing target rather than a new fault -- the sketch is supposed to
end up writing no registers at all -- and it is recorded here because holding
the line counter on `InputFormatter` made the duplication visible: both copies
now write the same held state, so a divergence between them is a divergence in
what the block believes it wrote.

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

### The sync pad returns on a fixed delay, so the sink can lock to a window a later solve moves

The sink fixes its active window WHEN IT ACQUIRES and holds it until it acquires
again, taking the origin from our blanking at that moment --
`investigations/the-shown-window-is-latched-at-lock.md`. It latches on the
BLANKING EDGE rather than on the first content: a black border panned into the
display window survives a verified re-acquisition unmoved, where locking to the
first non-blank sample would have jumped the window past it. The origin follows
the DISPLAY window because `VDS_BLK_BF_EN` gates which samples reach the DAC --
set, the final composite blank `(dis_hb|dis_vb)` forces the blank value over
whatever the playback stage is fetching, so `VDS_DIS_HB_SP` is where valid data
starts. **How the sink distinguishes that edge is open**: blanking and captured
black are indistinguishable on the panel, both flat at 37.5, and a pedestal of a
code or two would be crushed by the television. That is a scope question.

**`serviceEncoderRelook()` returns the pad `EncoderRelookMs` after the move that
took it away, whatever has happened since.** 300 ms is inside the window where
the raster is still being solved and FrameSync is still steering, so the sink is
handed a window to lock that a later solve then moves. Nothing corrects it
afterwards: a later solve that moves the window without moving the raster arms
no re-look at all, because `VideoPath` arms `encoderMoved_` on the horizontal
total, the vertical total and the field rate and on nothing else.

Measured at 640x480@75: the sink held 376 while the engine had solved 402, a
26-unit bar down the left that no pad toggle at 402 would shift, because the
window can be pulled earlier but not pushed later.

**The fix is to return the pad on QUIET rather than on a delay** -- restart the
wait whenever the timing moves again, so the pad comes back once and the sink
locks once, on the settled window. What stops that being a small change is that
it moves acquisition timing, and a longer hold is a longer dark panel on every
mode change; it needs checking against a cold boot before it ships.

**`EncoderRelookMs` is also the wrong SHAPE of constant** while `encoderMoved_`
ignores the window: two solves that agree on the raster and differ on the
blanking need a re-look and get none.

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

### The absence run has a branch that can never end it

`SourceAbsence::undecided()` is a no-op by construction: detection finding
nothing while a signal IS reaching the sync processor is neither evidence, so
the run neither advances nor ends. The teardown is reachable only through
`missed()`.

So a source that keeps something on the test bus while detection cannot claim it
stalls **indefinitely**. Measured once on `ypbpr`: `state: absent` across 150 s
of polling, holding the previous source's solve throughout (`cv` 628 at 37879 Hz
on a 525-line source), with `SP_VTOTAL` 97, `HSACT` 0 and `PLLAD_MD` still on
`vga`'s 1438. A manual `/sc?~` cleared it in 6.2 s; nothing on the board would
have.

Waking the source did not clear it -- the stall outlasted the source returning
by 45 s -- so this is the engine rather than the source.

**What is not established is which branch ran**, because `SYNC_EVENT` needs
`GBS_SAMPLING_LOG=1` and the fault was caught on a default build.
`SamplingLog::event()` also de-duplicates identical consecutive events, so a
ladder repeating one branch prints once and then goes silent: a quiet console is
what this fault looks like, not evidence against it.

Two `ypbpr` acquisitions in eight have since failed to complete inside 50 s on
an instrumented build, which is the same shape and has not been tied to this.

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

**What has no automatic exit is the trigger.** `VideoPath` arms `encoderMoved_`
only when a SOLVE moves the horizontal total, the vertical total or the field
rate, so the re-look fires on a mode change and on nothing else. A unit left
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
carries the measurements and the open candidate, which is that
`EncoderRelookMs` returns the pad 300 ms in, while FrameSync is still steering.

Neither a `PAD_SYNC_OUT_ENZ` toggle nor a source mode round trip re-centres it.

**THE PAD MOVES THE LANDING RATHER THAN REPAIRING ONE, and a single trial
cannot tell the two apart.** Nine 3 s drops from three different positions at
640x480@60 moved the picture twice: once from the right landing to the left one,
and once from the left landing to a third in the middle, right edges 1444, 1504
and 1545 photo columns. A drop that happens to land somewhere better reads as a
fix, and the next one moves it again.

**THE THREE LANDINGS ARE NOT A DRAW.** The encoder latches its window origin
from our blanking at the moment it locks -- a toggle with `VDS_DIS_HB_SP` at 300
put the origin at 300.3 -- so a landing is whatever the blanking was when the
pad returned. `EncoderRelookMs` returns it 300 ms in, **while FrameSync is still
steering**, so the value latched is one taken off a window that has not settled.
That is the leading explanation for three landings at one nominal framing, and
what would settle it is holding the pad away until the solve is quiet and
counting the landings again.
`investigations/the-shown-window-is-latched-at-lock.md`.

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

Two things that are NOT the fault: the capture window, which matches the mode's
published active region to the unit (640x480@75 reads `oh 292 / eh 1017` of
`ch 1335` against DMT's `(64+120)/840` and `640/840`); and `EngineCeilingHz`,
which buys room at 129.6 MHz -- a raster of 1728 at 75 Hz clears the bound -- but
raises the rate at which the same mismatch bites rather than removing it.

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

### The 450 ms hsync wait in detection never waits

The loop is `while (millis() - timeout < 450)`, and every path through its body
returns -- the final `return 0` sits inside it. So it runs exactly one
iteration: `STATUS_SYNC_PROC_HSACT` is read once, 10 ms after entry, and a pass
that misses it returns 0. On that answer `inputAndSyncDetect()` can call
`goLowPowerWithInputDetection()`, which powers the DAC down and runs
`setResetParameters()`.

Measured across five switches: the first pass after `/input` always misses hsync
and returns 0 in 13 ms, and the second pass 0.7 s later takes the branch. So the
budget the code states is not the budget it applies, and whether the chip is torn
down rests on a single sample of a status bit taken just after the mux moved.

`goLowPowerWithInputDetection()` announces itself with `bootLogPrintf`, which
writes to `Serial` and not to `SerialM` -- so **it never reaches the websocket
console**, and a teardown is invisible to every instrument a session can reach
remotely.

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
