# The far end of the output line

The requirement and the constants are current state, in
[`../scaler-geometry-model.md`](../scaler-geometry-model.md). This page carries
the models that were refuted on the way there, because three of them fitted the
readings, and the questions still open.

## What the symptom looks like

A picture sized to the raster's edge leaves almost no front porch, and three
artefacts arrive together:

- a black bar down the right that no zoom closes
- wrong colours — grey pushed toward red
- a wrapping pixel down the **left** edge

They are one fault. Giving the far end a front porch clears all three at once, so
nothing else needs to explain any of them.

## Refuted: a fetch budget measured in captured samples

The most convincing of the three, because it fitted two independent readings to
whole numbers.

`Memory::fetchFor()` sets `PB_FETCH_NUM = ceil(captureWidth / RequestsPerLine)`
and the playback stage makes `RequestsPerLine` requests per line, so the budget is
`4 x ceil(capture/4)` samples for a line that consumes `capture`. The ceiling is
the only slack, and it depends on divisibility — zero when `capture` is a multiple
of 4, three when it is one more. At capture 1000 there is none.

Converting the two thresholds into samples at the magnification of the day gave
2.9 and 6.3 against a reserve of 1.2 the firmware allowed, which reads as 3 and 6
samples exactly. The comment in `fetchFor()` also describes the artefact in as
many words: a line one pixel short of its source fails to finish and repeats, the
start of the picture reappearing at the right.

**It is not needed.** The front porch reserve clears the wrapping pixel while the
demand per line is still within 1.8 samples of the budget, so the budget was never
the binding constraint. The mod-4 slack observation is untested and may be a
latent hazard at some other capture width; it is not what was seen here.

The general warning applies — a good fit is not evidence the quantities are what
you think, and two thresholds landing on integers is not two points of
confirmation when the unit conversion is a free parameter.

## Refuted: a content-dependent black level

The hue drift is **gradual and monotonic** as the display window moves — greener
one way, redder the other, through a balance point — which rules out anything
discrete: a fetch shortfall and a 4:2:2 phase error both either happen on a line
or do not.

What fits gradual is the DC level on the analog link between the TV5725's DAC and
the MS9288A. The window sets the active/blanking duty ratio, an AC-coupled input
floats with average level, and each channel floats by a different amount because
each carries a different average. The direction even falls out of it rather than
being fitted: on a mauve picture R and B carry more average than G, so more active
video reads redder and less reads greener.

The prediction was that the balance point moves with picture content, and it
appeared to — 1892 on a game screen against 1900 on the desktop. **Both legs are
confounded.** The reboot between them changed the raster from 1901 to 1916 px and
the capture from 1000 to 1008, so the two values were never comparable; and the
drift itself stopped after that reboot and has not returned. Re-tested
afterwards, one window value is correct on both pictures.

## Refuted: 4:2:2 chroma phase

The frame buffer holds YUV 4:2:2, so a chroma sample is shared by a pixel pair and
an odd-numbered shift in where playback starts fetching swaps U and V.
`VDS_UV_FLIP`, `IF_UV_FLIP` and the `U_DELAY`/`V_DELAY` bits exist for exactly
that. It predicts a hue state **periodic with period 2** in the window register.

Observed behaviour is a single balance point with different wrongness either side,
not an alternation. The mechanism is real and the registers are there; it is not
what this was.

## Open: a black level that latches badly and skews later readings

The gradual drift above stopped after a reboot of the source, which also changed
the mode. Two candidates, and the reboot confounded them:

- the source was misbehaving and a power cycle fixed it
- the mode change re-ran the chip's clamp, clearing a badly latched black level

The second is better supported by the mechanism: `SP_HT_DIFF_REG`,
`SP_VT_DIFF_REG` and `SP_STBLE_CNT_REG` gate auto-clamp on timing stability, so a
clamp can latch while timing is unsettled and stay latched. If it holds, any
colour measurement taken while it is latched is worthless, which is the shape of
what happened here — no unit conversion reconciled the two balance points because
one of them was not measuring the front porch at all.

**The test is to reproduce the bad state and then force a mode change without
touching the source.** Nothing else needs to vary.

Related and separate: `applyRGBPatches()` writes `VDS_UCOS_GAIN 0x1c` and
`VDS_VCOS_GAIN 0x29` against a unity of `0x20` on both the RGB and the component
path — a chroma rotation nobody has justified. The colours are correct *with* it
in place, so it is not obviously broken. `SP_CS_CLP_ST`/`SP_CS_CLP_SP` landing
inside the hsync pulse rather than the back porch is a plausible reason someone
would have needed to compensate for something.

## Open: whether the encoder's sampling window is the same on every display

The bench TV fills its screen edge to edge from every other HDMI source, so it is
not overscanning, which places the sampling window at roughly 104..1817 of a
1901 px line — a board property rather than that panel's crop. The encoder
consumes the scaler's analog blanking and generates HDMI blanking of its own, so
the minimum the scaler must emit cannot depend on the display.

**A standards-conformant front porch does cost visible picture**, and the bypass
photograph is what says so: bypass reaches 26 photo columns further right than the
scaled path at 1080p, about 2.3% of the line, on one camera position and one crop
so the two are comparable. Blanking inside the encoder's sampling window shows as
black, so the reserve is not free and is now the measured floor rather than CEA's.

The sampling window itself has still never been measured directly. **Creeping the
display window down and watching for the black bar measures it**: the value where
the bar first appears is where the encoder stops sampling.

**The other test is other sets.**

## Not a reinstatement of the headroom rule

The retracted headroom rule was a margin between the memory window and the
`produced` picture, and it stays retracted. This is a different quantity: the
distance from the end of the picture to the end of the **raster**. The memory
window is still the display window, allocating nothing spare, and both now stop at
the front porch.
