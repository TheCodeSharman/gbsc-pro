# Register facts that are not obvious

- **THE ENGINE WORKS IN THE SOURCE'S FRAME, NOT IN WHAT `STATUS_SYNC_PROC_VTOTAL`
  READS.** That register counts from zero, so the bench RISC PC reads 311 and
  the Wii at 480p reads 524 where the frames are 312 and 525.
  `SyncProcessor::frameLines()` undoes it at the boundary and nothing
  downstream corrects again -- so the console says `measured: 312 lines`, a
  source key is `312@50.08/695??`, and `/framing.txt` stores the figure a mode
  file states. **A `+ 1` added to a count anywhere else is a second copy of a
  correction already made**, and there were eight of those before it moved.
  `docs/video-source-acquisition.md` has which other registers are zero based
  and where each is undone; `SamplingLog` and the sketch's diagnostics
  deliberately still print the register.
- **`STATUS_SYNC_PROC_HTOTAL` echoes `PLLAD_MD`.** The sync processor counts in
  ADC clocks and the ADC PLL is locked to HSync with `PLLAD_MD` as divider, so it
  reports your own setting back. Never key anything on it — **except as the one
  witness that the divider was LATCHED**, which is the next entry.
- **Reading `PLLAD_MD` back does not tell you what the ADC is doing.** It is
  loaded into the PLL by a *rising edge* on `PLLAD_LAT`, so between the write and
  that edge the register reports the new value while the chip still clocks at the
  old one. Anything that reads it back to derive something else is asking a
  question the register cannot answer. That is not theoretical: on 2026-08-15 a
  divider written **after** `latchPLLAD()` left `PLLAD_MD` reading 2210, the PLL
  running 2553 and `IF_HSYNC_RST` sized for 2210 — solid green screen, every
  register self-consistent, nothing to diagnose from. **Whatever writes
  `PLLAD_MD` must run before the latch, not after it.**

  **A solid green screen has a SECOND cause, and it is not the divider.**
  `PLLAD_CKOS` selects which tap of the ADC clock feeds the pipeline, and the
  decimators (`ADC_CLK_ICLK1X`, `ADC_CLK_ICLK2X`, `DEC1_BYPS`, `DEC2_BYPS`) undo
  in the digital domain what that tap added — so the two have to describe the
  same oversampling ratio. `Adc::applySampleRate()` writes all five together for
  that reason. Set `PLLAD_CKOS` to 2 against the decimators the firmware runs at
  oversample 4 and the screen is green and stays green: measured at +2 s and
  still green at +20 s, cleared by putting it back. Throughout it,
  `PLLAD_MD`, `IF_HSYNC_RST`, `SP_RT_HS_SP`, `HPERIOD_IF`, `SP_VTOTAL`,
  `STATUS_MISC_PLLAD_LOCK` and `STATUS_IF_HT_BAD` all read exactly what a healthy
  unit reads. **There is nothing in a register dump that distinguishes it**, and a
  detection pass is what repairs it, because that is what rewrites the group.

  Two traps come with it. The picture is the only instrument, so a still that
  misses it and a still that catches it look like the write not working — record
  a clip. And `HPERIOD_IF` stays perfectly healthy while the screen is green, so
  this is not the railing and the two must not be merged.

  `PLLAD_MD`, `IF_HSYNC_RST` (= `MD`/2) and `SP_RT_HS_SP`, the sync processor's
  retime window, are **ONE quantity in THREE registers**, and
  `Tv5725::SourceMeasurement` owns all three off one held value. It is *state*,
  handed to `Tv5725::CaptureWindow` rather than read back — the same rule
  `CaptureWindow::ProgressiveStart` already carried.

  **`SP_RT_HS_SP` IS THE CAPTURE COUNTER'S ORIGIN, NOT A FRACTION OF THE
  DIVIDER.** It lays the retimed pulse on the incoming one --
  `PLLAD_MD - STATUS_SYNC_PROC_HLOW_LEN + 63` -- so it follows the source's own
  sync width. `0.93 x MD` is `1 - duty` for a source whose sync takes 7% of the
  line and is wrong for every other duty, placing the whole capture window with
  nothing in a register dump to show it. **THE ORIGIN IS CALIBRATED FOR A SAMPLING
  DENSITY, AND IT IS WRONG OFF IT.** `RetimeOriginSamples` 63 was measured at
  about 1.12 ADC samples per source pixel; 1280x720@60 in pass-through samples
  1.235 and the computed 34 reads 0.069..0.137 at the test card's finest
  grating where 68 reads 0.0075..0.0109 -- nine to thirteen times, on that
  register alone. **Nothing derives the 68.** A narrow pulse no longer returns
  0: the stop is a phase and `retimeStopFor()` wraps it modulo the line. But
  `writeRetimeStop()` still returns silently on 0, which `retimeStopFor()`
  answers for an unmeasured pulse, so the whole capture window is then placed
  from the previous source's origin with nothing said.
  `docs/investigations/the-pass-through-shimmer-is-the-capture-origin.md`,
  `docs/investigations/the-retime-stop-is-the-counters-origin.md`.

  **AND THE ORIGIN CARRIES THE SOURCE'S VIDEO LAG IN THE SOURCE'S OWN PIXELS**,
  which is why one constant framed some modes and not others. The lag is about
  4.4 source pixels, so it costs 1 ADC sample on a mode sampling 1.36 samples a
  pixel and 14 on the bench's 320x256@50 at 4.30 -- `SourceTiming::pixelSamples()`
  is what makes it computable, the raster stating the line's pixel count, and a
  source matching no raster gets the constant as measured. **Two bench modes at
  ONE divider is what names the quantity**: 800x600@60 samples 1436 across 1056
  source pixels and 320x480@60 samples 1444 across 400, and the two want 2
  samples of origin and 9.
  `docs/investigations/the-capture-origin-varies-by-mode-at-one-line-rate.md`.

  **`IF_HSYNC_RST` is `PLLAD_MD` HALVED ONLY WHERE THE LINE IS DOUBLED**, which is
  what `SourceMeasurement::ifLineFor()` does — an IF unit is two ADC samples on a
  doubled line and one on an undoubled one. All twelve shipped tables were
  line-doubled SD, so `== PLLAD_MD/2` held across every one of them and reads as a
  universal law; measured on an 800x600@60 RGBHV source it is `== PLLAD_MD`, 1124
  against 1124, and that is correct. `SP_RT_HS_SP` is wrong in five of the tables
  (`ntsc_1280x720` ships **68** against 2180) and is only saved by the runtime
  write.

  **`STATUS_SYNC_PROC_HTOTAL` is the only thing on the board that can see a
  missing latch**, because it counts real ADC clocks per line and so reports the
  *latched* divider. Locked, it equals `PLLAD_MD` — 2553 against 2553, with an
  occasional ±1. **Unlocked it is noise that looks like a small latch error**: it
  held a steady 2558 against 2553 across 22 samples while `SP_VTOTAL` sat at
  97/98. Steady is not valid; ask whether `SP_VTOTAL` is counting first. And it
  reads wrong for a third reason too — an *unconfigured sync processor*. With
  `SP_PRE_COAST`/`SP_POST_COAST`/`SP_DLT_REG` zeroed it read 2400 and did not
  move when 2553 was written and latched by hand.
