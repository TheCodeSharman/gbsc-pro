# The judder follows the display, not the signal

A sub-pixel judder in MOVING content, on the scaling path: a flicker between
white and black at the edge of a sharp contrast change, present only where the
picture is moving. Static detail sits still beside it, to a position sd of
0.0001 .. 0.025 px, which is why every displacement measure reports it clean.

**IT IS THE SET.** Swapping the display on the capture dongle's loop-out, with
nothing else in the chain touched, decides it:

| sink on the loop-out | re-acquisitions | juddering |
|---|---|---|
| the bench television | 6 | **6** |
| a second display | 5 | **0** |

Eleven re-acquisitions, no exceptions in either column. One of the television's
six is a clean state flipped to juddering by a single toggle -- the transition
the second display never made.

The board emits a byte-identical signal throughout: `VDS_HSYNC_RST` 2139,
`VDS_VSYNC_RST` 999, `VDS_HSCALE` 475, `PLLAD_MD` 2200, `PA_SP` 29 and
`STATUS_SYNC_PROC_HTOTAL` 2200 read back on every roll. **The encoder's link
partner is the dongle, not the display**, so the HDMI handshake the MS9288A
performs is the same in both columns and the encoder is held constant along with
everything else.

## Why it took so long to reach a sink

Everything the board and the capture can report is identical across the artefact,
so nothing upstream of the set could separate the two states.

**`PAD_SYNC_OUT_ENZ` IS THE CONTROLLED RE-ROLL.** Held away two seconds and
restored, the link re-acquires and the verdict can change, with **every one of
the 608 configuration registers byte-identical either side**. It changes no
timing, scaling or raster register, so it isolates the re-acquisition from
everything else a mode change does, and costs about twenty seconds. An output
mode change re-rolls it for the same reason; a raster change too small to drop
the link does not.

| compared | config registers differing |
|---|---|
| juddering vs juddering, two acquisitions | **0 of 608** |
| clean vs juddering, separated only by a pad toggle | **0 of 608** |

The ten status addresses that differ -- `s0_00`, `s0_06`, `s0_07`, `s0_08`,
`s0_0a`, `s0_11`, `s0_12`, `s0_23`, `s0_2e`, `s0_2f` -- move between two states
of the SAME verdict as well, so they carry nothing.

**The frame time lock does not see it either.** Clean and juddering both
converge to `err` near 14200 against a target of 792481, the display clock
landing on 108012392 in both, and `pin in` equal to `pin out`.

**And the capture cannot see it.** Four independent measures over raw
uncompressed frames fail to separate the states:

| measure | what it reads |
|---|---|
| scroll step SIZE | spread scores a CLEAN 720p *worse* than a juddering 960p |
| scroll step CADENCE | intervals fall in the expected 2/3 capture beat in both |
| anomalous-interval RATE | clean 0.93 .. 1.09/s against juddering 0.83 .. 4.70/s, overlapping |
| edge change on bit-identical pairs | 0.000 grey levels in both |

A matched pair of clips of one scrolling scene, one clean and one juddering,
recorded minutes apart with only a pad toggle between, are indistinguishable on
playback.

**THE TELEVISION RUNS OFF THE DONGLE'S LOOP-OUT, WHICH IS WHY THAT IS NOT A
CONTRADICTION.** The loop-out passes the emitted signal through, so a set keeps
its original timing, while the USB capture is re-clocked to the dongle's own
~59 fps against a 50.47 Hz output. Frame CONTENT survives that; frame TIMING does
not. So the capture is structurally blind to an artefact carried in when frames
are presented, and every measurement above is taken through it.

## What was refuted on the way

Each was tested against a state the bench called juddering, and none moved it:

- **`VDS_HSCALE`.** A correlation across four output modes put 436 clean twice
  and 475 juddering twice; writing 475 -> 436 on a juddering state changes
  nothing. The correlation does not survive the A/B.
- **The read/write crossover phase.** `FrameSync`'s `targetPhase` moved 90 -> 270
  changes nothing.
