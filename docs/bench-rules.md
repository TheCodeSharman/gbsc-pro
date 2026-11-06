# The bench and its rules

Fork of gbs-control for the GBSC-Pro board: a TV5725 video scaler driven by an
ESP8266, taking analog RGB/YPbPr/composite in and HDMI out. The everyday bench
source is a RISC OS RiscPC at 320x256@50 (VTOTAL 311), on `vga`.

**It is not the only source, and a change judged only against it is judged
against one input, one sync type and one scan mode.** A **Wii on the YPbPr
input** is the only source here for sync on green, for interlace, and for the
component colour path -- and YPbPr is a *direct analog* path, so its timings are
its own. Only composite and S-Video go through the ADV7280/ADV7391 chain, which
regenerates them to broadcast standard, so any argument resting on a source being
standard-conformant reaches those two and nothing else.
**BOTH ARE PLUGGED IN AND POWERED AT ONCE, ALL THE TIME, AND SWITCHING NEEDS NO
BENCH TRIP** -- `/input?src=vga` and `/input?src=ypbpr`, so a session judges a
change against both inputs without anyone touching a cable and without asking
first. **Neither source has to be arranged**: assuming the Wii needs setting up,
or proposing it as an experiment rather than reaching for it, is the same
mistake as not using it at all.

**ACQUISITION IS SECONDS ON BOTH INPUTS, SO A WAIT OF MINUTES IS A FAULT AND NOT
A SETTLE.** Measured on `vga`: `/input?src=vga` to `sampling: 311 lines x
50.08 Hz` in 7.2 s, `state: acquired` at 8.1 s, FrameSync steering by 10.4 s,
and the sync-type probe answering `own V sync: yes after 2ms`. Measured on
`ypbpr` with the Wii in 480p: `/input?src=ypbpr` to `sampling: 524 lines x
59.80 Hz -> line rate 31395` in 15.2 s, acquired and holding, `PLLAD_MD` 1096
against `STATUS_SYNC_PROC_HTOTAL` 1096, `SP_SOG_MODE` 1, and a clean legible
picture.
**Do not budget minutes for a source to appear**; a source that has not solved
in about ten seconds is not settling.

**ACQUISITION ON `ypbpr` DEPENDS ON THE OUTPUT MODE, BECAUSE THE OUTPUT DECIDES
THE DIVIDER AND SO DECIDES WHETHER THE SOURCE CAN BE MEASURED.** A ypbpr source
retrying for ever is the open defect in `docs/known-issues.md` rather than a
regression to diagnose. **Read which output is set before judging an
acquisition**, and compare a change against the same one at both ends.
**1920x1080 IS NOT ITSELF THE BAR**: measured 2026-10-20, the Wii at 480p
selected with `/input?src=ypbpr` acquires into 1920x1080 and holds --
`STATUS_SYNC_PROC_VTOTAL` 524, `STATUS_SYNC_PROC_HTOTAL` 1212 against
`PLLAD_MD` 1212, `SP_SOG_MODE` 1, coast 7/6 -- holding across a 183 s run of
27708 samples. An earlier reading that it never acquires there is superseded.

**AND `/geometry`'s `state` IS STALE ACROSS AN INPUT CHANGE.** Four seconds
after `/input?src=ypbpr` it answered `acquired` while
`STATUS_SYNC_PROC_VTOTAL` and `HTOTAL` both read 0 -- the field left over from
the source that just went away. **Poll the COUNT, not the state**, or an
acquisition is judged before detection has run.

**ESTABLISH WHICH WII OUTPUT MODE IS SET BEFORE JUDGING THE INPUT.** All three
solve and hold. 480p solves in 15 s. 480i alternates 259/260 and still acquires,
because `SteadyRun::agree()` treats a pair alternating by one as agreeing -- an
earlier reading that it never reaches `state: acquired` predates that. 576i
holds a steady count of 310 and solves in **about three seconds**, measured from
the `/input` request to `sync pad: driven`; an earlier reading of 40 s is
superseded.

**AND ONLY 480i IS DEINTERLACED.** 576i is steered as progressive and shown as a
single field, line doubled -- correct in shape and aspect at half the vertical
resolution, which is invisible without a reference. The same is true of the RISC
PC's `INTERLACE ON` modes. `docs/investigations/interlaced-source-measurement.md`.
`docs/investigations/mode-detect-answers-before-any-measurement.md`,
`docs/investigations/two-owners-of-the-coast-lengths-double-the-count.md`.

**ALL THREE SYNC ARRANGEMENTS ARE ON THE BENCH AND ALL THREE ARE SCRIPTABLE.**
The RISC PC's sync type is one CMOS value rather than a mode-file setting, and
ModeServ sets it -- `SYNC 0` separate, `SYNC 1` composite, `SYNC 3` auto -- so
**a COMPOSITE-SYNC RGBHV source exists here**, on `vga`, and a branch keyed on
csync with RGBHV is bench testable rather than host-test-only. **WHICH MODE
DECIDES WHETHER IT ACQUIRES**: 640x512@50 acquires and fills the screen,
800x600@60 acquires and emits nothing, and **1280x720@60 never acquires on
composite at all** -- so a composite measurement taken at 720p measures that
open defect rather than the thing under test. `docs/known-issues.md`. Sync on
green is the Wii on `ypbpr`. No sync arrangement is out of reach from a session.

**`docs/bench-sources.md` is what each source can prove; read it before
concluding a branch is untestable**, and it carries what each one is currently
known to do.

**FIX THE SCALER, NOT THE SOURCE. THE UNCONFIGURED SOURCE IS THE TORTURE
TEST.** The firmware accepts whatever timings arrive, and bending the source to
suit it is a closed question rather than an open trade-off. The reason is
representativeness and not illegitimacy: authoring an MDF to match a panel is
exactly what the mechanism is for and would work, but a Wii cannot be retimed
and most retro machines emit what they emit, so arranging the one source that
CAN be arranged tunes the scaler to the least representative case and hides the
problems it exists to solve. The `RetroScale` MDF is the abandoned attempt --
a custom 320x256 entry tweaked until the picture went fullscreen, correct for
predicting that machine and carrying no authority about RISC OS timings. So no
mode-file edit, per-source lookup table or anything keyed on one machine's modes
is a runtime mechanism; the firmware cannot read an MDF at all. Defaults come
from what the chip can MEASURE, with a trim control for the rest.

**THE OUTPUT IS READ BY A USB HDMI CAPTURE, NOT BY PHOTOGRAPHING THE PANEL.**
`hdmi_capture.py` gives the emitted frame itself, so a margin is a number rather
than a judgement and two states compare without controlling the room. The
television runs beside it off the dongle's loop-out, so both views are live at
once. **A BLACK CAPTURE IS A BAD SCALER CONFIGURATION UNTIL THE BOARD HAS BEEN
RULED OUT.** Measured: the blocks the low-power teardown holds down
(`SFTRST_MEM_RSTZ` and its four neighbours in `s0_46`) were left held after the
source acquired, and the part emitted nothing while every configuration register
read correct -- a clean `/getregs` is no evidence here. Ask `s0_46`, then `s0_45`
and `s0_49`, then the console's `frame time lock` line, whose `out` rate proves
the scaler is feeding the encoder.
**An unseated HDMI input at the dongle does the same and neither power cycle
clears it -- but it is the LAST resort rather than the first**, because a
connector that has been seated stays seated, and reaching for it ahead of the
board is how a real fault gets a session spent on the cable.
`docs/bench-output-capture.md`.