- **`STATUS_MISC_PLLAD_LOCK` IS A DUTY CYCLE, NOT A STATE, AND ONE READ OF IT
  SAYS NOTHING.** It flickers several times a second on a picture judged
  perfect: measured with `Tv5725::SamplingLog` at 25 ms, a scaling state reads 1
  in **86.3% of 1131 samples with 261 transitions**, 8.7 a second. So a single
  HTTP read has better than a one in eight chance of returning 0 while
  everything is right, and **"the ADC PLL is unlocked" is not a conclusion a
  register dump can reach**. Ask the percentage and the transition rate, which
  need the sampling log -- an HTTP read answers at tens of hertz at best and
  cannot see a bit moving at 8.7 Hz.

  **The honest health metric is `STATUS_SYNC_PROC_HTOTAL` against the divider**,
  because the sync processor counts in ADC clocks and so reports a locked PLL's
  divider back. That is a comparison, it is stable where the bit is not -- 0 or
  ±1 on every healthy state measured -- and a genuinely sick state is hundreds
  out rather than one. `docs/investigations/pllad-lock-is-a-duty-cycle-not-a-state.md`.

- **`STATUS_IF_VT_OK` READING 0 IS THE NORMAL STATE ON SEPARATE SYNC, NOT A
  FAULT.** The block does not complete a vertical measurement there and
  `VPERIOD_IF` holds debris rather than a period, so the bench RISC PC on `vga`
  reads 0 whenever it is healthy. Reading it as "the input formatter is dead" is
  a diagnosis of the sync arrangement, not of the unit. `STATUS_IF_HT_OK` is no
  better in the other direction: it reads 1 while `HPERIOD_IF` rails.

  **IT GATES ACQUISITION TOO, AND WHAT COVERS THAT IS THE LINE PERIOD RATHER
  THAN THE BIT.** `InputFormatter::verticalPeriod()` returns 0 while the bit is
  0, and that reading is the SECOND INSTRUMENT the contradicted-count guard
  uses to tell a sync processor that stopped counting from a source standing
  still -- so where the bit reads 0 that guard is blind, and the engine held a
  dead count at `state: acquired` indefinitely. `rateMoved()` is what notices
  now: a line period that MOVED arms on its own where no field rate can be
  measured at all, because a reading that is missing cannot refuse one that
  moved. **So the bit still explains nothing and is still not a verdict** -- it
  no longer decides whether the engine can act.

  **THE QUIET PIN IS THE DISCRIMINATOR, AND IT IS NOT THE SAME THING AS THE
  BIT.** `TestBus::selectInputVsync()` drives the formatter's vertical SIGNAL,
  where `STATUS_IF_VT_OK` gates only its MEASUREMENT REGISTER -- so a healthy
  separate-sync source reads the bit 0 and still times a field rate, which is
  why the arm does not fire there. `vsyncEdges(): no INPUT vsync` on the console
  is the state that reaches it. `docs/known-issues.md`,
  `docs/investigations/the-formatters-frame-is-gated-off-when-the-count-stops.md`.

  **The pattern is worth naming, because four of these have now cost time.**
  `STATUS_MISC_PLLAD_LOCK`, `STATUS_SYNC_PROC_VSACT`, `STATUS_IF_VT_OK` and
  `STATUS_SYNC_PROC_HLOW_LEN` all read like verdicts and are none of them a
  verdict: one is a duty cycle, one reports the sync path already chosen, one
  reports a measurement this sync type never completes, and the last reports a
  pulse width the block only measures once it has LOCKED -- so the vertical
  interval's broad pulse, which is present on composite sync and absent on
  separate, cannot be used to CHOOSE the sync type. Measured as a 2x2, it
  false-positives on 41% of readings on a separate-sync source in the csync
  arrangement and finds the pulse 3 times in 2736 on a composite source in the
  separate one.
  `docs/investigations/the-own-vsync-probe-answers-by-absence.md`. **A status bit is a measurement of a block, not a
  judgement of the unit** -- ask what the bit is counting before quoting it as
  evidence, and prefer a comparison that has a right answer, such as
  `STATUS_SYNC_PROC_HTOTAL` against the divider.

