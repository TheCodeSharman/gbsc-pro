# The diagnostic toolkit

Which instrument to reach for is above -- the console first, then `/geometry`,
`/samplinglog`, `/testbus`, fields read by NAME, the HDMI capture. This is what
to DO with them, and every entry here has had to be arrived at more than once.

### Whose black is it: lift the picture

A border is black ACTIVE video and our blanking is black, so counting black at
an edge cannot tell them apart and "is the picture flush" is unanswerable until
they are separated. **`VDS_Y_OFST` lifts the PICTURE's luma, the source's own
border with it, while the VDS blanking interval keeps the blanking value** -- so
after a lift every black row or column still at an edge is OURS and everything
grey is the source's. It needs `VDS_DYN_BYPS` 0, or the offset reaches nothing.

Measured on `vga` at 320x256@50 into 1080p: 7 black columns at the left and 2
rows at the top before the lift and **0 and 0 after**, so both belonged to the
source; 14 columns at the right under both, so **all fourteen were ours**.
Without it the two edges read alike and a deficit gets attributed to the wrong
end of the line.

**IN PASS-THROUGH THE LIFT REACHES NOTHING AND `HD_MATRIX_BYPS` 1 IS THE
ANSWER.** Video routes around the VDS there, so `VDS_Y_OFST` is out of circuit.
Bypassing the colour matrix leaves the picture green and the source's border
dark magenta at G exactly 0, which separates the same two blacks in one
register. Measured on the Wii at 480p into 1080p: border at the right ~50
emitted columns, bottom ~40 rows, top ~3, and **none at all at the left** --
so the bars a pass-through frame shows are the source's, and the left one is
not arriving. **It also refutes the black-hunt model**, because the border is
not black in that state and the window still does not reach it.
`docs/investigations/the-transmitted-window.md`.

### Move the picture without disturbing the parity bias

The memory window's WIDTH parity is the zoom shear, so a probe that moves one
end of it changes the thing under test. Three shapes, each asking a different
question:

| move | what it asks | parity |
|---|---|---|
| `VDS_DIS_?B_SP`/`ST` alone | where the SINK cuts, and where the write reaches | no rule on the display window, so safe |
| all four by the same delta | where the picture COULD go | both widths intact |
| `VDS_?B_SP` alone | whether an artefact belongs to the WRITE or the aperture -- the write's own features follow it 1:1, the aperture's do not | **flips it**: move `VDS_?B_ST` with it |

### Creep in whichever direction the state calls for

A boundary found by bisection is only the real one if nothing lies beyond it,
and a creep that assumes which way the error runs finds nothing when it runs the
other way. Read the edge at the solved value FIRST, take the direction from
that, and creep a unit at a time until the state flips; the reading is the last
value before the flip, whichever side it started from. A walk that returns
nothing after its whole range is a walk whose premise was wrong, not an absent
boundary.

### Poll the registers across the change, not just the console

The console says what the engine DECIDED; it cannot say what moved first, and
its timestamps are delivery times that compress. A cascade -- a register we
write, then the count, then something keyed on the count -- is only visible as a
timeline of registers sampled a few milliseconds apart across the event. Where
the device's own figure for an interval exists, that is the one to believe.

### Compare against something that was working

Two baselines, and they answer different questions:

- **A known-good snapshot** from `tools/gbsc-pro-hwtest/snapshots/`, diffed with
  `snapdiff.py`. This says what OUR change moved. Take `--save` at both ends
  when the question is what survived an excursion, because the config dump
  answers a narrower question than it appears to.
- **The vendor image.** The repo's own 1.x tags are the stock board firmware, so
  a fault that might predate the rewrite can be scored against what the board
  shipped doing -- often by reading those sources rather than by flashing them.

  **DO NOT FLASH A STOCK TAG TO TAKE A REGISTER COMPARISON. IT CANNOT ANSWER
  ONE.** Upstream `1.3` registers no register route at all and its
  `dumpRegisters()` has every print commented out, and `be58a24f4` has
  `/getreg` but no `/getregs`, no `/input`, no `/samplinglog`, and a console
  heap gate of 20000 that this fork never reaches -- so the websocket takes a
  client and delivers nothing. Flashing one and then discovering there is
  nothing to read costs the flash, the comparison and the trip back.

  **`tools/reference-build/` is the answer and it is already built.**
  `be58a24f4-bench-instruments.patch` adds `/input`, `/samplinglog` and a
  usable console gate to the reference commit and nothing else; its `README.md`
  has the build line. Both sides are kept as images in
  `~/Projects/gbsc-pro-backups` -- `esp_ref-be58a24f4-instrumented_*.bin` and
  `esp_current-<rev>-samplinglog_*.bin` -- so a switch is one `flash-bin.sh`
  rather than a rebuild.

  Three things that bite on the way:
  **a full snapshot is 1536 single reads there**, about 90 s, `/getregs` being
  absent; **the switch reformats the filesystem each way**, SPIFFS against
  LittleFS, so `/framing.txt`, `/slots.bin` and the preferences go unless they
  are pulled over `/fs/download` first, and `SeleInputSource` comes back 0 with
  a `default:` case that transmits nothing, so the input needs reselecting;
  and **its detection sweeps for a live signal**, so the source with sync on
  another ADC input wins whatever is asked for -- with both bench sources
  powered, that build cannot be held on the one under test.

### Keep the shape and the pan still between readings

`reset_framing()` resets the declared ASPECT as well as the framing, so a run
that calls it to get back to a known state drops a 16:9 source to the raster's
default and narrows the room -- two readings either side of one are taken on
different pictures. Read `aspect` and `shaped` out of `/geometry` with every
measurement and put the shape back with `/uc?G`, which cycles it.

The pan matters the same way, and `framing_to()` lands on the granule rather
than the value asked for, so read `oh` back rather than assuming it.

### A black capture after a session of poking

Every register reading correct with nothing on the capture is the encoder
holding a stale timing, and the recovery is the `PAD_SYNC_OUT_ENZ` toggle rather
than the connector or a power cycle. Frozen poking across a source mode change
is what provokes it, because nothing re-presents the output while automation is
held. Measured: resets all 1, pad enabled, `STATUS_SYNC_PROC_HTOTAL` equal to
`PLLAD_MD`, `state: acquired` -- and a black frame until the pad was dropped and
driven again.
