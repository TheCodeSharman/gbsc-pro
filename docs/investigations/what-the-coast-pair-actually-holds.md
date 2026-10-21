# What the coast pair actually holds, and what covers the vertical interval

`SP_PRE_COAST` and `SP_POST_COAST` read as "whether to coast". They are not.
RD-5725-1.1 calls s5_38 *"set the coast will valid before vertical sync (line
number)"* and s5_39 *"when line cnt reach this value coast goes down"*, so the
pair is a span either side of vertical sync -- and at 0/0 it spans nothing.

## The coast generator contributes nothing at 0/0

Measured on the RISC PC at 320x256@50 composite, automation frozen, one bit at a
time, eight counts per state and every write read back:

| state | `STATUS_SYNC_PROC_VTOTAL` | `STATUS_SYNC_PROC_HTOTAL` |
|---|---|---|
| control, 0/0 | 308 x8 | 2200 |
| `SP_NO_COAST_REG` 1 -- generator off | **308 x8, unchanged** | 2200 |
| `SP_DIS_SUB_COAST` 1 -- sub coast off | **303 x8** | 2200 |
| both restored | 308 x8 | 2200 |

Turning the coast generator off at 0/0 moves nothing, because at 0/0 there is no
coast left to turn off. Disabling the **sub coast** costs five lines. The sub
coast is a separate mechanism, is not reached by the pre and post lines, and is
what operates inside the interval.

`applyForSyncType()` already uses the real disable: `SP_NO_COAST_REG` is written
0 on the csync branch and 1 on the separate-sync branch. That bit, not the pair,
is what turning the coast off means.

## Nothing was starving on this source

`STATUS_SYNC_PROC_HTOTAL` held 2200 against a 2200 divider in all four states
above, including with every coast mechanism disabled. The ADC PLL never lost
lock. So on an unserrated NOR composite source the coast is not protecting PLL
lock: the edges missing through the vertical interval cost **counted lines**,
and the sub coast recovers five of them.

That is why removing the pre and post lines costs this source nothing and only
stops the shear -- the pre lines were blanketing seven lines of real,
correctly-spaced horizontal edges ahead of the interval.

## It does not generalise

The Wii at 480p will not hold at 0/0 at all: the count dithers 521..525 and the
sync pad cycles every ~2.2 s. Its vertical sync is serrated, so its interval
delivers edges rather than withholding them, and what the pair is covering there
is something the RISC PC's does not have.
[`the-equalisation-probe-cannot-see-serration.md`](the-equalisation-probe-cannot-see-serration.md)
has which sources need the hold and how to tell.

**So "0/0 is safe" is a statement about a source, never about the pair**, and
the mechanism by which the pre lines produce the shear on a source that does not
need them remains open.