- **A WIDE STATUS FIELD TEARS ACROSS ITS TWO BYTES, AND THE TORN VALUE LOOKS
  LIKE A FAULT.** `rawRead()` takes both bytes in one I2C transaction, which is
  not atomic at the chip: the second byte is clocked out tens of microseconds
  after the first, and a per-line register updates inside that gap. **The
  signature is a 2x2 crossing of two genuine values** --
  `STATUS_SYNC_PROC_HLOW_LEN` on composite reads 153 (`0x099`) and 2661
  (`0xA65`), and the crossings 101 (`0x065`) and 2713 (`0xA99`) appear 57 times
  in 27207 samples with nothing else between them. **Decompose an outlier
  before believing it**, and treat it as real only where its high and low bytes
  are not both another observed value's -- `STATUS_SYNC_PROC_VTOTAL`'s 337
  (`0x151`) is genuine on exactly that test. `sampling_stats.py` flags the
  crossings itself.

  **AND A FIELD THAT NEVER MOVES CANNOT SHOW THE ARTEFACT**, so a comparison of
  two states is unsound in one direction: the same source on separate sync
  reads one high nibble and two adjacent low bytes, where tearing is invisible
  by construction, so its narrower spread is not evidence that the measurement
  is steadier. **A single-bit field is immune** and carries across such a
  comparison where a wide one does not.
  `docs/investigations/a-wide-status-field-tears-across-its-two-bytes.md`.

- **`STATUS_SYNC_PROC_VSACT` is not a lock indicator, and it is not dead
  either — it reports the sync path you are already on.** Both readings are
  measured, on the same source, a day apart:

  | | `VSACT` | `SP_VTOTAL` | picture |
  |---|---|---|---|
  | csync path (`SP_SOG_MODE` 1, coast 7/3) 08-14 | **0 in 2375/2375** | 308, 7.34% off-mode | glitching |
  | separate-sync path (`SP_SOG_MODE` 0, coast 0/0) 08-15 | **1 in 150/150** | 311, 0.00% off-mode | clean |

  So `V=no` is the normal locked state *in the csync configuration* — the
  earlier "dead on this board" reading was that configuration seen alone, and
  clearing `SP_EXT_SYNC_SEL` on its own does not revive the bit. **Never judge
  lock on it**; ask whether `SP_VTOTAL` is counting. That mistake cost an hour
  recovering a unit that was fine, and `dump_registers.py`'s lock indicator
  required it, so **every successful restore printed as failed** until `96efeec`.

  **Never steer the sync type off it.** Choosing `rto->syncTypeCsync` by reading
  `VSACT` is self-latching: the bit only reports correctly once the sync type is
  already right, so a unit that lands on csync stays there. `applyPresets()`
  decides with `sourceHasOwnVsync()` instead, which switches `SP_EXT_SYNC_SEL`
  and asks whether a V sync line actually arrives.

  **IT RUNS PER INPUT CHANGE, AND A MODE CHANGE REUSES WHAT IS HELD.**
  `establishSyncType()` keys on the input selection and says so, because a source
  changes its sync type far more rarely than it changes mode and a composite
  source spends the whole probe window every time. **A SOURCE THAT CHANGES ITS
  SYNC TYPE ON ONE CABLE IS NOW FOLLOWED, AND WHAT ARMS IT IS THE EXHAUSTED
  REFERENCE CLOCK SET** -- the held type decides what the sync processor counts,
  so a source on the wrong arrangement cannot be measured through ANY clock, and
  the set is finite. Measured on `vga` at 1280x720@60: re-probed 2.5 s after
  `SYNC 1`, and 5.7 s after `SYNC 0` with the source acquired at 6.6 s, where
  before neither direction recovered without `/sc?~`. A line count cannot be
  that discriminator -- at 720x480@60 the wrong path counts 518 against 525.
  **`/sc?~` still forgets it**, and `SP_SOG_MODE` read back is the only witness
  that the type in force is the one the source is sending -- check it before
  believing any measurement taken across a sync-type change, and note that
  FREEZING across one blocks the re-probe outright.
  `docs/sync-type-selection.md`,
  `docs/investigations/a-sync-type-change-arms-no-probe.md`.
- **The scaling path and bypass are told apart by `DAC_RGBS_BYPS2DAC` and
  `OUT_SYNC_SEL`**, both 1 in bypass and 0 on the scaling path.
  `DAC_RGBS_ADC2DAC` is NOT the tell, being 0 on both, and the scale registers
  are not either, since bypass leaves `VDS_HSCALE`/`VDS_VSCALE` on the last
  scaled load's values. **Establish the path before reading anything the input
  formatter owns as a fault** -- in bypass the IF is out of the circuit and its
  registers measure nothing. `docs/rgbhv-bypass-trap.md`.
