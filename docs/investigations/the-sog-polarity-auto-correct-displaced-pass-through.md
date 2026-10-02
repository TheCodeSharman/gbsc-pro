# The SOG polarity auto-correct displaced pass-through

`SP_SOG_P_ATO` is the chip's own "sog auto correct polarity". The scaling path
clears it and `applyForPassThrough()` used to set it, inherited verbatim from
the sketch with no stated reason. **It is a second owner of a fact
`normaliseHsyncPolarity()` already settles, and it displaces where the sync
processor starts counting by a whole pulse.** One bit, two faults.

## It made pass-through need its own retime stop

An H-positive RGBHV source in pass-through came out with a black bar down the
left — 150 of 1920 columns at 800x600@60, the card's right-hand frames run off
the end of the line. Walking `SP_RT_HS_SP` frames it, and the value that does
does not follow the pulse: at divider 2038, 800x600@56 with a 139 sample pulse
frames at 1991 and 800x600@60 with a 243 sample one at 1981, where subtracting
the pulse predicts a 104 sample spread. One stop, the divider less 52, frames
both.

**That pulse-independence was the bit being compensated for.** Frozen, with a
`PAD_SYNC_OUT_ENZ` re-lock at each step, 800x600@60 in pass-through:

| `SP_SOG_P_ATO` | `SP_RT_HS_SP` | left black margin |
|---|---|---|
| 1 | 1986, the pulse-independent stop | 0 |
| 1 | 1858, `divider - pulse + origin` | **150** |
| **0** | **1858, `divider - pulse + origin`** | **0** |
| 0 | 1986 | 0, and 8 columns short at the right |

So with the bit cleared the ordinary arithmetic frames pass-through, and
`retimeStopFor()` needs no route. A branch keyed on the route was written, shown
to break the scaling path, and reverted; what it was fitting is here.

## It survived into the scaling path, 310 columns of it

Nothing on the way back out of pass-through puts it back, so a
scaling -> pass-through -> scaling round trip returned the scaled picture
displaced 310 of 1920 columns with a black band down the left. A full
`snapdiff.py --save` either side, all 1536 registers, resolves to 32 fields:
sixteen `HD_*` left loaded with the raster bypass was driving, a divider family
that moved two samples, and three polarity bits.

Single-field, in the displaced state, each written alone:

| written | left margin | bottom margin |
|---|---|---|
| — | 310 | 7 |
| `SP_HS_POL_ATO` 1 | 310 | 7 |
| `SP_VS_POL_ATO` 1 | 310 | **0** |
| `SP_SOG_P_ATO` 0 | **0** | 0 |

`SP_RT_HS_SP` reads 1331 throughout, so the retime stop carries none of it.
`SP_VS_POL_ATO` carries the vertical and `SP_SOG_P_ATO` the whole horizontal.

**`/sc?~` clears it**, which `leaving-bypass-leaves-the-sync-path-behind.md`'s
recovery table had not tried — it lists a source mode round trip, `/sc?U` and
`/restart`. That page's own fault, `SP_HS2PLL_INV_REG` left set, is separately
fixed and reads 0 across this one.

## What the picture is judged on

**The green border flush on all four sides, not a black margin count.** `CARD`
carries a one-pixel green border hard against all four edges, so "is anything
clipped" is a yes/no on one frame. Scoring a change on a shrinking black margin
instead is how the reverted route branch came to be recorded as recovering 97
columns on the scaling path when it had pushed the card off the left and the
bottom.

Five states accepted on that test with the bit cleared: 800x600@60 and
640x480@60 on each route, and scaled after a round trip.

## Judging a hand-set register here

**A register set by hand in pass-through is judged only after a
`PAD_SYNC_OUT_ENZ` re-lock.** Without one the frame carries the window placement
the sink latched at the previous lock rather than the register's effect: a creep
over `HD_HS_ST` without re-locking produces a clean linear response with slope
−1 and a repeatable optimum, every point of which is the stale placement moving.

**The HD channel's horizontal is not a lever for this.** Frozen and re-locked,
`HD_HB_SP` 247 -> 600 — 353 samples of blanking — moves the left margin from 147
to 150, and `HD_HS_ST` 40 -> 160 -> 300 moves it 98 columns then 5, which is the
sink placing its window at a re-lock.
`the-encoder-tunes-the-left-edge-in-pass-through.md` already refutes both.

## The retime stop is inert to every measurement the solve reads

Frozen, so no solve rewrites it, `SP_RT_HS_SP` set to 1858, 1986 and 2037
against a divider of 2038 returns one field rate — 60.32 Hz, one distinct value
in 120 readings at sd 0.000 — and leaves `STATUS_SYNC_PROC_HTOTAL` 2038,
`VTOTAL` 627 and `HLOW_LEN` 1790 untouched at all three. The field rate is
`Tv5725::SamplingLog`'s `rates` mode, which runs from `loop()` and so answers
frozen. An earlier reading that the stop destabilises the field rate is refuted.
