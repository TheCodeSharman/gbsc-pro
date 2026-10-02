# The pass-through left bar is not the retime origin

An H-positive RGBHV source in pass-through is emitted with a black bar down the
left — 150 of 1920 columns at 800x600@60, with the card's right-hand frames run
off the end of the line. Walking `SP_RT_HS_SP` frames it, which is what made the
retime origin look like the cause. **It is not, and moving it there breaks the
scaling path.**

## What is measured

Walking `SP_RT_HS_SP` in pass-through until the card's one-pixel green border
frames at both ends of the line, divider 2038 and oversampling ratio two:

| source | polarity | pulse | stop that frames it | implied origin |
|---|---|---|---|---|
| 640x480@60 | low-active | 235 | 1870 | 67 |
| 640x480@72 | low-active | 112 | 1994 | 68 |
| 640x480@75 | low-active | 150 | 1955 | 67 |
| 800x600@56 | **high-active** | 139 | 1991 | 92 |
| 800x600@60 | **high-active** | 243 | 1981 | 186 |

The three low-active rasters span 123 samples of pulse and return one origin.
The two high-active ones differ by 104 samples of pulse, where
`divider - pulse + origin` predicts a 104 sample spread in the stop that frames
them; the measured spread is **10**, and one stop — 1986, the divider less 52 —
frames both.

**That the stop which frames pass-through does not follow the pulse is a real
observation. What it is evidence OF is the part that was wrong.**

## What it is not

**It is not the sync polarity.** The model was that
`normaliseHsyncPolarity()` inverts the hsync into the retiming module while the
sample clock keeps the original, so the two reference opposite edges and the
pulse between them must not come off the origin. **Both routes configure those
bits identically on a high-active source**, so there is no asymmetry to carry
the correction: `normaliseHsyncPolarity()` is the only writer of
`SP_HS_INV_REG` and it runs from `SourceMeasurement::readSource()` on both
routes, and `HdBypass::applyChannelSyncEdges()` writes `SP_HS2PLL_INV_REG`
`positive ? 0 : 1`, which is 0 for a high-active source — the same 0
`normaliseHsyncPolarity()` and `applyForPassThrough()` write. Measured on the
scaling path with this source: `SP_HS_INV_REG` 1, `SP_HS2PLL_INV_REG` 0,
`STATUS_SYNC_PROC_HSPOL` 1.

**It is not the shared origin's to fix, and applying it there is a regression.**
`SyncProcessor::retimeStopFor()` is called from `HdBypass` and from
`VideoPath::applySamplingClock()`, so a change to it reaches the scaling path,
where the retime stop is the capture counter's origin. At 800x600@60 scaled into
1080p, frozen with the source's `MODE` re-issued so the engine re-acquires
against each value:

| stop | the card |
|---|---|
| 1330, `divider - pulse + origin` | green border flush on **all four sides**, card complete |
| 1386, `divider - 52` | green border on the right only, card off the left and the bottom |

So the origin the low-active arithmetic gives is the correct one on the scaling
path, for the same source, the same polarity and the same duty that pass-through
wants a different stop for.

**And the scaling path is not insensitive to it.** 56 samples of origin moved
the picture about 97 output columns, so the window is genuinely placed from that
origin rather than from something that cancels it.

## What it points at instead

Pass-through places the picture from the HD channel's own horizontal —
`HD_HS_ST`/`HD_HS_SP`, `HD_HB_ST`/`HD_HB_SP`, `HD_HSYNC_RST` — and those do not
follow the source's measured sync width. A correction applied through the shared
origin to compensate for a fixed channel placement is **pulse-independent by
construction**, which is exactly the shape the table above shows: one stop
framing two rasters whose pulses differ by 104 samples.

`HD_HS_ST` 40 -> 160 with `HD_HB_ST` 2038 -> 120 and `HD_HB_SP` 247 -> 612 also
frames it, survives a re-lock, and touches no retiming register.

## The acceptance test is the border, not the margin

**A shrinking black margin is not evidence of better framing**, and scoring on
one is how the regression above was recorded as an improvement. `CARD` carries a
one-pixel green border flush to all four edges, so "is anything clipped" is a
yes/no on one frame: green on all four sides means nothing was lost. The same
change that took the scaling path's left margin from 313 columns to 216 — read
as 97 columns recovered — had pushed the card off the left and the bottom
entirely.

## Judging a hand-set stop

**A register set by hand in pass-through is judged only after a
`PAD_SYNC_OUT_ENZ` re-lock.** Without one the measurement reads the window
placement the sink latched at the previous lock, not the register's effect: a
creep over `HD_HS_ST` without re-locking produces a clean linear response with
slope −1 and a repeatable optimum, and every point of it is the stale placement
moving. Re-locked, the same creep only moves black from one edge of the frame to
the other.

**A source mode round trip is what re-acquires the capture**, so a one-register
A/B on the scaling path re-issues the source's `MODE` at each value with
automation frozen, and the engine's own re-acquisition judges it.

## The stop is inert to every measurement the solve reads

Frozen, so no solve rewrites the register, `SP_RT_HS_SP` was set to 1858, 1986
and 2037 against a divider of 2038 and the source measured at each:

| stop | field rate, 120 readings | `HTOTAL` | `VTOTAL` | `HLOW_LEN` |
|---|---|---|---|---|
| 1858 | 60.32 Hz, 1 distinct value, sd 0.000 | 2038 | 627 | 1790 |
| 1986 | 60.32 Hz, 1 distinct value, sd 0.000 | 2038 | 627 | 1790 |
| 2037 | — | 2038 | 627 | 1790 |

The field rate is `Tv5725::SamplingLog`'s `rates` mode, which times the input
formatter's vertical on `DEBUG_IN_PIN` — the same measurement
`SourceMeasurement` takes, and it runs from `loop()` rather than the acquisition
tick, so it answers frozen. **So a solve that follows a stop change is not
responding to anything the stop did to the source**, and an earlier reading that
the stop destabilises the field rate is refuted.

## What a mode change costs, whatever the stop

`640x480@60 -> 800x600@60` leaves the sync processor counting **1970..1973
against a divider of 2038** for about seven seconds, so `Adc::dividerLatched()`
is false, `takeDuty()` refuses every reading as `UNLOCKED`, and the engine
re-measures at 10 Hz until the recovery ladder reaches `restart sampling clock`
at pass 60 and clears it in one step. The field rate reads a steady 60.31 Hz and
627 lines throughout. The same four mode changes against either stop give 55
`UNLOCKED` readings and 54, one restart each, the same htotal distribution.
`docs/known-issues.md` carries it.

**A capture taken inside that window reads as a dead output and is not one.**
The dongle needs ten seconds or so after a mode change before it delivers, so a
frame grabbed too early is mean luma exactly 0.00 — no link rather than a black
picture, which reads exactly like the board having stopped.