- **Judge only settled samples.** Discard ~6 s after any mode change. Raw
  sampling across a sweep throws garbage at nearly every change that resolves on
  its own; scoring those produced 15 false positives in one run.
- **Preset loads leave things behind**, and the symptom lands somewhere
  unrelated. A yellow-tinted picture is `DAC_RGBS_B0ENZ` (s0 `0x45` bit 0)
  cleared by a bulk table load that never got patched back — the firmware never
  writes that bit to 0 anywhere. Fix: `curl '…/setreg?s=0&r=0x45&v=0x11'`.
  `docs/preset-load-clobber.md`.
- **"Something writes it" is not "something owns it", and there are TWO levels
  of that.** The first is settled: a field written only by `setResetParameters()`
  or one of the two bypass switches has no owner on the *scaling* path, because
  those functions leave a state a preset had to undo. The second cost step 4 a
  second time and no tool can see it: attribution is per FUNCTION, so a field
  `doPostPresetLoadSteps()` writes only inside `if (rto->outModeHdBypass)` or
  `if (rto->inputIsYpBpR)` reads as owned by a function that unquestionably
  runs. **22 fields were in that state on 2026-08-15 with `--gap` reporting
  zero.** A brace-depth heuristic flags an order of magnitude more than matter
  and was reverted. **The check that works is a bench diff against a known-good
  dump, minus the divider-derived differences.**
  `docs/investigations/preset-abandonment-audit.md`.
- **`/uc?h` does not clear Mode Detect.** It sets `presetPreference =
  Output480P` — a persistent user preference — and force-calls `applyPresets()`.
  It appears to "fix" railing by reloading a preset. The actual Mode Detect reset is
  `resetModeDetect()` (`SFTRST_MODE_RSTZ`, s0 `0x47` bit 1), reachable via
  `/setreg` — but it does **not** recover a bypassed IF, and nothing in the
  firmware resets Mode Detect while sync is present.
- **THE 535-LINE GATE IS GONE, and it was never a property of the part.** An
  RGBHV source used to enter bypass above 535 lines with no branch to leave by,
  so it stayed there for the life of the boot. Removed once the bench measured
  what it was costing: 800x600 at VTOTAL 627 scales sharp and full screen, with
  `PLLAD_MD` 1124 latched against the bypass switch's hardcoded 1856.
  **THE ROUTE IS STORED PER SOURCE, AND PASS-THROUGH IS WHAT A SOURCE WITH
  NOTHING STORED TAKES**, in the same record as the framing and the shape,
  because whether pass-through works is decided by the source rather than by
  the output. `/uc?x` stores a choice against the source in force; there is no
  unit-wide preference. `/geometry` reports both as `route` and `routeStored`.
  **A CHOICE LANDS WITHOUT THE SOURCE MOVING, AND STORING ONE IS THE SAME
  OPERATION AS ANSWERING IT** -- `VideoSourceAcquisition::chooseRoute()` and
  `resetFraming()`, because the route is re-answered only on a pass that
  re-resolves the source, and the reset cleared the route with the framing and
  armed nothing. **THE ROUTE IS ALSO KEYED ON LESS THAN THE FRAMING IS**: the
  sync duty is read through the divider the path in force installed, which
  spends 45% of `SyncWidthIdentity` on a route change alone, so the route is
  keyed on the count, the rate and both polarities, held on every entry the
  source has, and `FramingTable::findRoute()` is the one way to ask. **The host
  suite's figure for that same gap is ten times larger and is a fake
  artefact** -- a seeded pulse COUNT against a moving divider, so do not quote
  it as hardware. `docs/rgbhv-bypass-trap.md`, `docs/framing-presets.md`.
- **`produced` IS `capture x 1024 / scale`** — a simple multiply, both axes, no
  loss term at either end. It did not look like one because the deficit against
  it *changes sign* with magnification, so four models were proposed in one
  evening and three refuted. The deficit was never in the span:
  `produced` was being measured from a corner assumed constant, and the corner
  moves. **The write start is `VDS_?B_SP + START_CONST + START_PER_MAG x
  magnification`** — 55 + 25m horizontally, 0.2 + 0.8m vertically — which is
  pipeline latency before the first write, seen from the far end where it could
  only look like loss. Measuring a fixed span from a moving origin gives a length
  that appears to vary. `docs/scaler-geometry-model.md`, `measure_origin.py`.
- **A good fit is not evidence the quantities are what you think.** The refuted
  two-term model fitted all eleven readings to 0.43 px and was still wrong;
  residuals could never have caught it. Only measuring the near edge did.
- **A measurement through an edge you cannot see is not a measurement**, and the
  specific failure is creeping until the *picture* stops and recording it as
  where the *screen* stops. That produced `CORNER_H` 129, `ORIGIN_OFFSET_H` 78
  and 94, `PANEL_VISIBLE_LEFT` 127 and a vertical bezel of 63 — every one of them
  a write start wearing another name, and 127 and 78 are the same reading filed
  twice. **Put content beyond the edge first**: set the picture to overrun the
  raster on all four sides, then creep, and every boundary has live video either
  side of it. Measured that way the bench panel shows `90..1351 x 41..1121`, not
  the 127/63 carried here for months.
- **Two points cannot disconfirm a line.** Three magnifications is the minimum
  that can fail, and `measure_produced.py` and `measure_origin.py` print
  residuals so they can.
