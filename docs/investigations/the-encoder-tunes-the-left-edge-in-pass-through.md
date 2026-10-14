# The encoder tunes the left edge in pass-through

Horizontal placement on the RGBHV pass-through path is chosen by the MS9288A
when it locks, not by the registers the scaler writes. So there is no channel
delay to measure and no offset constant to carry: a value fitted to where the
picture sits describes one of the encoder's landings, and the next lock replaces
it.

**This is why pass-through frames a picture the scaler never framed.** Nothing
on this path computes where active video sits — `HdBypass` plays the source's
raster out untouched and blanks to a fraction of the line — and a correctly
placed picture arrives anyway. The encoder is doing it, which is also why the
result moves without a register moving, and why every attempt to derive the
placement from the scaler's own registers fits a constant to one landing.

## What is measured

Changing `HD_VS_ST`, which cannot affect horizontal placement, brings the
source's border back into frame. Vertical sync is not an input to where a line
starts, so the only thing it can have done is provoke a re-lock, and the re-lock
re-placed the picture.

That matches the eight landings in
[the-picture-position-is-latched-not-re-rolled.md](the-picture-position-is-latched-not-re-rolled.md):
a natural acquisition at dx 0 and seven re-locks forced with `PAD_SYNC_OUT_ENZ`
all at dx −8, every board register identical throughout. Landings are discrete
and a forced one is repeatable; what differs is which landing a given lock
arrives at.

**The clip measurements of those re-locks do not stand**, and the reason is
worth carrying: three of the four clips have a phase split of 12/108, 15/105 and
28/92 where an even one is what a 2 Hz flip gives. The phase had locked onto a
transient — a re-lock settling — and the profile that came back describes that
transient, at tens of grey levels where the flip reads at one or two.
`flash_edges.py` refuses a clip on that split now. A landing difference in
columns needs clips taken after the picture has settled.

## Three models this refutes

**A fixed shift between the blanking window and the video.** A pass-through
window at `HD_HB_SP` 431 / `HD_HB_ST` 1975 reads as the engine's 417 / 1961 plus
14 on both edges, width unchanged. The near edge was off the panel at the sync
position in force, so creeping it moved nothing visible and the placement came
from the far edge alone, doubled onto both. **A measurement through an edge the
display does not paint is not a measurement**, and the width was never in
question: the solved 1544 matches the painted 1543 at every landing measured.

**A one-for-one response to the emitted sync.** `HD_HS_ST` 40 paints from sample
445 and 33 from 438, which looks like the sink's window tracking the sync. Two
landings cannot separate that from a write provoking a re-lock that lands seven
away, and a vertical sync change — which cannot affect horizontal placement —
moves the picture too.

**A channel delay behind the emitted sync's position.** The same 40 at 800x600@60 and
640x480@60 reads as the channel's own latency. Two modes agreeing is what a
constant looks like *and* what two locks arriving at the same landing looks
like. Nothing distinguishes them without provoking a re-lock at a fixed mode,
which moves the picture with the constant unchanged. `HdBypass`'s
`ChannelSyncStart` holds the value and no longer claims to measure anything.

## The instrument

The card's liveness animation makes both boundaries readable from one clip, with
nothing on the scaler touched. `PatLib` flips the screen border cyan/magenta and
four corner blocks yellow/white off one phase, so `R−G` carries the border and
is zero on the corners, while `B` carries the corners and is flat across the
border. The corner blocks are `CW% DIV 3` by `CH% DIV 3` and identical by
construction, so a picture running off either end of the panel returns that
corner narrower — which is the acceptance test, needing no blanking change and
no column-to-sample mapping.

**Phase-locked averaging is what makes it readable.** The flip is three grey
levels through the bench camera, which a frame difference cannot find. Splitting
the frames by phase and averaging each half takes the noise floor to 0.05 levels
and reads the border at 57 sigma.

Two traps in it. Chroma subsampling leaves crosstalk where border and corner sit
side by side, so the clean reading is the corner with no border beside it. And a
border-detection window fixed in photo columns counts the corner block as border
once a landing moves the corner into it — anchor it outside the detected picture
edge.

## What the blanking window is

