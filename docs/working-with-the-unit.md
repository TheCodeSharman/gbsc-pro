# Working with the unit

Snapshot before changing anything, diff after:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> --out snapshots/before.json
python3 tools/gbsc-pro-hwtest/snapdiff.py --diff snapshots/before.json snapshots/after.json
```

**The archive is `tools/gbsc-pro-hwtest/snapshots/`** — 100 tracked states plus
`LOG.md` saying what each one was. The `snapshots/` directory at the repo root
holds a handful of recent scratch dumps and is *not* it; reading this section as
pointing there found three files, concluded the archive did not exist, and cost
a session on 2026-08-14. To recover a working picture:

```sh
python3 tools/gbsc-pro-hwtest/dump_registers.py --host <ip> \
  --restore tools/gbsc-pro-hwtest/snapshots/dis-hb-st-tweak-2026-08-03.dump.json \
  --segments 0,1,2,3,4,5 --repeat 2
```

**A restore latches** since `96efeec` — `PLLAD_LAT` is what loads `PLLAD_MD`,
`ND`, `KS`, `CKOS` and `ICP`, and a snapshot cannot carry a rising edge. Before
that fix a restore across a change of divider left the ADC PLL on the old one
with **every register reading back correct** and the screen black. If you write
registers back by any other route, latch them yourself.

**Not every "known-good" is a full dump.** Some are 11-field summaries
(`known-good-riscpc-320x256-50-2026-08-11.json`), which `--restore` cannot use
and `snapdiff.py` crashes on rather than reporting the mismatch. Check the size
before planning an experiment around one.

**Restore every segment, and note that this line used to say `1,3,4,5`.** Low
power calls `setResetParameters()`, which zeroes segment 0 — DAC power, the pad
enables, the display clock select — and segment 2, so a restore that skips them
cannot recover from the state you are most likely recovering from. Segment 2 is
the expensive one to miss: the registers that matter there are *bypass* bits
(`MADPT_PD_RAM_BYPS`, `DIAG_BOB_PLDY_RAM_BYPS`, `MADPT_Y_WOUT_BYPS`,
`MADPT_VIIR_BYPS`), so zeroed they route video *through* the deinterlacer RAM
with every coefficient at zero, and the screen fills with random colour pixels
while all 608 config registers still read correct.

**A restore is not a recovery on its own.** The firmware has to agree that the
source is present, or `sourceDisconnected` stays true and `loop()` keeps
ratcheting `ADC_SOGCTRL` down every 500 ms underneath the picture you just
restored — see the freeze note below, because `/freeze` does not stop it.

The two snapshot formats are not interchangeable: `dump_registers.py` writes 496
config registers, `snapdiff.py` writes all 1536. Diff like against like.

**AND A DIFF OVER THE SMALLER SET IS NOT A DIFF OVER THE MACHINE.** Comparing a
608-register dump against a full one leaves 928 addresses outside the comparison
entirely, and 114 of those hold values that are neither 0x00 nor 0xff on a
settled unit — s1 0x60..0x77 is a live table, not padding. So *"a full dump
either side differs in only five fields"* is a statement about whichever subset
was taken, and every such conclusion here was drawn over the config range. When
the question is **what state survived an excursion**, take `snapdiff.py --save`
at both ends; the config dump answers a narrower question than it appears to.

`geometry.py --host <ip>` prints the input side, output side, and where the three
horizontal extents disagree — the fastest read on why a picture is wrong.

**NEVER hand-write a field's segment/register/offset/width. Read it by NAME,
and the by-name call is the SHORTER one:**

```python
from gbs_unit import read_named, read_fields
read_named(host, "PLLAD_MD")                                  # one field
read_fields(host, ["PLLAD_MD", "STATUS_SYNC_PROC_VTOTAL"])    # many, one request
```

`read_field(host, 5, 0x12, 0, 12)` is the same read with four numbers nobody can
check, and it is reserved for a slice that genuinely has no name.

**This rule is broken every session, and the reason is ambient rather than
ignorance.** `tools/gbsc-pro-hwtest/` is full of literal `read_field(host, 3,
0x02, 4, 11)` calls, so the wrong shape is what surrounding code models — and
this section used to demonstrate the rule with a three-line recipe that ended in
`read_field()` with the numbers pre-extracted, which is *more* typing than
inventing them. **Do not copy the slice style out of the tests.**

A wrong NAME raises before a request goes out. **A wrong ADDRESS does not error,
it returns a plausible number**, which is what makes this expensive rather than
merely tedious. Two ways it has cost real time:

- four invented addresses read as `SP_VTOTAL` 71 steady, `HTOTAL` 2723 against a
  2250 divider and an 88% sync duty — a textbook no-lock signature, diagnosed as
  a dead source, while the picture on the screen was perfect. By name the same
  registers give 311, 2250 and 7.1%.
- checking RGBHV bypass by hand-written slice gave `DAC_RGBS_BYPS2DAC` 0 and
  `OUT_SYNC_SEL` 0 on a unit correctly *in* bypass with a full-screen picture,
  which reads exactly like the "no raster at all" failure. By name: 1 and 1.

**If a status read disagrees with what the screen is doing, check how you
addressed it before believing the registers.**

`tools/gbsc-pro-hwtest/tv5725_registers.json` is the map behind all of these, and
the same one `snapdiff.py` and `setfield.py` decode with. `setfield.py --set
NAME=VALUE` writes by name, read-modify-write, so a field sharing a byte with its
neighbour does not destroy it. `dump_registers.py` reads everything in one pass.

### THE BENCH TELEVISION DOES NOT OVERSCAN. IT IS SET TO FULL, ON EVERY MODE.

**So a photograph of the panel shows the WHOLE HDMI active area, and content
missing from a photograph was not sent.** Every mode, not just the ones someone
checked.

**THE GUARANTEE IS THE SET BEING ON FULL, AND IT HAS ANOTHER MODE.** Put it in
its own 4:3 mode and it CROPS -- it is fitting a 16:9 signal, so it takes the
sides off rather than pillarboxing, and an hour went into diagnosing that as a
firmware fault. The board emits the shape itself now
(`docs/aspect-ratio.md`), so the set has no reason to leave FULL; if a
measurement disagrees with every other instrument, check which mode the set is
in before believing it. Nothing on this page may be read as the set hiding picture, and a
discrepancy may not be explained away as overscan -- that explanation is closed,
and reaching for it has cost several sessions and at least one wrong retraction.

**AND A SET OFF FULL CAN COST AS LITTLE AS ONE PIXEL, WHICH IS WHY IT READS AS
A BOARD FAULT.** It is not always the obvious crop above. `PATTERN CARD`'s green
frame is one SOURCE pixel wide, so a set off FULL can show no border on any edge
while the emitted frame carries it on all four -- measured at 320x256@50 into
1080p with 16:9 declared, the USB capture reads 2 px of black at the near edge
and 0 at the far, and the panel beside it showed none at all until the set went
back to FULL. **THE TWO INSTRUMENTS DISAGREEING AT ONE PIXEL IS THE SET'S MODE**:
ask the capture, and a border that returns when the set goes to FULL is the
whole diagnosis. Weakening the rule above on the strength of the panel alone is
the wrong retraction it warns about.

Two consequences, both the opposite of what is easy to assume:

- **The panel is a valid instrument for where the OUTPUT ends**, vertically and
  horizontally. A row or column count against a known feature is a real
  measurement, not a measurement of the set.
- **A "clip" seen on screen is the board's.** If the picture is short at an
  edge, the board did not emit it.

What still is NOT a panel property, and is the thing that actually blocks a
naive edge measurement: **the engine always scales the capture to fill the
raster**, so there is never blanking above or beside the picture to measure
against. That is a property of the solve. Count a feature of the SOURCE.

**THE PATTERNS ANIMATE BY DEFAULT, AND `ANIM OFF` STOPS THEM.** `PM5544` flips
its four corner squares yellow/white twice a second (`PROCanimcorners`) and
`CARD` flashes its border yellow, so a feature caught in one phase and absent in
the other reads exactly like a crop and has been reported as one. **Turn the
animation off and a single frame is evidence again**; without it, measure
against something that does not animate -- the grid, the colour blocks, the
frequency wedge -- or record a clip and compare frames in the same phase.
`PatLib.bas` in the `RiscPc` repo says which is which.

**`CARD` CARRIES A ONE-PIXEL GREEN BORDER FLUSH TO ALL FOUR EDGES**, so "is
anything clipped" is a yes/no on one frame rather than a margin to compute.
Green on all four sides means nothing was lost.

**THAT IS THE ACCEPTANCE FOR A FRAMING: the border on all four edges with NO
BLACK between it and the picture's edge.** Read at 16:9 -- a 4:3 declaration
pillarboxes, the bars are OUR blanking, and a strip taken at the frame's own
edge then measures the bars instead of the picture. **And a stored framing
answers before the computed default**, so `/sc?B` forgets this source's entry
first or what is judged is the last session's tuning. `card_edges.py` reports
the border's green dominance and the black count at each edge together.

**`PATTERN` WITH NO ARGUMENT REDRAWS THE DEFAULT rather than reporting what is
up**, so asking what is on screen changes it -- and `MODE` repaints too. Re-issue
`PATTERN CARD` after either, or the measurement is taken against a card nobody
selected.

### The shape of an eye-in-the-loop measurement

Where the picture is the only instrument, the tool is a **jog the user drives**,
not a sweep the session drives. `creep_window.py` is the worked example — coarse
and fine keys, one keypress per step, marks recorded as it goes. Build the next
one the same way; the rules below are each a wasted session.

- **A scripted sweep with a prompt per step does not work.** Numbered steps
  cannot be reported back reliably, and the answer arrives after the state has
  moved on. Give the keys to whoever is watching the screen.
- **Never jump to a value.** A transition that happens between two frames nobody
  saw is not a measurement. Creep *to* the interesting value, do not land on it.
- **Never bisect.** Corruption comes in bands, so a boundary found by halving is
  only the real one if nothing clean lies beyond it — keep creeping past the
  first edge to find out.
- **Enter means "clean, continue".** One keypress per step is what makes a
  one-unit creep bearable over eighty of them.
- **Marks belong in the tool.** Values printed to scrollback are lost; a mark
  with a note, printed as a table on exit, is the measurement.
- **Read every register the arithmetic uses in ONE pass.** The engine re-solves
  as the measured field rate wobbles, so a capture from one solve paired with a
  window from another invents discrepancies. A 46 px overshoot was reported that
  way and did not exist.
- **Freeze automation first**, or the solver rewrites the windows underneath the
  experiment.
- **BUT A FROZEN POKE CANNOT JUDGE WHERE THE PICTURE STARTS ON THE EMITTED
  FRAME**, because the transmitted window's start is latched at link-up and no
  register write re-locks it. Measured at 640x480@60: the pair the engine solves
  puts the source's outermost pixel off the left of the frame when written by
  hand into a frozen acquisition, and four columns inside it once the link has
  re-acquired — same two registers, opposite verdicts, and the frozen one is
  stable and repeatable.
- **A SOURCE MODE ROUND TRIP RE-ACQUIRES THE CAPTURE, AND ONLY A SYNC PAD
  TOGGLE RE-PLACES THE SINK'S WINDOW.** Re-issuing the source's `MODE` while
  automation is FROZEN makes the source leave and return with the hand-set
  register in force, so the engine's own re-acquisition judges it -- which is
  what makes a one-register A/B on the CAPTURE valid, and it agrees with
  `card_edges.py` on both in-scope VESA modes. It never toggles
  `PAD_SYNC_OUT_ENZ`, and the sink's window stays where the last mode change
  put it: measured on 320x256@50 into 1080p, three round trips at 172, 171 and
  172 with the framing zoomed for the middle one. **The sink places its window
  each time the pad is driven again** and remembers nothing between returns --
  158.9 with the picture at the aperture's edge, 171 with captured black there,
  on one state. **WHAT IT READS TO CHOOSE THAT IS OPEN, AND THE IMAGE CONTENT
  IS REFUTED**: see the shown-window entry above, and do not reason from "it
  hunts the first non-black column" in either direction -- a measurement taken
  with picture hard against the aperture's edge reports the APERTURE back, and
  the discriminator is the black at the LEFT rather than where the picture
  starts. So a measurement of where the SINK cuts the line needs a pad toggle
  in the state under test, and a measurement of the capture needs the round
  trip. The dongle needs ten seconds or so to deliver a
  usable frame after either, so a clip taken too early is all black and reads
  as a dead output. Read the register back afterwards -- a frozen unit is not an
  inert one.
  `docs/investigations/the-transmitted-window.md`,
  `docs/investigations/full-screen-framing-on-the-vesa-modes.md`.
- **THE SINK'S OWN POSITION IS PER RASTER AND NO TERM PLACES IT.** Measured
  across the stock AKF50 set into 1080p and 720p with `framing_sweep.py --tier
  S` and read with `framing_report.py --sink`: within an output it steps with
  the field rate in bands, two sources sit off their band, and a transition
  with PICTURE at the aperture's edge lands 5 to 9 units before it where a
  pad toggle on the settled state does not. So a frame judged after a mode
  change carries a placement a settled toggle would not give, and a model in
  T, the clock or the rate cannot close the 50 Hz 1080p rasters.
  `docs/investigations/the-transmitted-window.md`.
- **An open window past the end of the picture is not corruption.** It shows
  whatever the playback stage fetches, which looks like a fault and is only
  absence of data. Establish that there IS picture under the window before
  reading anything as a floor — force the magnification if need be.
- **A MAPPING FROM PHOTO COLUMNS TO OUTPUT PIXELS DOES NOT SURVIVE AN OUTPUT MODE
  CHANGE.** Measured twice: 57 columns adrift after a 1080p/960p/1080p round
  trip, and ~150 columns after a pass-through round trip, each with the raster
  registers read identical either side. Calibrate by differencing a frame at one
  `VDS_DIS_?B_ST` against frames at others, so the difference IS the strip the
  register blanked, and **re-calibrate after any output excursion**. Read against
  a stale mapping, a correct far edge reads as 110 px of overshoot and the
  line's repeat reads as the picture.

  **THE SCOPE IS AN EXCURSION, AND IT IS NARROWER THAN IT READS.** Both figures
  are ROUND TRIPS, and both were taken on the scaling path before
  `Tv5725::OutputMode` stated a standard's timings. Every mode it offers now
  carries one — CEA-861 for 1080p, 720p, 480p and 576p, VESA DMT for the rest —
  and a sink locking to a mode it recognises places it the same way every time.
  **Nothing records the picture moving between two acquisitions while one path is
  held**, and nothing records it on pass-through at all.

  **AND IT IS NOW MEASURED QUIET ON THE SCALING PATH.** `full_margins.py` carries
  the anchor and `counter_origin.py` does not, and on three modes spanning a
  19-sample error range at 1080p they agree within a sample -- so a margin taken
  at 100% framing reads the counter's own placement and needs no re-lock. The
  comparison crosses no output-resolution change and no pass-through excursion,
  which is where the 57 and ~150 column figures came from.

  So a blanking edge found in pass-through is a real edge and does not need
  re-checking against a second acquisition. **Read as a general licence to
  explain away a discrepancy, this entry invents constants**: a pass-through
  blanking offset justified by it rests on one frame photographed with
  automation UNFROZEN, where the registers in force at the exposure are not
  vouched for. Freeze, and read the registers either side of the shutter, before
  reaching for this.

  **THE DISPLACEMENT IS DOWNSTREAM OF THE BOARD, AND IT IS NOW MEASURED.** Both
  checks this entry used to demand have been run. A full `snapdiff.py --save`
  either side covers all 1536 addresses and the byte-identity has been
  *constructed* — writing every differing byte so the dump reports 0 differing
  leaves the picture where it was, as do resets of the video blocks, the SDRAM,
  the phase adjusters and the sync processor.

  **The instrument that localises it is the STV9426 overlay, and it needs no
  probe.** The television's menu is drawn from `HS_OUT`/`VS_OUT` and keyed into
  the video at U13, downstream of the VDS, so it rides the SYNC timebase while
  the picture rides the VDS's counter — one photograph carries both. At two
  landings they move together, overlay -100.78 px / +26.58 rows against picture
  -101.01 px / +26.43 rows, agreeing to 0.23 px and 0.15 rows with the camera
  controlled at lag 0, r = 0.998. Video cannot have moved relative to sync
  inside the scaler, so the analog frame is identical and the position is chosen
  after it.

  **What is still NOT established is which downstream part chooses it** — the
  MS9288A and the television are both there and a second display is what splits
  them — and *why* it differs between acquisitions. So the attribution that
  remains unlicensed is a specific mechanism inside the encoder, not the
  locality. Repeat the overlay measurement before believing any new claim about
  where the position is set; it costs one photograph per landing.
  `docs/investigations/the-transmitted-window.md`.
- **Separate what the board must emit from what one display happens to show.**
  The MS9288A consumes the scaler's analog blanking and generates HDMI blanking
  of its own, so the minimum the scaler must emit is a board property, measured
  once, portable. Where a set stops painting is that set's overscan. Sizing a
  constant from the second reads as a fix and ships one panel's number.
- **BYPASS IS A SECOND VIEW, NOT A PANEL REFERENCE, AND IT IS NOT TRUSTWORTHY
  VERTICALLY.** The set does not overscan, so the panel needs no reference: what
  is on it is what was sent. And bypass has a vertical fault of its own --
  `known-issues.md` carries 800x600 in bypass clipping the top and leaving a bar
  at the bottom, **varying between entries** -- so it cannot anchor a vertical
  measurement. It remains useful as a second path to compare against
  horizontally. An 800x600 source in bypass passes straight through filling the
  screen:
  `printf 'MODE X800 Y600 C256 F60\n' | nc 192.168.88.10 6502`, which that
  source now takes by default — **nothing has to be asked for, and `/uc?x`
  stores the scaling path against it if the comparison wants one**. Take it at the same camera position as
  the frame being judged, compare the two, and `/uc?x` back afterwards.
- **PUT COLOUR AGAINST THE EDGE BEFORE MEASURING ONE.** At a default framing the
  last thing on screen is captured INPUT blanking, so "where the picture ends"
  measures the source's border and not the output's edge — a comparison against
  bypass is out by the whole border. Zoom in and pan until content is hard against
  the edge under test. Panning `-` moves the capture left and puts the image
  against the RIGHT edge; `+` exposes the source's blanking there instead.
- **USE PLAIN `tv-snap`, NOT `--full`, FOR ANY POSITION MEASUREMENT.** The
  rectification is a saved fixed homography in `~/.config/tv-snap/rectify` —
  lens correction, perspective and a scale, applied identically to every shot —
  so the plain rectified capture is what makes two frames comparable. `--full` is
  the keystoned raw sensor frame, and positions measured on it are not
  comparable between shots. No crop is saved (`tv-snap --show-crop` says so), so
  the panel is not cropped to a fixed frame either; setting one would improve it
  further, and the calibration frame has to be pass-through.
- **THE ROOM IS IN THE PHOTOGRAPH, SO ABSOLUTE BRIGHTNESS IS NOT A MEASUREMENT.**
  Held still, `tv-snap` is repeatable to a tenth of a grey level -- four shots over a
  minute on an untouched unit gave mean 88.9, 88.9, 89.0, 89.0 -- and that
  repeatability is exactly what makes the trap convincing. Opening a curtain
  moves it further than anything the scaler does, and the auto-exposure that
  follows lifts the mid-tones while leaving the blacks where they were, which
  reads as a gamma change rather than as light. A picture "looking washed out"
  across minutes is therefore not evidence about the unit, and a register diff
  taken beside it will offer plenty of innocent differences to blame.
  **What survives is a DIFFERENCE between states photographed close together,
  with the same state shot twice as the control** -- the shape
  `docs/investigations/framing-is-anchored-to-a-measured-pulse.md` uses.
- **A FIXED BAND OF A PHOTOGRAPH IS COMPARABLE ONLY WHILE THE PICTURE STAYS PUT.**
  The sync pulse, the playback fetch and the scale all move the picture, and a
  count inside a fixed band then reports the movement rather than the artefact —
  in both directions, so a starved fetch scores *cleaner* than a clean edge. It
  is the same error as measuring a fixed span from a moving origin. Anchor the
  band to a feature of the picture, and print that feature's position with every
  frame so a shift is visible in the results rather than hidden in them.
- **A SHARE OF ZERO DISPLACED FRAMES IS NOT A VERDICT UNTIL A FRAME HAS BEEN
  LOOKED AT.** `picture_stability.judge`'s row-profile correlation finds lag 0
  on whatever part of the frame still correlates, so a band of unwritten memory
  a hundred rows high reads as perfectly still. The H coast stop past the line
  (`SP_H_CST_SP` >= 1728 at 320x256@50) scored 0 of 514 on every trial beside a
  frame that was mostly magenta noise, with the measured sync width at 1 and
  the count pinned at 199 -- and it would have shipped as a fix. Grab a frame
  with `hdmi_capture` and look at it, and read `STATUS_SYNC_PROC_HLOW_LEN` and
  the count beside the share. **The count is a correlate of the roll, not an
  instrument for it**: `SP_H_PULSE_IGNOR` 30 removed every short count and left
  the roll alone, and `SP_DLT_REG` 4095 gave the steadiest count ever seen on a
  frame rolling in 90% of samples.
- **A FRAME DIFFERENCE IS NOT A DISPLACEMENT, AND IT IS BLIND OVER FLAT COLOUR.**
  Scoring a clip on `tblend=all_mode=difference` measures displacement
  MULTIPLIED BY LOCAL CONTRAST, so a whole line can shift across a solid block
  and read zero while a fraction of a pixel on a grating reads large. Per-row it
  then puts the entire score on the test card's frequency wedge, which reads as
  aliasing when the artefact is uniform jitter. **Measure an edge's POSITION
  instead** — average a flat strip to one row, take the centroid of the
  strongest luma gradient, and the spread across frames is the jitter in pixels.
  The bench floor established that way is 0.05 px.
  `docs/investigations/a-hand-set-divider-cannot-be-judged-against-a-solved-window.md`.
- **NO SUBSET OF AN INTERDEPENDENT SOLVE CAN BE SET BY HAND AND JUDGED.** The
  divider is one input to a solve that also sets the capture window, both
  scales, the playback fetch and the stride, so freezing automation and walking
  `PLLAD_MD` measures the MISMATCH against the last solve. It produced a clean
  cliff at 2005 across two line rates with the PLL group identical either side —
  which was `IF_HB_ST2` frozen at 2005, and every "clean" point had the window's
  stop past the end of a shorter line. **There is no count limit below 2047 and
  that figure must not be reinstated.** Compare engine-solved states, or change
  the rule and reflash.
- **An artefact that leaves the screen is hidden, not cured**, and which one it
  is decides whether it travels. Move the thing that generates it back onto the
  panel and re-measure before concluding anything: shift the picture back by the
  same amount the change moved it, then compare like with like.