- **WHERE AN EXTREME CARRIES A REAL HAZARD THE MARGIN BELONGS IN THE DEFAULT,
  NOT IN THE CLAMP**, because the default is adjustable and the clamp is not.
  The line volume is the worked case: the PT2257's floor was hardcoded 12 dB
  above its own 0 dB, costing 12 dB on every source to guard a clipping level
  the encoder's datasheet never states. Exposing 0..50 dB with a default of 12
  keeps the protection for a unit nobody has touched and lets a quiet source
  reach 0 -- measured clean at 0 dB on the RiscPC's headphone output. Capping
  the top cannot express that and penalises every source to guard a case that
  may not exist.
- **Don't pin a CONSTANT to a panel edge**, because where a display stops
  showing is a property of that display: `geometry_math` centres on the raster
  instead and the user finds their own edges with pan and scale. The vertical
  visible region is derivable — `1121 - 41 = 1080` exactly, the encoder's active
  window, same on every display.
  **This is not licence to explain a discrepancy as overscan on THIS bench**,
  where the set is on FULL for every mode and paints the whole active area. A
  measurement taken here is portable in the direction that matters: what the
  board emitted.
- **The engine CALCULATES every register from held state. Registers are an
  output and are never an input.** Everything the engine needs, it already
  knows, because it computed it: the raster, both scales, both windows, the
  divider. Reading one back to derive another is what most of the geometry bugs
  here were.

  **The rule is two rules, and conflating them blocks correct work.** One is
  that **every field has a single owner**, and all writes to it go through that
  owner. The other is that **nothing is read back to derive something else**, so
  a value is derived once and a previous solve's state cannot reach the next
  one.

  **The exception to the read rule is a TRUE MEASUREMENT — something only the
  chip can tell you about the SOURCE.** `STATUS_SYNC_PROC_*` is the obvious
  family, and it is not a closed list: the source's hsync polarity is as much a
  measurement as its line count. What disqualifies a read is asking the chip for
  a value the engine itself put there.

  **BOTH RULES ARE SINGLE RESPONSIBILITY, AND THE EXCEPTION LICENSES ONE OWNER
  RATHER THAN EVERY CONSUMER.** Every measured fact about the source —
  field rate, line rate, line count, sync polarity — is owned by
  `Tv5725::SourceMeasurement`, and a class that needs one asks for it. Taking a
  second reading of a fact something else already holds is the same defect as a
  second writer on a field: `FrameSync` timed the source's vsync period on
  `DEBUG_IN_PIN` inside its own correction, the two instruments disagreed, and
  the unfiltered one won — a missed edge ISR reads a WHOLE MULTIPLE of the
  frame, which scales the phase target with it and slams the correction to its
  clamp. **An agreement test does not rescue a second owner**: agreement rejects
  noise and cannot reject bias, and two readings of a doubled frame agree with
  each other perfectly. Only a check against something independently known
  rejects that, which is what `rateIsPlausible()` is and what the period had
  none of. `docs/investigations/the-frame-time-lock-saturates.md`.

  **Writing a register FROM a measurement is ordinary, not an exception.**
  `SP_HS_INV_REG` set from the measured polarity is the same kind of act as any
  other solved register — one owner, derived once. Prefer it over carrying the
  polarity into the solve: **normalising at the hardware boundary removes a
  variable from the solver, and one fewer input retires a whole class of bugs
  rather than one instance of it.** Measure it where the sync arrangement is
  already being determined, write it once, and let everything downstream see one
  shape.

  Two consequences. **A register written by anything outside the engine corrupts
  the calculation**, which is why the OSD, the IR handler and the web pads must
  call the engine rather than write registers — and why a value the engine did
  not choose is *adopted* explicitly (`SourceMeasurement::adopt()`) rather than
  silently read. And **state living outside the registers is not a problem to
  solve**: it is the design. Hold it, derive from it, expose it over
  `/geometry` so tests and presets can set it.

  **Inheriting means reading it off the chip**, and every geometry fault of
  2026-08-06 was one: inheriting the corner put 41 px of the previous frame down
  the left of the screen, and inheriting the picture size froze a picture at 620
  lines that no zoom step could grow. `scale_step` deliberately takes no `scale`
  argument — there is a test asserting the parameter does not exist, because its
  existence was the bug. Every pad press recomputes every window, pan included.
- **The output raster is computed too, since 2026-08-13.**
  `Geometry::solveRaster()` derives both totals, both sync pulses and the display
  clock seed from the frame height and the measured field rate, so a preset
  table's raster bytes are overwritten on every mode change. Measured
  1436 x 1126 @ 80.85 MHz before, **1915 x 1126 @ 107.81 MHz after** — a third
  more horizontal resolution. Two rules come with it. The order is raster →
  clock → windows → rate steer last — expressed inside `Geometry::poll()`, which
  drives the whole sequence, because the display clock is steered from the seed
  the raster chose. Running the steer early gave a 31 Hz frame and a black
  screen.
  And **a wider raster costs zoom range, because the zoom floor is
  `raster / maxMagnification` while the default capture is a property of the
  INPUT line alone** — `1126 x 0.864 = 973` here — so the two do not
  track and widening the output silently eats the travel. That is the mechanism;
  the numbers below are what it cost, twice.

  `AxisHorizontal` used to magnify at most 2.048x (`scaleMin` 500, swept 2026-08-09).
  At the 1436 raster that left 307 units of travel; when `Geometry::solveRaster()`
  made the raster 1916 on 2026-08-13 it left **73**, at which point the
  horizontal scale clamps before the picture reaches full screen.
  **THE FLOOR IS PER AXIS, `Axis::magnificationFloor()`: 342 horizontally,
  which is 2.994x, and 205 vertically, which is 5.0x** — so the floor follows
  the raster (`room x floor / Unity`, about 640 at 1916) and no longer
  collapses each time the output widens. Both are PICTURE-QUALITY bounds and
  not derivations, and they differ because the evidence does: past 3.0x the
  horizontal scaler starts picking wrong samples, which is what `d882dff75`
  put the horizontal one there for and `docs/known-issues.md` carries the
  sweep, while nothing measured bounds `VDS_VSCALE`, a different register in a
  different stage. A shared floor put the horizontal finding on both and
  letterboxed every source too short to reach 3.0x.

  **RD-5725-1.1 states no minimum for `VDS_HSCALE`**: it gives
  only `HSCALE = 1024 x in / out` and the field is 10 bits, so there is no
  hardware bound to calculate and any floor is a picture-quality choice. Where
  interpolation starts to look bad depends on the resolution ratio, so it is the
  user's to find by zooming: only the picture can say when it is clean.
