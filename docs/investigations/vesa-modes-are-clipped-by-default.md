# VESA modes are clipped by the default capture window

640x480@60 loses about 8% of its picture off the right-hand edge on a fresh
source. The solve is not wrong — it is self-consistent to two pixels — and the
capture window is not measured from the picture, because nothing can measure it.
What is wrong is where the default window is *placed*.

## The mechanism

`ActiveImage::place()` centres the window in the line:

```c
long start = (long)(line.units() - width) / 2 + pan;
```

**Active video is never centred in the line.** `sync + back porch` runs far longer
than the front porch, so the active region's centre sits at 53–58% on every
source, and a centred window is offset left of the picture — wasting capture on
back porch and cropping the far edge. `docs/vesa-gtf.md` recorded this for 15 kHz
sources; it is worse for VESA.

## The numbers

VESA DMT, active video as a percentage of the line, for the modes this monitor
definition offers:

| mode | starts | ends | width |
|---|---|---|---|
| 640x480@60 | 18.0% | **98.0%** | 80.0% |
| 640x480@72 | 20.2% | 97.1% | 76.9% |
| 640x480@75 | 21.9% | **98.1%** | 76.2% |
| 800x600@56 | 19.5% | 97.7% | 78.1% |
| 800x600@60 | 20.5% | 96.2% | 75.8% |
| 1280x1024@60 | 21.3% | 97.2% | 75.8% |

| envelope | starts | ends | width |
|---|---|---|---|
| VESA DMT | 18.0% | 98.1% | — |
| 15 kHz Acorn / broadcast | 11.7% | 97.4% | — |
| **both** | **11.7%** | **98.1%** | **86.4%** |
| **current default** (0.76 x 1.04, centred) | 10.5% | **89.5%** | 79.0% |

The default ends at 89.5% where the picture runs to 96–98%, so **every VESA mode
in the table is cropped, by 6.7 to 8.6% of the line.**

## Confirmed on the bench

The capture-geometry card on 640x480@60 shows the nested frames complete on the
left with margin to spare, and running off the screen on the right — the exact
asymmetry the arithmetic predicts. Zooming out until the card is contained takes
the window from `[11.6%, 90.7%]` to `[11.6%, 99.8%]`, so the picture's right edge
is near 98% as the standard says.

The source is VESA-timed: `STATUS_SYNC_PROC_HLOW_LEN / STATUS_SYNC_PROC_HTOTAL`
measures the sync at **11.57%** of the line against DMT's 12.00%.

## Two ways to fix it, and they are not equivalent

**The requirement has two halves — a standard mode should be full screen *and*
unclipped — and only one design gives both.**

**Widen to the envelope.** Start at 11.7%, width 86.4%. Nothing is ever cropped,
on any source, and it needs no table. But it captures 6–8% of blanking on every
VESA mode, so a standard mode comes up with black borders: unclipped, not full
screen. It also gives up picture on the 15 kHz modes that are correct today.

**Use the DMT entry when the source matches one.** Key on measured line rate and
field rate; when they match a DMT mode, take that mode's exact active window;
otherwise fall back to the envelope. Standard modes then come up full screen and
unclipped, which is the requirement, and non-standard sources keep the safe
behaviour. `docs/vesa-gtf.md` already concludes that DMT is where the standard is
accurate, as against GTF's curve, which that page rejects.

**Recommended: the DMT table**, because the envelope cannot deliver "full screen"
by construction.

## What was built

**Both halves landed.** The envelope is the fallback and the table is consulted
first, so a standard mode comes up full screen and unclipped and everything else
comes up whole with black at the edges.

`Axis` carries the envelope as a start and an extent per axis — 11.7% to 98.1%
of the line, 6.1% to 99.4% of the frame — so a source matching no published
raster is placed rather than centred, and `ActiveImage` asks the same two
questions of either source. The vertical fraction no longer splits on field
rate: an envelope containing a 50 Hz and a 60 Hz source alike makes the split a
choice about which of the two to crop.

