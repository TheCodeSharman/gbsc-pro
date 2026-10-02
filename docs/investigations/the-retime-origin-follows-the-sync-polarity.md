# The retime origin follows the sync polarity

`SyncProcessor::retimeStopFor()` places the stop of the retiming window, and the
counter that every window is measured from takes its origin there. On a
**low-active** source the stop is the divider less the source's sync width plus
a fixed origin. On a **high-active** source the sync width does not come off it
at all: the stop sits `HighActiveStopSamples` before the end of the line,
whatever the pulse measures.

`normaliseHsyncPolarity()` writes `SP_HS_INV_REG` for a high-active source,
which inverts the hsync **into the retiming module** and leaves
`SP_HS2PLL_INV_REG` at 0, so the retiming and the sample clock reference
opposite edges of the same pulse. The distance between them is the pulse.

## What is measured

Walking `SP_RT_HS_SP` in pass-through until the card's one-pixel green border
frames at both ends of the line, divider 2038 and oversampling ratio two
throughout:

| source | polarity | pulse | stop that frames it | implied origin |
|---|---|---|---|---|
| 640x480@60 | low-active | 235 | 1870 | 67 |
| 640x480@72 | low-active | 112 | 1994 | 68 |
| 640x480@75 | low-active | 150 | 1955 | 67 |
| 800x600@56 | **high-active** | 139 | 1991 | 92 |
| 800x600@60 | **high-active** | 243 | 1981 | 186 |

The three low-active rasters span 123 samples of pulse and return one origin.
The two high-active ones differ by 104 samples of pulse, where subtracting the
pulse predicts a 104 sample spread in the stop; the measured spread is **10**,
and one stop — 1986, the divider less 52 — frames both.

**640x480@72 and 800x600@60 settle it between polarity and line rate.** Their
line rates differ by 0.05%, at 37.86 and 37.88 kHz, and their stops differ by
the whole correction.

## What this refutes

**That the origin is one measured constant.** It was calibrated on four states,
all of them low-active, which is why it held: 61.0, 62.6, 63.6 and 63.5. A
high-active source was never in the set.

**That matching the inversions fixes it.** Writing `SP_HS2PLL_INV_REG` to 1 so
the sample clock sees the same sense as the retiming leaves the framing exactly
where it was — 150 columns of bar at 800x600@60, unchanged across a sync-pad
re-lock, with the sync processor still counting 2038 by 627. The two references
are not made one by that bit.

**That it is a pass-through fault.** The scaling path takes the same stop. At
800x600@60 scaled, the engine's 1329 clips the card's right-hand colour blocks
entirely and the high-active 1384 recovers them. The scaling path has its own
residual framing question, which this does not answer.

## The stop is inert to every measurement the solve reads

**It does not destabilise the field rate, and nothing downstream of it moves
either.** Frozen, so no solve rewrites the register, `SP_RT_HS_SP` was set to
1858, 1986 and 2037 against a divider of 2038 and the source measured at each:

| stop | field rate, 120 readings | `HTOTAL` | `VTOTAL` | `HLOW_LEN` |
|---|---|---|---|---|
| 1858 | 60.32 Hz, 1 distinct value, sd 0.000 | 2038 | 627 | 1790 |
| 1986 | 60.32 Hz, 1 distinct value, sd 0.000 | 2038 | 627 | 1790 |
| 2037 | — | 2038 | 627 | 1790 |

The field rate is `Tv5725::SamplingLog`'s `rates` mode, which times the input
formatter's vertical on `DEBUG_IN_PIN` — the same measurement
`SourceMeasurement` takes. So a solve that follows a stop change is not
responding to anything the stop did to the source.

**The engine is quiet with the high-active stop in force**: 90 s of console with
the source settled at 800x600@60 prints nothing at all, and the emitted frame is
a full-screen picture at mean luma 82.44.

## What a mode change costs, on both stops alike

`640x480@60 -> 800x600@60` leaves the sync processor counting **1970..1973
against a divider of 2038** for about seven seconds, so `Adc::dividerLatched()`
is false, `takeDuty()` refuses every reading as `UNLOCKED`, and the engine
re-measures at 10 Hz until the recovery ladder reaches `restart sampling clock`
at pass 60 and clears it in one step. The field rate reads a steady 60.31 Hz and
627 lines throughout, so nothing about the source is in question.

**It is not the stop's.** The same four mode changes driven against the
low-active build and the high-active one are indistinguishable — 55 against 54
`UNLOCKED` readings, one `restart sampling clock` each, the same htotal
distribution and the same field rates. `docs/known-issues.md` carries it.

**A capture taken inside that window reads as a dead output and is not one.**
The dongle needs ten seconds or so after a mode change before it delivers, so a
frame grabbed too early is mean luma exactly 0.00 — which is no link rather than
a black picture, and reads exactly like the board having stopped.

## What the stop is worth, measured on the emitted frame

800x600@60 in pass-through, the card with `ANIM OFF`, captured off the USB HDMI
dongle at both stops:

| stop | left black margin | mean luma |
|---|---|---|
| 1858, the low-active formula | **150 columns** | 70.92 |
| 1986, `divider - HighActiveStopSamples` | **0** | 82.44 |

At 1858 the card's right-hand frames run off the end of the line; at 1986 its
one-pixel green border is present on all four edges. 640x480@60, which is
low-active, takes 1866 on both builds and is unchanged.

## Judging a hand-set stop

**A register set by hand in pass-through is judged only after a
`PAD_SYNC_OUT_ENZ` re-lock.** Without one the measurement reads the window
placement the sink latched at the previous lock, not the register's effect: a
creep over `HD_HS_ST` without re-locking produces a clean linear response with
slope −1 and a repeatable optimum, and every point of it is the stale placement
moving. Re-locked, the same creep only moves black from one edge of the frame to
the other.

## The instrument

`HD_BLK_GY_DATA`, `HD_BLK_BU_DATA` and `HD_BLK_RV_DATA` carry the colour the
pass-through channel substitutes while it blanks. Painted magenta, the board's
own blanking becomes visible and separable from the source's porch, which is
otherwise electrically identical to it. That is what shows the blanking standing
still in the window while the picture moves under `SP_RT_HS_SP`, and it is the
only reading on this bench that can tell the two kinds of black apart.

The clamp samples inside that blanked region, so the substituted colour becomes
its black reference: painted magenta, a white frame reads R−G −88 at the top of
the picture decaying to zero by row 180, where black reads −6.8 decaying by row
60. **Restore the blank data to black before judging colour.**