- **The output hsync position affects left-hand corruption, and nothing models
  it.** `VDS_HB_SP` below 8 corrupts, measured with `VDS_HS_ST` at 10; left-hand
  corruption that survived everything else then cleared by moving the pulse to
  62..77 (later, and a third as wide). The tempting reading — that the floor is
  `VDS_HS_ST - 2` — is **refuted by that same state**, which is clean with
  `VDS_HB_SP` at 9, fifty units before the pulse. Position or width, one at a
  time, is the experiment. Treat 8 as measured at one hsync setting only.
  `snapshots/hsync-tuned-no-left-corruption-2026-08-06.json`.
- **THE TAIL BAND HAS TWO CAUSES AND BOTH BOUNDS ARE LIVE. A DOUBLED LINE IS
  CAPPED AT 2200 ADC SAMPLES, ON A MEASUREMENT.** The first cause is the VDS's
  one-line delay, `VDS_D_RAM_BYPS` — the OLED's Menu->Color->Line filter,
  `uopt->wantVdsLineFilter`, `/uc?m` — which sits DOWNSTREAM of the capture and
  destroys the tail of every line past `VideoProcessor::LineBufferSamples`
  captured samples. **The solve refuses that state both ways**:
  `dividerCeilingForLineFilter()` holds the divider down until the captured line
  fits the delay, and where the source has more pixels than the delay holds
  `applyLineFilter()` leaves the delay out and keeps the samples. So that one is
  a property of the buffer rather than an artefact a session will see, and the
  trade is what is left: the delay costs sampling density, or it is out of
  circuit. A framing widened past the buffer by hand still pays the ceiling
  without the delay.
  **The second cause is the CAPTURE PATH, and the delay was the confounder in
  every measurement of it**: bypassed, the band is still there. The onset is
  sample 2236..2256 of a doubled line, so `SamplingClock::DoubledLineSampleLimit`
  holds a doubled line at 2200 and the whole line arrives — measured on the
  picture, the bench 320x256@50 going 2506 -> 2200 with the band's 156 green
  photo columns going to none. **So "there is no capture bound" is REFUTED and
  the constant must not be deleted**; what it costs is density, 2.15 kept samples
  per source pixel against 2.45. **Judge a divider against the bound that binds,
  which on the bench is this one doubled and the eleven-bit line counter
  undoubled** — `firmware_constants.expected_divider()` applies both, and the
  rating alone reads 14% high. `docs/known-issues.md`,
  `docs/investigations/the-tail-green-is-the-vds-line-filter.md`,
  `docs/investigations/tail-green.md`.
- **THE LINE COUNTER IS ELEVEN BITS AND THE REGISTER IS TWELVE**, so 2047 kept
  samples per line is the wall on every source and both scan modes — and a
  read-back cannot test it, because `s1_0F` bit 3 stores a 1 and returns it
  while the counter ignores it. No status field reacts either: a counter of
  1500 on a 1103-unit line tears the picture in half with `STATUS_IF_HT_OK`
  still 1. Pass-through hits the same wall in `HD_HSYNC_RST`. **Only the
  picture can measure this**, and the discriminator is `correct + 2048`, which
  an eleven-bit counter must render indistinguishable from `correct`.
  `docs/investigations/the-line-counters-are-eleven-bits-measured.md`.
- **THE KEPT COUNT IS THE RULE AND NYQUIST IS BEST EFFORT.** What carries the
  source's pixels is the count the decimator KEEPS — `PLLAD_MD` undoubled, and
  **half of it where the line is doubled**. Oversampling acts on the CONVERSION
  rate (`converted = kept x ratio x samplesPerUnit`, the doubler's own
  decimation being a second factor of two), so it buys freedom from aliasing and
  **buys no resolution at all**. `SamplingClock::recommendedDivider()` therefore
  maximises the kept count and takes a ratio only where it costs none of it.
  **The asymmetry is the reason**: a source that could have been oversampled and
  was not aliases the top of its band, while one starved of samples to pay for
  oversampling loses resolution no filtering brings back.
  **The divider therefore moves with the measured line rate** wherever the row
  binds rather than the counter, so a wobble re-latches the ADC PLL.
  `docs/sampling-table.md` is what every input mode lands on, generated from the
  firmware — regenerate it, never edit it.
