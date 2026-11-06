# "No HDMI" with every register perfect

Seen four times in one evening. The scaler can be locked, preset loaded, DACs
powered, sync output enabled — and the TV still says no signal. Registers cannot
distinguish these:

1. **The output clock is not running.** `PLL648_CONTROL_01 == 0x75` is a
   *sentinel the firmware wrote* meaning "the Si5351 drives the display", not a
   measurement that it does. Diagnostic: watch the console. **Do not infer which
   vsync sample failed — the firmware now says.** `vsyncPeriodAndPhase()` prints
   `no INPUT vsync` or `no OUTPUT vsync`, and `runFrequency()` names its failing
   check. A stream of bare headers with no outcome means a pre-`f5bb2b0` build.
   *"The input is locked, so it must be the output sample"* was inferred twice
   and is wrong: measured on the bench unit with the source locked and the TV
   dark, it is the **input** sample that times out — a measurement-path fault at
   `DEBUG_IN_PIN`, not a video-path one. And because `runFrequency()` returns
   early, the Si5351 never gets adjusted at all.
2. **The encoder is transmitting the PREVIOUS timing.** It samples the analog
   output and does not always notice the timing under it moved, so it carries on
   with the mode it locked to before. **What identifies it is the rate the TV
   reports** — a sink that rejected a mode reports no mode, a sink holding a
   stale one names the *old* rate.

   **Toggle `PAD_SYNC_OUT_ENZ` (s0_49 bit 2) before pulling the rails.** Set it
   to 1, pause, set it back to 0: dropping HSOUT/VSOUT makes the encoder
   re-acquire, and the picture comes back at once with no power cycle. Entering
   RGBHV bypass does the same the long way round. **The firmware already does
   this on every source mode change and every output change**: the arm takes
   the aperture and the pad away before the first register the setup moves,
   and `VideoSourceAcquisition::presentWhenSettled()` gives both back once,
   after the divider has latched, the caller's rate match has landed and the
   sampling phase has been searched, and never inside `MinimumSyncAwayMs` of
   the drop. Nothing else presents the output: a solve that completes while it
   is away leaves presenting to that pass. So `sync pad: away` on the console
   is the arm and `sync pad: driven` is the end of the setup, and a divider,
   rate match or phase line AFTER the driven line is a sequencing fault.
   `docs/investigations/encoder-stale-timing.md`,
   `docs/investigations/the-transmitted-window.md`.

   A power cycle — mains *and* USB, since USB backfeeds the rails — remains the
   fallback for a state the toggle does not clear.
3. **The TV timed out** and dropped the input.
4. **The link is not connected.** Which sounds beneath mentioning and has cost a
   whole session, because the guidance below says the connector is the LAST
   resort and that is right only while a cable is known to be IN.

## TWO READINGS SAY "PHYSICAL LINK" AND NO CONFIGURATION FAULT CAN FAKE EITHER

Reach for these before any raster or clock arithmetic, because they are cheap
and they are decisive.

**EVERY OUTPUT MODE BLACK IS NOT A CONFIGURATION FAULT.** `/output?res=` the
whole list. A mode selection, a clock ceiling or a raster that cannot carry its
own active width reaches SOME modes and not others -- that is what makes it a
configuration fault. `720x480` costs 31.6 MHz against a 108 MHz ceiling and
`1280x720` 86.8 MHz, so if those are dark alongside `1920x1080` there is
nothing left to misconfigure.

**AND THE OSD IS THE SECOND INSTRUMENT, BECAUSE IT IS NOT OUR PICTURE.**
`Osd::OSD` is the STV9426's own overlay, generated on the board and keyed into
the video at U13, DOWNSTREAM of the VDS -- so it rides to the sink whatever the
capture window, the scale or the geometry are doing. `/menu?key=menu` opens it
and answers the page it drew. **An open menu that does not reach the sink is
not a picture fault**, and that is true whether the frame under it is black,
green or correct.

Measured 2026-11-07: a 449-line 70.157 Hz source, `state: acquired`, the
console's `frame time lock` holding `in 70155 mHz, out 70156 mHz` so the VDS
was feeding the encoder, `sync pad: away` then `driven` across the change,
`s0_46` 0x7f, `DAC_RGBS_PWDNZ` 1, `STATUS_SYNC_PROC_HTOTAL` equal to
`PLLAD_MD` -- six output modes dark, the OSD open and absent, and the HDMI
cable out. **Four hypotheses were built and refuted on the bench before the
connector was asked**, which is the cost of taking "seated stays seated" as
covering a cable that was never in.

**A `/getregs` THAT READS PERFECT IS WHAT THIS LOOKS LIKE**, so the register
dump is not the instrument. The engine reports health because the engine IS
healthy; everything it can see is downstream of nothing.

**"IT IS THE ENCODER" IS THE STANDING WRONG ANSWER ON THE SCALING PATH.** Two
occasions where it genuinely was the encoder bound how far that reaches, and
neither licenses reaching for it again:

- **A raster that is not a standard one confuses it**, and the symptom is
  framing that JUMPS WITH NO REGISTER CHANGE. That is what `Tv5725::OutputMode`
  stating each mode's own standard timings fixed: emitting CEA, or close to it,
  stopped the jumping. So a standard raster is the CURE, and a non-standard one
  is the condition -- not the other way round.
- **In bypass it locks to the actual image**, which turning the source's border
  on is enough to move. **Why bypass and not the scaling path is not known.**

**The second of those is itself the argument that it is not the encoder while
scaling: if it locked to image content there, PAN WOULD NOT WORK.** Panning
moves the picture inside an unchanged raster every day, and the lock does not
follow it.

**Refute it before carrying it**, because it is cheap to: FREEZE automation and
change one output register. If the picture moves as a whole, the placement is
downstream; if a strip is blanked and the rest correlates at zero shift, it is
the board. A row-averaged profile of two photographs settles it in one command.

**A COMPARISON ACROSS TWO ACQUISITIONS CANNOT ANSWER THAT QUESTION**, and it is
what makes the encoder look guilty. Two frames either side of a mode change, an
input change or a sync-type round trip differ by a displacement nobody wrote.
Freeze, take both frames inside ONE acquisition, and the displacement is gone.

**THE DISPLACEMENT IS REAL, BUT "THE WINDOW FOLLOWS OUR BLANKING" IS REFUTED --
DO NOT REACH FOR IT.** What holds is that the shown window is established when
the link ACQUIRES and held until it acquires again, downstream of the board:
across one such lock the STV9426 overlay -- generated on the board and keyed in
at U13 -- moved 124 photo columns with the board frozen and no sync register
touched, which nothing on the board can do.

**What is refuted is that it takes its origin from `VDS_DIS_HB_SP`.** Measured
at 320x256@50 into 1080p, the window sits at our unit 161.6 whether that
register is 143 or 200, across a source mode round trip and across a
`PAD_SYNC_OUT_ENZ` toggle; the toggle moves it about 11 columns between two
landings and onto neither value. **The black at the LEFT is the discriminator**
-- a window that followed would paint our first column at column 0 and show
none, and a reading that only asks where the picture starts is reading our own
aperture. That confound is what cut `OutputMode::TransmittedWindowDelayPx` from
20 to 3 and left 25 of 1920 columns black at the right.
`docs/investigations/the-transmitted-window.md`.

**THE SHOWN WINDOW MOVES WITH SOMETHING THE OUTPUT REGISTERS DO NOT DESCRIBE,
AND WHAT IT IS REMAINS OPEN.** Latching to image content is the obvious reading
and it does not survive the arithmetic, so do not carry it as settled either
way.

Measured on `vga` at 320x256@50 into 1080p with the RASTER, the APERTURE and the
SCALE all held -- `VDS_DIS_HB_SP`/`ST` 161..1831 and `VDS_HSCALE` 421 at every
reading, `VDS_HB_SP` 42, only `IF_HB_SP2`/`ST2` panned ten units. Our blanking at
the far end, read with the picture lifted: **15, 2, 14, 2** across four sync-pad
re-latches at pans of 237, 247, 237, 247. Reproducible to a unit, and twelve
units of shown window on a pan the emitted timing cannot carry.

The same pan on 800x600@60, again every output register identical, moves the
LEFT instead: 2 columns of ours at the default pan and 17 ten units in, and the
lift raises none of the 17.

**What rules out the easy explanation is the scale.** A pan of ten capture units
displaces the CONTENT by ten times the magnification -- 24.3 output units on the
doubled 320x256 at 2.43x, 12.7 on 800x600 at 1.27x -- while the window moved
about ten output units on BOTH. A sink choosing its window from the content it
receives cannot land on a figure that tracks the pan in CAPTURE units, because
nothing downstream of the VDS can see them.

**So the candidates are still open**, and two of them are instruments rather
than the board: the MS9288A, the television, and the USB capture dongle, which
is itself a sink with a latch of its own. Splitting them needs the same two
states read on two sinks at once.

**Bypass is NOT a way to get a picture out of an unknown source.** It passes the
source's own timing to the encoder, so it only works where the DISPLAY can show
that timing -- the bench 800x600 does, and a 15 kHz mode does not. Reaching for
`/sc?k` to recover a source that would not scale gives no signal at all, which
reads as the bypass having failed rather than as the TV refusing 15 kHz.
`/sc?~` leaves bypass and re-detects.

**`VDS_ENABLE == 0` is not evidence of no output.** In RGBHV bypass the video
path does not go through the VDS at all, so an empty segment 2/3 is expected and
the unit is still driving the encoder. Reading it as "nothing is being sent" is a
mistake that has been made and cost a wrong diagnosis — bypass produces a working
800x600 picture. See `docs/rgbhv-bypass-trap.md`.