`HD_HB_SP` 340 / `HD_HB_ST` 2037 is the source's **active video** extent.
`RetroScaler-Acorn.mdf` gives 800x600@60 as `128, 48, 40, 800, 40, 0` — sync
128, back porch 48, border 40, display 800, border 40, front porch 0 — so active
video runs 176/1056 to 1056/1056, which on a 2038 line is 339.7 and 2038.0.

`HdBypass` computes 417 / 1961 from `SourceTiming`, which is VESA DMT's
**display area**, 216/1056 and 1016/1056. Both descriptions agree on the total
and the sync width and disagree on the 40 pixels at each end that this source
emits as border and DMT spends on porch. A border is active video, so blanking
to the display area crops it.

Since the encoder places the picture, the window is not what frames it. Blanking
only has to keep sync and back porch off the screen, so it biases wide and a
border sliver at one end is the expected result rather than a fault.

**So a source's departures from the standard it matched do not matter here.**

## The window is not read by the encoder at all, and that retires the branch

`PAD_BLK_OUT_ENZ` is 1, so HBOUT and VBOUT are off: the encoder takes the
analog RGB with HSOUT/VSOUT and nothing else. `HD_HB_SP` therefore cannot state
where active video is. It can only paint black over video the channel sends.

**Keeping a border visible was the reason for consulting the published raster,
and it is void.** Whichever edge the window states, the border at the left of
the frame does not arrive and the border at the right does -- blanking to a
standard's sync extent cannot make the left one visible, and blanking to its
display area cannot make the right one invisible. Nothing the window does
reaches the question.

**The reason given here used to be that the encoder skips a BLACK border, and
that is refuted below**: bypassing the colour matrix leaves the left border
magenta on the wire and it still does not arrive. The conclusion stands, the
mechanism behind it is open.

What the edge does decide is how much blanking sits inside the emitted active
area, which the encoder shows as a bar. Measured on the Wii at 480p through the
channel's 2038, each value forced to re-lock with `PAD_SYNC_OUT_ENZ` and read
off the USB capture:

