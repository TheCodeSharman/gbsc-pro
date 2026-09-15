# Known issues

Open defects and unsettled questions, each with what was measured and what would
settle it. A row leaves this page when the behaviour is fixed or the question is
answered, and the evidence goes to `investigations/`.

**This is not a work queue.** The refactor's order is
`video-source-acquisition.md`; this page is what is wrong with the machine
regardless of which step is in flight.

## Reaches the picture

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

Measured on the bench panel as a translation rather than a scale change: the lit
width is 892, 953 and 968 columns across three frames while the left edge moves
159 -> 5.

**What has been checked is the solve, and it is unchanged** -- `VDS_HSYNC_RST`
1915, `VDS_VSYNC_RST` 1124, `VDS_HSCALE` 546, display window 110..1899,
`/geometry` `oh 51, eh 954, ov 38, ev 582`. `VDS_HS_ST` 0 / `VDS_HS_SP` 32 is
the correct derivation for this raster, `syncNs x clockHz` giving
296.30 ns x 108.03 MHz = 32, so the output sync placement has not drifted either.

**It must not be filed against the HDMI encoder.** Nothing on this board can
read or configure the MS9288A, so that attribution cannot be tested and closes
the question instead of advancing it; the round-trip form of it is already
refuted in
`investigations/leaving-bypass-needs-a-count-the-divider-cannot-give.md`. **The
position of a picture is a register.**

**The hypothesis is the sampling clock, and it has evidence.** `PLLAD_MD` takes
different values on one unchanged source -- 2250 and 2206 both measured on the
RiscPC at 320x256@50 -- and `IF_HSYNC_RST` tracks it, 1125 against 1103. The
capture's framing constants do not: `IF_HBIN_SP` 272, `IF_HB_SP` 72 and
`IF_HB_ST` 2 read identical across both. So a capture positioned at a fixed
count of IF units begins at a different fraction of the source line depending on
the divider, and the framing arithmetic is not equivalent across two clock
settings and two output rasters -- 2022 x `VDS_HSCALE` 526 against 1915 x 546
here.

What has NOT been compared is a full dump either side of one round trip -- only
the fifteen fields above -- so `snapdiff.py --save` before and after is the next
move, covering the 928 addresses a config dump leaves out.

Neither a `PAD_SYNC_OUT_ENZ` toggle nor a source mode round trip re-centres it.

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

### A composite-sync source in pass-through gives no signal

640x480@60 on `vga`, one cable, one mode, the sync type the only thing moving --
`SYNC 0` against `SYNC 1` on the RISC PC. Separate sync passes through and fills
the panel, sharp and correctly coloured. Composite sync gives no signal, and the
sink reports no signal rather than naming a stale mode.

`PLLAD_MD` is 2039 against `STATUS_SYNC_PROC_HTOTAL` 2039 on both, so the
divider latched. What differs is the vertical count: 524 on separate sync, and
98 / 235 / 884 / 1852 on composite, never the 524 the mode is due.

**The separator level is refuted.** Held at the default 13 the count still never
reaches 524 and the panel stays dark, and the same source on composite sync
displays on the SCALING path with the level walked to 24. So what fails is the
sync processor's count on the pass-through route, not what the separator is fed
at.

Would settle it: a `/testbus` sweep on each sync type in pass-through, which
says which sync-processor stage stops carrying vertical sync.

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
default framing at `scaleMin` 500, and the floor is now `raster / 4`, leaving
315 units of travel rather than none. 129.6 MHz is already measured as working
and sharp, and buys a third more horizontal resolution. Not tried.