- **The horizontal axis has no native resolution.** The chip sees sync edges, not
  pixels, so the source's pixel clock is unknowable and 320x256 and 640x256 are
  indistinguishable. Capture is in ADC sample units, and how many there are per
  line is your choice (`PLLAD_MD`).
- **Blanking cannot be auto-detected.** A border is black *active* video,
  electrically identical to back porch. Sync-domain measurement finds the raster,
  never the picture inside it.
- **The headroom rule is RETRACTED — do not reinstate it.** The old rule
  (`memory window - produced >= ~13 px`) rested on `SOLVED-mode13-fullscreen-clean`,
  whose `VDS_DIS_HB_ST` of 1372 blanked 74 px of a picture ending at 1446 — and
  tearing shows worst at the *right* of the line, so its evidence was hidden.
  Points taken the same evening at HSCALE 993 and 850 failed identically, with
  24.9 px and 163 px of picture hidden. **Any headroom measurement is worthless
  unless the display window contains the whole picture.**

  Only two points survive scrutiny, and they do not fit a rule:

  ```
  HSCALE 1023 (x1.001) -> produced  798.78, edge at VDS_HB_ST  881    33.2 px
  HSCALE  850 (x1.205) -> produced  961.36, edge at VDS_HB_ST 1095   ~84.6 px
  ```

  The requirement is **not monotonic in HSCALE**, and the corruption comes in
  **multiple stable bands**, so an edge found by creeping down is only the true
  edge if you creep past all of them. More measurement does not fix that.

  So `geometry_math.py` no longer computes a margin: the memory window IS the
  display window, `VDS_?B_ST == VDS_DIS_?B_ST`, allocating nothing spare, and
  `HEADROOM_WARN_PX = 100` is **a floor to warn below, not a budget to reserve**.
  Banded non-monotonic thresholds look like marginal signal integrity, in which
  case the numbers are facts about *this board*. A torn picture is still not
  automatically a fault to chase.
- **The zigzag is NOT HSCALE-banded, and that is measured.** `VDS_HSCALE` was
  swept by hand across the corrupted state on 2026-08-09 and **no value cleared
  it**. The previous session's reading — clean at 823 and 762, torn at
  795, therefore banded like the left-edge corruption above — does not survive
  a full sweep. Do not reach for "move the scale" as the explanation or the fix.
  The two banding observations are **separate**: the headroom/left-corruption
  bands above stand, the zigzag one does not, and merging them is what produced
  the wrong call. An earlier "restoring a snapshot fixed it" was the snapshot
  being a different *mode*, not a different scale.
- **The sync processor counts in ADC samples, not IF units.** Settled
  2026-08-09 and worth stating because the opposite was assumed. Three of its
  registers hold values above the 1277-unit IF line — `SP_RT_HS_SP` 2374,
  `SP_H_CST_SP` 1667, `STATUS_SYNC_PROC_HTOTAL` 2553 — and `HLOW_LEN` only
  matches the source's mode file in ADC: `181/2553` = 7.09% against AKF50's
  `36/512` = 7.03%, where reading it as IF units gives 14.17%, twice the mode's
  sync width. **`SP_CS_CLP_ST`/`SP` ARE THE EXCEPTION AND THEY COUNT THE 27 MHz
  REFERENCE**, which `SP_CLP_SRC_SEL` selects and every path here asks for.
  Measured by walking the register against the emitted frame on the Wii at 480p
  into 1080p, divider 1212: **2.667 emitted columns per count** against 1.885
  per ADC sample, so reading the pair in ADC samples or IF units is out by a
  factor of 1.4. A reference count and a source pixel coincide on that source --
  858 x 31469 is 27 MHz -- so the walk rules the other two out and the clock
  selection names the survivor.

  **So the small values are the back porch, and the live bound is where the
  picture starts** -- 0..66 usable on that source, picture from 67. Past it the
  black level comes off picture and **the whole frame darkens with it**, a
  picture mean of 152.4 at a start of 108 against 205.7 at 19. So a dark or
  washed-out frame is as much a clamp symptom as the visible bar, and **neither
  shows in a register dump** -- every register reads correct under both.
  `docs/investigations/the-clamp-had-two-owners-and-two-units.md`.

  **`SP_H_CST_SP` IS A SECOND SUCH EXCEPTION, AND THE 1667 DOES NOT SETTLE
  IT.** Above the IF line rules out IF units and nothing more: the third
  candidate is **27 MHz counts**, and `SyncProcessor::coastLineFor()` writes the
  register in exactly those — `27e6 / lineRate`, which is 1728 on that source,
  so the 1667 read back is the firmware's own `0.968 x 1728` and is evidence
  about the formula rather than about the hardware. **The two readings only
  diverge away from the divider the formula was fitted at**, 1728 against 2553
  there and 858 against 1446 on the Wii at 1080p, so a coast window that scores
  well at one divider says nothing about another. Ask the COUNT at two
  dividers with the window placed, not the register.
- **`SP_H_PULSE_IGNOR` IS IN ADC SAMPLES TOO, AND A BYTE THAT READS ONE DIVIDER
  IGNORES THE SOURCE AT ANOTHER.** 107 was measured right through a 2200-sample
  line and ignored the Wii's 106-sample pulse through the 1446 a 1080p raster
  affords, which is the whole of why the Wii never acquired at 1920x1080. It
  follows the divider now, 49 per thousand of the line, and the search writes
  16 because an unlocked block counts at the 27 MHz reference, where the same
  pulse is 63 or 31 samples -- so a threshold sized for a divider cannot be
  written before the clock is latched.
  `docs/investigations/the-pulse-ignore-is-a-fraction-of-the-line.md`.
