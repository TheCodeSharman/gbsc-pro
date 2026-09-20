# A hand-set divider cannot be judged, and a frame difference is not displacement

Two method failures found in one session, each of which produced a confident
wrong answer with repeatable numbers behind it.

## A divider set by hand is judged against the last solve's window

`PLLAD_MD` is one input to a solve that also sets the capture window, both
scales, the playback fetch and the memory stride. Freezing automation and
walking the divider leaves every one of those at the value solved for the
divider the engine chose, so what is being measured is the *mismatch*, not the
divider.

Swept at 800x600 with the window frozen, the picture scored a flat ~0.26 on a
frame-difference metric from 1600 to 2004 and then jumped to 2.0+ at 2005 —
a clean-looking cliff, reproduced at a second line rate 20 MHz away in ADC
clock, with `PLLAD_KS`, `PLLAD_CKOS`, `PLLAD_ICP` and `PLLAD_FS` identical
either side. It read as a hardware count limit at ~2005, below the eleven-bit
counter's 2047.

**It was the capture window.** `IF_HB_ST2` was frozen at **2005** — the stop
`VideoSourceLine::lastCapture()` clamps to `units - 2` for the divider the
engine had solved — and the cliff sat exactly where the divider reached it.
Every point that scored clean had the window's stop **past the end of a shorter
line**, which is not a state the engine ever produces. The second line rate
carried the same frozen window, so it confirmed nothing independently.

Re-run with the window tracking the divider (`IF_HB_ST2 = divider - 1`), every
point scores 2.0–2.6 including 1600 — because that sweep is confounded too:
moving the window without re-solving `IF_HB_SP2`, the scale and the fetch is a
different inconsistent state.

**So there is no measured count limit below 2047**, and the ~2005 figure must
not be reinstated. `CLAUDE.md` carries the same trap for `PB_FETCH_NUM`; the
general form is that **no subset of an interdependent solve can be set by hand
and judged.** Compare engine-solved states, or change the rule and reflash.

## A frame-difference metric measures contrast displaced, not displacement

Scoring a clip with `tblend=all_mode=difference` and taking the mean luma of the
difference answers "how much did the picture change", which is **displacement
multiplied by local contrast**. Over a solid colour block a whole line can shift
and the metric reads zero; over a fine grating a fraction of a pixel reads
large.

Per-row, that put essentially all of one clip's score in the rows carrying
PM5544's frequency grating and near zero everywhere else — which reads as
"the artefact is aliasing of near-Nyquist detail" when the artefact is uniform
horizontal jitter that only the grating is able to show.

**Measure the position of an edge instead.** Averaging a flat strip down to one
row and taking the centroid of the strongest luma gradient gives a sub-pixel
column per frame; its spread is the jitter in pixels, and it is independent of
how much contrast the picture happens to carry there.

Measured that way on engine-solved states, against a camera floor established by
shooting one state twice:

| state | divider | jitter, px sd |
|---|---|---|
| 320x256 doubled, 4x | 2506 | 0.04, 0.05 on the repeat |
| 1920x1080, 1x | 2006 | 0.09 |
| 1280x1024, 1x | 2006 | 0.09 |
| 800x600, 2x | 2006 | 0.32 |
| 1024x768, 1x | 2006 | 0.64 |
| 1600x600, 2x | 2006 | 1.83 |

**The ranking across modes is not yet trustworthy**, because each mode has its
own framing and the strongest edge in the strip is a different feature in each
clip — the column it locks onto runs 7, 218 and 883 across the set. What the
numbers do establish is that several undoubled modes sit six to thirteen times
the floor, so the jitter is real and is displacement rather than aliasing.

**One artefact is the leading explanation, not two.** Jitter is invisible over a
solid colour block and obvious on a grating, so "worse in the high-frequency
band" is what one uniform jitter looks like through a contrast-weighted eye.

## What was ruled out on the way

- **Coast.** `SP_PRE_COAST` and `SP_POST_COAST` both read 0 on the bench's
  separate-sync source, so neither can be positioning a band.
- **The ADC PLL.** `STATUS_MISC_PLLAD_LOCK` stays 1 and
  `STATUS_SYNC_PROC_HTOTAL` tracks the divider across every state above, and the
  four PLL group members read identically either side of the supposed cliff.
- **A still photograph.** The instability is not visible in a single frame at
  all, and the first pass through these modes recorded 1080p as good from one.
  Record a clip.
