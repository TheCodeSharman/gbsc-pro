# The clamp window is a fraction of the line, so it ignores the sync width

**Status:** open. The mechanism is placed and the code is named; the exact
geometry of what the clamp lands on is not measured, and the sweep that would
settle it is at the end.

`updateClampPosition()` places `SP_CS_CLP_ST`/`SP` as a fixed proportion of the
line and consults nothing about the sync:

```cpp
float multiSt = isCsync ? 0.032f : 0.010f;
float multiSp = isCsync ? 0.174f : 0.058f;
start = 1 + (accInHlength * multiSt);
stop  = 2 + (accInHlength * multiSp);
```

`accInHlength` is `STATUS_SYNC_PROC_HTOTAL` on a separate-sync source, which
echoes `PLLAD_MD`. So the window is 1.0% to 5.9% of every line whatever the sync
underneath it is doing. On the bench that is 23..132 of 2250, and it reads back
as exactly that.

`STATUS_SYNC_PROC_HLOW_LEN` measures the sync width and is not consulted.

## What it costs: the picture takes the border's colour

The bench source flashes its border magenta and cyan. In a mode where the window
misses, the whole picture flashes with it, in phase.

Mean frame colour, sampled about 0.4 s apart on the RISC PC at 640x256@55:

```
PM5544    R=150 G= 67 B=128     magenta
          R= 35 G=174 B=136     cyan
CARD      R= 70 G= 77 B=117     the same two states, smaller swing
          R= 51 G= 94 B=121
```

Bimodal with no intermediates, and R and G move in opposition while B stays put
-- which is magenta against cyan, the border's own two colours. The amplitude
follows the picture content, so it is not the source drawing a flash over the
whole screen.

**The ADC is not doing it.** `ADC_RGCTRL`/`GGCTRL`/`BGCTRL` read 123 and the
offsets 64 throughout, unchanging across the flash.

**It is at or before capture.** With `CAPTURE_ENABLE` 0 the output holds one
frame and the alternation stops dead -- R=145 G=86 B=153 over six frames -- so
nothing in playback, the VDS or the encoder is involved.

## Why it is mode dependent

The window does not move; the sync does.

| | `HLOW_LEN` | as % of line | clamp 23..132 | picture |
|---|---|---|---|---|
| 320x256@50 | 159 | 7.1% | ends inside the sync pulse | stable, correct |
| 640x256@55 | 80 | 3.6% | ends 52 samples past the sync edge | flashes with the border |

`SP_VTOTAL` is 311 in both, so the line count is not what distinguishes them.
Four frames of the 50 Hz mode read R=116..119 G=175..177 B=230, no alternation.

## What is NOT established

**That the clamp overlaps the border.** The window ends at ADC sample 132 and
the capture window starts at 264, so the arithmetic does not by itself put the
clamp in active picture -- the back porch of this mode has not been measured.
What is established is that the clamp position changes the cast, that the
artefact is in phase with the border, and that the mode dependence runs the way
the fraction predicts.

Moving the window to 96..176 changed the cast from magenta-dominant to
green-dominant and did **not** stop the alternation, so a different placement is
not on its own the fix and the window is not the only thing involved.

## The measurement that would settle it

Creep `SP_CS_CLP_SP` down from 132 in the failing mode, one unit at a time with
automation frozen, and find where the flashing stops. That boundary is the start
of whatever the window is sampling, in the register's own units, and it can be
read straight against `HLOW_LEN`. Bisecting would miss it if the transition is
banded, which is why it is a creep.

Both states have to be photographed at each step, because a single frame lands
on one phase of the flash and looks like either answer. Two frames about 0.4 s
apart, compared as mean colour, separate them.

## Where the fix belongs

`docs/input-acquisition.md` step 5 moves the clamp window to
`SyncProcessor`. The placement wants the measured sync edge rather than a
proportion of the line, and `HLOW_LEN` is already read on the same pass.
