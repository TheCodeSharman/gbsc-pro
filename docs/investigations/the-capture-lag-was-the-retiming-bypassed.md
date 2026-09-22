# The capture lag was the retiming bypassed

`VideoSourceLine::CaptureLagFraction` held 0.0539 — a displacement of the video
along the input formatter's line counter, applied as a fraction of the line. It
is deleted. It was never a pipeline latency; it was a curve fitted to a
misconfiguration, and the misconfiguration is one bit.

## The bit

`SyncProcessor::applyForSyncType()` wrote `SP_HS_LOOP_SEL` 1 in **both** sync
branches. That is `s5_57[6]`, *"Bypass PLL HS to 57 core"* — at 1 the sync
processor's retiming module is out of circuit, and the input formatter's line
counter is reset by something other than the retimed hsync.

With it bypassed the counter runs roughly 78 units ahead of the video, so the
last ~78 units of every source line fall past the counter's reset and cannot be
reached at any framing.

## The measurement

800x600@60 into 1080p, 100% framing, engine frozen, the routing bit the only
variable.

| | the card's bottom corner squares, in photo columns |
|---|---|
| `SP_HS_LOOP_SEL` **1** | **one square**, 195..220. The right-hand one is cut off |
| `SP_HS_LOOP_SEL` **0** | **two squares**, 96..121 and 1469..1491 |

The two squares' outer edges are source pixels 216 and 1016, which is 1090
counter units across 1395 photo columns — **0.7814 units per column**. The left
square moved 195 → 96:

```
99 columns x 0.7814                    = 77.4 units
CaptureLagFraction x 1439 (0.0539)     = 77.6 units      -- 0.3% apart
```

The offset the constant was applying is the offset the bypass introduced, and
with the retiming engaged the residual offset is **zero**. Cross-check: at zero
lag the window should show 39.3 units of blanking to the left and 52.6 to the
right, a picture 1512.6 columns wide; card plus both margins measured 1512.7.

Against DMT 800x600@60 before the fix: back porch 86 of 88 px captured, active
**781 of 800**, front porch **0 of 40**. Vertical was already complete, 600 of
600 — which is why the constant was horizontal only.

## What else the bypass was hiding

The whole `SP_RT_*` group is inert while `SP_HS_LOOP_SEL` is 1, which is why
those registers have never appeared to do anything. Frozen, one field at a time,
diffs as mean absolute difference of a column profile over a 300-row band
against a camera noise floor of ~0.3:

| field | change | picture |
|---|---|---|
| `SP_RT_HS_SP` | 1337 → 1420 | 0.29 — nothing |
| `SP_RT_HS_SP` | 1337 → 700 | 0.33 — nothing |
| `SP_RT_HS_SP` 700 | + `SFTRST_SYNC_RSTZ` pulsed | 0.26 — nothing |
| `SP_SYNC_BYPS` | 0 → 1 | 0.57 — nothing |
| **`SP_HS_LOOP_SEL`** | **1 → 0** | **29.2, max 127** |
| `SP_RT_HS_SP`, loop engaged | 1337 → 1100 | **56.4, max 179** |

So `SP_RT_HS_SP` is a live knob once the loop is engaged, which makes
`SyncProcessor::RetimeStopPercent` = 93 a value that now reaches the picture and
wants a derivation rather than an inherited number.

Also measured inert on this path, and not the cause: `IF_LINE_ST`/`IF_LINE_SP`
(the deinterlacer is off and `IF_LD_RAM_BYPS` is 1) and `SP_HS_EP_DLY_SEL`
(reads 0). `CaptureWindow::ProgressiveStart` therefore writes into a register
that does nothing on the progressive path.

The deinterlacer was confirmed unengaged throughout: `DIAG_BOB_PLDY_RAM_BYPS` 1,
`MADPT_PD_RAM_BYPS` 1, `RFF_ENABLE`/`WFF_ENABLE` 0, `MAPDT_VT_SEL_PRGV` 1,
`MADPT_Y_MI_OFFSET` 127, `MADPT_Y_MI_DET_BYPS` 1.

## The two changes must land together

Deleting the lag without engaging the retiming misplaces every window by 78
units. Engaging the retiming without deleting the lag double-counts the
correction.

## What this retracts

`docs/investigations/a-standard-mode-loses-both-edges-while-every-stage-measures-correct.md`
is where 0.0539 was derived, and its reasoning does not survive. Its two
readings were taken on **different sources** — 800x600@60 at `PLLAD_MD` 1124 and
480p at 1880 — so divider and source were confounded, and it was never two
dividers against one source as the derivation assumed. The shipped constant
matched neither reading; both came out near 0.063.

It also moves the far-edge bound. A stop at `units - 1` had been measured as
freezing the picture, which is what put `VideoSourceLine::lastCapture()` at
`units - 2`; that reading was taken with the retiming bypassed, and with it
engaged `units - 1` captures cleanly on both axes.
`docs/investigations/the-capture-tail-was-one-unit-short.md`.

## The vertical constant went too

`FrameLagUnits` held -7 counter units, derived beside the horizontal one and
justified by four readings taken by creeping the capture window a unit at a time
until the card's outermost row entered the picture:

| source | counter units | first picture line | video arrived at |
|---|---|---|---|
| 320x256@50 at 480p | 312 | 36 | 29.4 |
| 320x256@50 at 960p | 624 | 72 | 64.3 |
| 800x600@60 | 628 | 27 | 20.3 |
| 1024x768@60 | 806 | 35 | 28.3 |

Read as a count of the counter's own units every reading is seven, which is what
made it look like a property of the capture path.

**It is deleted, and the readings are better explained by where the default
framing opened.** Both of the places a vertical window is placed from counted
the vsync pulse as leading blanking -- the convention the DMT tables and the
monitor definition state, where `v_timings` lists sync first -- while the
counter zeroes at the pulse's TRAILING edge. So the window opened a sync width
into the picture, the lag cancelled some of that, and a creep that finds the
picture late measures the pair rather than a pipeline delay.

`SourceTiming`'s `vstart` column is the back porch alone now, so a source
running a published raster is placed on its active area exactly: measured at
800x600@60, capture `23..623` of 628 against the standard's 23 and 600, and at
640x480@60 `33..513` of 525 against 33 and 480.

**The envelope is the half that remains.** `Axis`'s vertical `activeStart` is
0.061, which is the same leading-edge convention, and it is what places a source
matching no published raster.

## The retiming does not cause a vertical wobble

Engaging it was blamed for a 1-2 px whole-picture vertical shift at ~25 Hz, and
the measurement that exonerates it -- the loop toggled with the field rate
re-sampled 40 times each way, and the build re-acquired eight times -- is on
`docs/investigations/single-sample-rate-jitter.md`, with the single-trial
reasoning that produced the claim.