- **Clock steering.** `/framesync?observe=1` parks the clock and steers nothing;
  a clean state stays clean. With the clock parked the phase drifts about -2
  ticks per 1.6 s sample out of a 3169926-tick frame -- a residual rate error
  near 20 uHz, so at this operating point the steering corrects nothing and its
  absence costs nothing.
- **A frame-rate beat against the output raster.** The lock converges with
  `in 50474 mHz` against `out 50475 mHz` and `pin in == pin out`, one slipped
  frame per thousand seconds, which cannot produce an artefact seen every few
  frames.
- **A torn frame.** Stepping frames change over their full height together, and
  the parked tear is a separate artefact the bench reports as absent.
- **A per-column or per-row displacement.** Uniform across twelve column strips
  and fourteen row bands, so the picture moves as a whole where it moves at all.

**The output mode BIASES it rather than determining it**, and a mode-by-mode
verdict table is not evidence on its own: `PA_SP` landing in its 10..16 band
puts the shimmer on the picture beside this, and the two are separate artefacts
that must not be merged.

## What the set's own settings show, and what they do not explain

**The bench television's "PC mode" leaves the artefact much less pronounced**,
which places some of it in the set's own processing rather than in what it
receives. That is as far as the evidence reaches.

**IT IS NOT MOTION INTERPOLATION SELECTED BY THE MODE'S STANDARD, AND THAT
READING IS REFUTED BY THE DATA IT WAS DRAWN FROM.** A set that interpolates a
broadcast input and leaves a PC one alone would judder on the CEA modes and be
clean on the DMT ones. The opposite is observed:

| mode | standard | verdict |
|---|---|---|
| 1080p, 720p, 576p | CEA-861 | clean |
| 1024p, 960p | VESA DMT | juddering |
| 480p | CEA-861 | juddering, and independently broken -- `VDS_DIS_VB_SP` 36 .. `VDS_DIS_VB_ST` 37, one line |

So the CEA/DMT split correlates with the verdicts while running against the
mechanism that would explain it, and **what distinguishes the two groups is not
established**. The same six points also split on output line rate -- 53.8 and
50.5 kHz juddering between 56.8, 37.9 and 31.6 kHz clean -- and six points
cannot separate that from the standard they carry.

**1024p and 960p cannot be moved to CEA** in any case: CEA-861 does not define
1280x1024 or 1280x960, so there is no firmware change available on that reading
even had it survived.

**What PC mode leaves behind may be the field rate, and that is a trade with no
free side.** The output follows the source -- 50.475 Hz with a game running,
50.08 Hz on the bench card, neither of them 50.000 -- so a panel with a fixed
refresh has to rate-convert, and an offset of a few hundredths of a hertz
repeats or drops a frame every second or two.

**A CRT HAS NO NATIVE RATE, WHICH IS WHY FOLLOWING THE SOURCE COSTS NOTHING
THERE AND SOMETHING HERE.** It deflects at whatever arrives, so an odd field
rate is free and `FrameSync`'s strategy is simply correct. A fixed-refresh panel
removes that freedom: SOMETHING must convert, and the only question is which end
does it less badly. Emitting exactly 50.000 Hz would let such a set show it one
for one, and would move the same hitch onto the board, the source not being at
50.000 either. So the choice is not between a hitch and no hitch.

That is the trade `FrameSync` already decides, and moving it needs a measurement
of which end carries it better rather than a preference. **What is not measured
is whether this set can lock to 50.475 Hz at all**, or is converting it to 50 or
to 60; a set that tracks the input within a range would make the question moot.

## What this does not say

It does not say the emitted signal is beyond criticism. A set that judders on a
signal another set takes cleanly may be reacting to something real about it --
the 50.475 Hz field rate, the non-CEA raster, or the timing jitter the capture
cannot measure. What is established is only that **the board emits the same
thing in both verdicts**, so no register, solve or firmware change is implicated
by this artefact, and chasing it upstream of the sink spends time on a signal
that is already identical across the symptom.

The instrument to reach for if it returns is the loop-out swap, not a register
dump: two sinks, the same signal, and `PAD_SYNC_OUT_ENZ` to roll each of them.
