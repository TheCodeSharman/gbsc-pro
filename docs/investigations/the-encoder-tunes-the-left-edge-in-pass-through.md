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
[the-picture-position-is-re-rolled-by-the-sync-pad.md](the-picture-position-is-re-rolled-by-the-sync-pad.md):
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

## Open

Whether the encoder keys on where active content starts. Turning the source's
border off moves the first non-black pixel by 40 source pixels, so the border
on/off pairing across two landings would answer it. **The instrument cannot take
the border-off half as it stands**: the border flash is the only signal strong
enough to lock the phase to, and with it off the corner blocks alone read at
about one grey level through this camera and the phase locks onto noise. A
longer clip, or a source-side animation with more contrast, is what that needs. What selects between landings is also unanswered — a forced re-lock is
repeatable, a natural acquisition lands elsewhere.

`tools/gbsc-pro-hwtest/snapshots/golden-800x600-bypass-2026-09-21.json` is a
landing with both corners complete and the frequency wedge at the camera's noise
floor. `docs/photos/2026-09-21-golden-800x600-bypass/README.md` carries the
photographs and the register values.
