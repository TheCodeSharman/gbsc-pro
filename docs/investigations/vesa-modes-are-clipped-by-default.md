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

## What this does not answer

- **The vertical axis.** `place()` centres both, and the card is cut top and
  bottom at 640x480@60 as well. Vertical blanking is asymmetric too. Not
  investigated.
- **The capture write origin.** `docs/vesa-gtf.md` warns that translating a
  percentage of the line into `IF_HB_ST2`/`IF_HB_SP2` needs an origin that has
  never been measured, and is assumed to be `VDS_HB_SP`. The bench now gives an
  empirical anchor for one mode, which is not the same as measuring it.
- Whether the remaining Acorn modes are VESA-timed. Only 640x480@60 was checked.

## One thing it helps

A wider capture needs **less** magnification to fill the same raster, and the
magnification is bounded by the output width limit in
[720p-edge-corruption.md](720p-edge-corruption.md). Fixing the placement moves
that constraint in the right direction rather than fighting it.