Against stock AKF50's 15 kHz modes, whose picture sits between 15.4% and 90.4%
of the line and 7.1% and 99.4% of the frame, the centred default cropped 0.9% of
the line off 768x288 and 0.9 to 7.0% of the frame off every one of them. The
envelope crops none of them. It costs picture size on a source whose blanking is
narrower than the envelope: the bench card fills 72% of the screen width against
79%, and 88% of its height against 96%, the rest being black the user trims and
the framing table then remembers.

`Tv5725::SourceTiming` is that table, thirteen DMT and CEA-861 rasters keyed on
the frame, the field-rate bucket and the hsync duty. `ActiveImage` places an
untuned axis from the standard's own active window when one matches, and from
the assumption when none does. Both axes, because the raster states both.

Measured on the bench at 640x480@60: capture moved from `131..1020` to
`203..1103` of a 1125-unit line, 18.04% and 98.04% against the standard's 18.00%
and 98.00%, and the vertical window landed on `35..515`, the raster's 480 active
lines exactly. A 311-line source matches nothing and is placed as it was.

**The duty is what separates two standards on one key.** A 525-line 60 Hz frame
is both 640x480 DMT, 96 pixels of sync in 800, and 720x480p, 62 in 858, and they
put active video 2.2% of the line apart. A source matching no row keeps the
assumption rather than the nearest row, because placing a source from a raster
it is not emitting crops picture.

**`STATUS_SYNC_PROC_VTOTAL` counts from zero**, so the match is against one more
line than it reports: the bench reads 524 on the 525-line mode, and 311 on
AKF50's 312-line 320x256.

## The remaining Acorn modes are VESA-rastered but not VESA-blanked

Stock AKF50 states each mode as `sync, back porch, left border, display, right
border, front porch`, and its VESA-rate modes keep the standard's total and a
near-standard sync while spending the back porch on **border** instead. The file
says so itself: the 72 Hz and 75 Hz entries carry the VESA timings they replaced
as a comment.

| mode | AKF50 h_timings | picture starts | DMT window starts | picture cropped |
|---|---|---|---|---|
| 640x480@60 | 94,22,22,640,22,0 | 138/800 | 144/800 | 6 px left |
| 640x480@72 | 48,84,30,640,30,0 | 162/832 | 168/832 | 6 px left |
| 640x480@75 | 64,76,30,640,30,0 | 170/840 | 184/840 | 14 px left |
| 800x600@56 | 72,84,34,800,34,0 | 190/1024 | 200/1024 | 10 px left |
| 800x600@60 | 128,48,40,800,40,0 | 216/1056 | 216/1056 | none |

Nothing is cropped on the right on any of them, which is the 6.7 to 8.6% the
centred default lost. What remains is up to 14 px off the left — 2.2% of picture
width — on a source that borders differently from the standard it otherwise
follows.

**That is left to the user rather than padded away.** A pad wide enough to
contain these is derived from one machine's mode file, which `docs/vesa-gtf.md`
rejects as a basis for defaults, and it puts a black edge on a true DMT source.
One pan press covers it and `docs/framing-presets.md` remembers it against that
source.

## What this does not answer

- **The capture write origin.** `docs/vesa-gtf.md` warns that translating a
  percentage of the line into `IF_HB_ST2`/`IF_HB_SP2` needs an origin that has
  never been measured, and is assumed to be `VDS_HB_SP`. The bench now gives an
  empirical anchor for one mode, which is not the same as measuring it.
- **Interlaced sources.** The table is progressive only: a field arrives rather
  than a frame, and what its line count reads as has not been measured.

## One thing it helps

A wider capture needs **less** magnification to fill the same raster, and the
magnification is bounded by the output width limit in
[720p-edge-corruption.md](720p-edge-corruption.md). Fixing the placement moves
that constraint in the right direction rather than fighting it.