| `HD_HB_SP` | 147 (the raster's sync extent) | 238 (the envelope) | 290 |
|---|---|---|---|
| black at the right | 251 columns | **56** | 117 |

238 read 56 on three consecutive forced re-locks, so the response belongs to the
register rather than to which landing the lock arrived at. It is **not
monotonic**, which is the same shape as the three models above and the reason no
offset constant is fitted here either.

So the envelope is the one answer on both branches, and `HdBypass` no longer
reads `SourceTiming` horizontally. The bound that remains is that the envelope
may never reach where a source puts video -- 0.117 of the line against the Wii
480p's 122/858 and the AKF50 800x600's 176/1056 -- because bypass has no framing
control to give cropped picture back with.

## What is left is the source's own border, moved to one edge per axis

With the envelope in force the Wii at 480p emits 56 columns of black at the
right and 51 rows at the bottom, against 0 and 1 at the left and top. Measured
with the matrix bypassed, the right bar is about one border width rather than
two -- 50 emitted columns is roughly 19 source pixels against the 15 the
source's own border spends -- so the picture is DISPLACED by a border width,
not stripped of both. The
asymmetry is not the channel's: removing the vertical window entirely, through a
re-lock, leaves the bottom bar where it was, and the registers are live --
blanking 120 lines puts 188 rows of black at the top.

It is the source's safe-area border, which `known-issues.md` measures at about
15 px left, 17 px right, 15 lines top and 9 lines bottom, with the encoder's
content origin moving all of it to one edge of each axis. The vertical fits
closely: 24 border lines of a 480-line window is 54 rows of 1080 against the 51
measured, with 1 at the top. Nothing on the board can reach it.
Whether this source spends 88 pixels on back porch like DMT or 48 plus a 40
pixel border, the encoder places the picture either way and a window biased wide
covers both. `SourceTiming`'s precision is earned on the SCALING path, where the
capture window is what frames the picture and a centred default clips every VESA
mode — [vesa-modes-are-clipped-by-default.md](vesa-modes-are-clipped-by-default.md).
On pass-through the match only has to be close enough not to blank picture.

## A solve is not judged with the source's border on

`BORDER ON` puts non-black video between the sync and the standard's active
start, which is the one thing that moves the encoder's hunt. The whole border
then arrives on the panel and the far end of the picture leaves it, which reads
as a framing fault in the scaler and is the source doing what it was asked.

The border is what makes an edge measurable, so it has to be on to take a
measurement and off to judge the result. Measured at 800x600@60 with the border
on, the entire 40 pixel border is on the panel at a flash amplitude of 230
levels and the right end of the picture is cut. With it off and the encoder
re-locked, the card fills the panel with its castellation complete at both ends
and the sink reports 800x600/60Hz.

## The border is readable: bypass the colour matrix

`HD_MATRIX_BYPS` 1 leaves the picture green and renders the source's border
dark magenta at **G exactly 0**, so the two blacks this page could not separate
-- our blanking and the source's border -- are separable in one register. It is
the pass-through counterpart of lifting the picture with `VDS_Y_OFST`, which
reaches nothing on this path.

This retires the instrument problem the previous Open section described. The
border-off half needed the RISC PC's card and a phase-locked camera clip; the
board renders the border itself, on any source, read off the USB capture.

## The border reaches three edges and not the left

Measured on the Wii at 480p into 1080p, `HD_HB_SP` 238, the frame completely
filled at 1920x1078:

| edge | border, in emitted pixels |
|---|---|
| left | **none** -- column 0 is picture, G 239 |
| right | ~50 |
| top | ~3 |
| bottom | ~40 |

So the transmitted window sits about one border-width right of where it would
have to be to carry the whole source: the left border falls outside it and the
right border is wholly inside it.

**AND THE BORDER IS NOT BLACK IN THAT STATE, SO "THE ENCODER SKIPS BLACK" DOES
NOT EXPLAIN IT.** With the matrix bypassed the left border is magenta on the
wire and the window still does not reach it. Whatever places the window, it is
not a hunt for the end of black.

**The same deficit is measured a second way.** Correlating the pass-through
frame against a scaled one of the same source -- band anchored to each frame's
own picture, since the two sit at different vertical offsets -- fits
`pass = 1.0495 x scaled - 100` at r 0.994, which is 56 columns absent at the
left and none at the right. The method returns 1.0010 on the vertical axis
where both pictures measure 1028 rows, which is what says the horizontal 1.0495
is the picture and not the fit.

## No channel register moves the left edge

Every one of these was walked with `PAD_SYNC_OUT_ENZ` forced to re-lock, and
the left edge did not move off column 0 on any of them:

| register | walked | right bar | left |
|---|---|---|---|
| `HD_HB_SP` | 90, 140, 190, 238, 290, 360 | 251, 251, 250, **55**, 116, 199 | unmoved |
| `HD_HS_ST`/`SP` | 128/4, 164/40, 264/140, 364/240 | 57, **56**, 369, 486 | unmoved |
| `HD_INI_ST` | 0, 60, 120, 2000 | 56, 54, 56, 56 | unmoved |
| `HD_BLK_GY_DATA` | 5, 128 | 55, 56 | unmoved |

Three things fall out of that table.

**238 is a sharp local optimum and both directions are worse**, which is the
non-monotonic response this page already refused to fit a constant to. Below it
the response saturates at 251, the same figure 147 gave.

**`HD_INI_ST` is inert.** A near-full-line shift of the channel counter's own
origin moves nothing, so the register stores and reads back while the counter
ignores it -- the same shape as `HD_HSYNC_RST`'s bit 3.

**`HD_BLK_GY_DATA` is the encoder's black reference, not a bar.** Raising it to
128 tinted the whole picture magenta, mean luma 183.7 to 110.2, and reverting
restored it exactly. It cannot move the window because whatever it is set to
becomes the level the encoder restores from. `ComponentBlankLuma` is therefore
load-bearing for colour.

## The capture dongle is not doing it

The television was plugged directly into the board, with the USB capture out of
the path entirely, and the picture is the same: no border at the left. Two
sinks that share no hardware agree, so the dongle's own window latch is
excluded and what remains is the MS9288A or the channel.

## Open

What places the transmitted window. It is not the end of black, not any channel
register, and not the capture dongle. The MS9288A is on no I2C bus and has no
published register map, so the remaining board-side candidate is whether the
channel plays the left border out at all -- which the input retiming and the
divider reach and this sweep did not.
