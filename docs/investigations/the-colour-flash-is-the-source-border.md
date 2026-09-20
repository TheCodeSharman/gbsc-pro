# The pass-through colour flash is the source's border

In pass-through the whole picture alternated between a red cast and a green one,
about every few seconds, on a source and a scaler that were both sitting still.
It was read as a fault in the scaler's colour path for a long time. It is not a
scaler fault at all: it is the RISC PC's own border, and turning the border flash
off stops it.

## What it looked like

RISC PC on `vga` at 800x600, pass-through, ModeServ drawing PM5544. Two frames
seconds apart: the entire card red in one, entirely green in the next -- not the
border alone, the whole picture including the greyscale and the white ellipse.

**The two casts are the COMPLEMENTS of the two border colours.** ModeServ's
liveness animation flipped the screen border cyan and magenta twice a second
(`VDU 19,0,24,0,255,255` against `255,0,255`). Cyan border gives a red picture;
magenta border gives a green one. Subtracting a colour from everything is what a
wrong black reference does.

## The intervention that identifies the trigger

`BORDER OFF` -- ModeServ now defaults to it -- leaves the border black and
flips only the ring and corners inside the picture. Three frames over twelve
seconds are then identical and correct: greys neutral, colour bars right, white
white. Nothing else changed between the two states, and the scaler was not
touched.

## What it is NOT, each measured

Every one of these was checked while the flash was running, and none of them
moves it:

| candidate | measurement |
|---|---|
| the colour registers | `HD_MATRIX_BYPS`, `HD_DYN_BYPS`, `DEC_MATRIX_BYPS`, `IF_MATRIX_BYPS`, `VDS_CONVT_BYPS` and `ADC_RYSEL_*` read identical between a red frame and a green one |
| ADC gain, the auto-gain loop | `ADC_RGCTRL`/`GGCTRL`/`BGCTRL` sat at 123/123/123 across fifteen samples over 24 s while the picture alternated |
| ADC offset | 64/64/64, unmoved |
| the DAC channel enables | `DAC_RGBS_R0ENZ`/`G0ENZ`/`B0ENZ` all 1 |
| the ADC clamp window | moved from `SP_CS_CLP_ST` 19..108 -- inside the sync pulse -- to 250..350, the back porch for this raster. **No change**, still alternating |
| the output raster | it casts at `STATUS_SYNC_PROC_VTOTAL` 627, the VESA total, as readily as at 679 |
| our firmware | present on `be58a24f4`, the 2026-08-02 build. **Never a regression** |

## What is still open, which is the substance of it

**Which stage reconstructs black is not localised, so the fault is not
understood.** The intervention proves the border triggers it; it does not say
what reacts to the border, and avoiding a trigger is not a fix. It is not the
TV5725's clamp window, its gains or its offsets, all of which were held or moved
without effect. What is left in the path is the MS9288A, which samples the analog
output and re-encodes it, and which is on no I2C bus, has no register map and
configures itself from mask ROM -- so if it is doing its own level detection,
nothing here can observe or stop it.

That matters for more than this card, and is why this stays open: **any source
whose picture content swings its average level may move the output's black in
pass-through**, and there is no scaler-side control to hold it. A static border
avoids it on this bench and nowhere else -- a console with a flashing title
screen would do the same thing and cannot be told not to.

What would advance it: a source that swings its average level WITHOUT a border,
to separate "the border specifically" from "the average level of the frame"; and
a scope on the analog output during the swing, to see whether black is moving on
the wire before the encoder or only after it. Neither needs the scaler changed.

## Consequence for the source

`docs/bench-sources.md` and the `RiscPc` repo's `video-source` carry the change:
the border flip is `BORDERFLASH%`, off unless asked for, because a test card
exists to be measured through and one that moves the black reference is
measuring the wrong thing. The card stays visibly alive -- the ring and corner
flips are inside the picture, which is where a liveness signal a scaler can see
has to be anyway.