- **EDID is unreachable.** The MS9288A HDMI encoder is on no MCU's I2C bus (see
  the schematic), so output rasters are chosen blind.
- **Sync stability does not mean the divider is right.** `getStatus16SpHsStable()`
  passed happily with `PLLAD_MD` halved from 2553 to 1276, and the display went
  solid green — the sync processor locks onto edges regardless of whether the
  ADC is sampling the line the way the rest of the pipeline assumes. Anything
  choosing `PLLAD_MD` automatically needs a real validity test; "did sync
  survive" is not one.
- **Don't read the scaler's raster as what the TV sees.** The MS9288A samples
  the analog output and re-encodes it, so `VDS_HSYNC_RST` and friends describe
  the scaler's own timing, not the HDMI mode the display locks to.
- **Read `s0_49` before chasing any output register — half the output pads are
  off.** The MS9288A takes the analog RGB output plus HSOUT/VSOUT, so the digital
  video output port is never driven, and the preset tables switch it off:

  | `s0_49` bit | | every scaling preset |
  |---|---|---|
  | 0 | `PAD_CKIN_ENZ` | 0 — external clock in, **enabled** |
  | 1 | `PAD_CKOUT_ENZ` | 1 — CLKOUT pin, **disabled** |
  | 2 | `PAD_SYNC_OUT_ENZ` | 0 — HSOUT/VSOUT, **enabled** |
  | 3 | `PAD_BLK_OUT_ENZ` | 1 — HBOUT/VBOUT, **disabled** |

  Two consequences that have each cost time. **`VDS_EXT_HB_*` and `VDS_EXT_VB_*`
  do nothing.** They program the HBOUT/VBOUT blanking ("this blanking is for
  external used") and those pins are off; `VDS_SYNC_IN_SEL` is 0 as well, so the
  VDS takes sync from the IF module and there is no internal consumer either.
  `applyPresets()` copies `VDS_DIS_?B_*` into them and
  nothing refreshes them afterwards, so they are *always* stale after the first
  pad press — measured at `EXT_HB` 348..1356 against `DIS_HB` 99..1501, with a
  perfect picture. Writing `VDS_EXT_HB_*` by hand changes nothing either. Do not
  add them to a solver and do not suspect them for an edge artefact.

  And **the datasheet's 108 MHz `CLKOUT` rating is a pad spec for a pin nobody
  loads**, so it is not the proven limit on the display clock. What bounds the
  internal VCLK is stated nowhere; the divider register offers 129.6 MHz and
  162 MHz above it, and `ntsc_1280x1024`/`ntsc_240p` already ship `0xA5`.
  The 2026-08-11 sweep settled the range: 108 MHz and **129.6 MHz both work and
  are sharp**, 162 MHz flickers then goes black. So 129.6 is the measured
  ceiling — `OutputMode::WorkingCeilingHz`. `src/tv5725/OutputMode.h`.

  **The engine nevertheless asks for 108, and that is not a contradiction.**
  `OutputMode::EngineCeilingHz` is a *usability* limit, not an electrical one.
  Three constants, three different questions — do not collapse them.

  **RAISING IT HAS BEEN TRIED AND IT IS A SETTLED NO — do not propose it as an
  untried experiment.** The reason is not the zoom travel an earlier reading
  gave, which is spent: a clock buys raster width and **raster width costs
  MAGNIFICATION**, because the capture is the source's and the solve magnifies
  it to fill whatever raster it was handed. Past a magnification the part does
  not state, the playback pipeline has no data for the head of each line and
  shows zeros as green then stale memory as a grey comb. Measured on the RiscPC
  at 320x256@50 with a capture of 973 units, 108 MHz gives 1.84x and is clean
  where 129.6 MHz gives 2.22x and a run-up on the picture. **It cannot be
  blanked or moved** — blanking opens the display window later and clips panned
  content, and moving the output hsync slides the whole picture, the encoder
  holding its sampling window fixed to that pulse — and framing cannot escape it
  either, a 2304 px raster magnifying past the onset even at the whole
  capturable region. `docs/investigations/display-window-opens-early.md` carries
  the sweep and the measurements; `src/tv5725/OutputMode.h` carries the three
  ceilings and what each one answers.

  **AND THE RASTER IS NARROWER FOR A 60 Hz SOURCE THAN A 50 Hz ONE, WHICH MOVES
  THE CAPTURE BOUND WITH IT.** The horizontal total is the display clock over
  the output's own lines and **the SOURCE's field rate**, so one output
  resolution is several rasters: 1080p solves 1920 at 50 Hz and 1602 at 59.94,
  and `VideoPath::dividerCeilingForOutput()` follows it — 1724 against 1446 for
  the same source line. So **1080p is the mode with the LEAST horizontal room,
  and a 60 Hz source is its worst case**, which is why a fault keyed on the
  divider shows on the Wii and never on the 50 Hz bench RiscPC. Judge a divider
  against the raster the source's rate solves, never against the output's name.

  The live trap is the inverse: `ofw_RGBS` and `ofw_ypbpr` are the only presets
  that **clear** bit 3, so loading either brings HBOUT/VBOUT alive carrying
  whatever stale window was left behind.
