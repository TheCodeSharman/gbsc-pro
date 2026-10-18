# The coast the formatter needs is one the sync processor cannot count through

`640x512@50` on composite sync does not acquire. The engine solves it correctly
and then tears the solve down, over and over, because `STATUS_SYNC_PROC_VTOTAL`
reads anything from 97 to 1974 while the input formatter reads the source's own
frame throughout.

**The coast decides it, and the pair the engine derives is the wrong end of the
range.** Held short the sync processor counts the source perfectly; held at the
derived 7/6 it counts garbage.

## The points

`vga`, RiscPC on `SYNC 1`, `MODE X640 Y512 C256 F50`, output 1920x1080,
`/coast?pre=&post=` holding each pair against the engine's own derivation --
which it survives, `measureVerticalInterval()` declining to measure across a
coast held from outside. Each leg is 20 s of `STATUS_SYNC_PROC_VTOTAL` and
`VPERIOD_IF` read adjacently in one request, scored as the count landing inside
the formatter's own frame less the vertical sync width.

| pre/post | counts inside the formatter's frame | spread | state |
|---|---|---|---|
| 7/6, the derived pair | 7 of 31 | 147..1899 | absent |
| 7/4 | 5 of 34 | 302..1970 | absent |
| 7/2 | 3 of 29 | 228..1973 | absent |
| 3/2 | 6 of 33 | 97..1807 | absent |
| **1/1** | **42 of 42** | **529..531** | **acquired** |
| **0/0** | **43 of 43** | **529..531** | **acquired** |
| 18/10 | 6 of 34 | 97..1846 | absent |

**THE FIRST ROW IS THE CONTROL.** Writing the derived pair through the override
leaves the source indistinguishable from the default -- 7 of 31 against the
`absent` loop the engine reaches on its own -- so the route is harmless and the
counts belong to the VALUES.
[`the-coast-cannot-be-lengthened-on-sync-on-green.md`](the-coast-cannot-be-lengthened-on-sync-on-green.md)
is where that null row became a requirement.

## What the probe asks, and what it does not

`VideoPath::measureVerticalInterval()` takes the coast off and asks whether the
INPUT FORMATTER still completes a vertical measurement without it. On this mode
it measured the interval uncoasted in **0 of 8** and chose post coast 6.

That question is sound for the formatter and says nothing about the sync
processor, which is the half acquisition depends on: the count is what
`VideoSourceAcquisition` reads, what the source key is built from and what the
divider is solved against. So the pair is chosen to protect a measurement the
engine can do without here -- the formatter reads `VPERIOD_IF` 533 with
`STATUS_IF_VT_OK` 1 whatever the coast is on this source -- at the cost of the
one it cannot.

**AND IT IS KEYED ON THE INPUT SELECTION, NOT THE MODE.** One measurement per
input change, so a mode arriving later runs on a pair measured against whatever
was in force then.

## The coast is not a constant the bench shares

A short pair is not available as a global. Measured elsewhere on the same
source and cable: `SP_PRE_COAST` 0 reads 16.2% of 6407 emitted frames displaced
on `320x256@50` composite against 0.4..0.8% at 7, and on the Wii's sync on
green every value away from the shipped pair degrades or destroys the lock --
48/6 takes liveness to 2.8%. So what this measures is a pair that suits one
mode, not a better constant.

## What a short coast does NOT buy

At 1/1 the mode acquires with the divider and the window the separate-sync leg
solves -- `PLLAD_MD` 1424, `oh` 267, `eh` 1018, `cv` 534 restored from the
formatter against a raw count of 530 -- and **the emitted picture is still
wrong**: 34 output columns of black at the near edge and 198 at the far with
both borders whole, and the top border off the frame with 41 rows at the
bottom. At 0/0 no edge is readable at all.

So the coast is what stands between this mode and an acquisition, and something
further stands between the acquisition and a picture.

## What the count costs downstream, where it does acquire

The raw composite count runs 4 lines short of the frame -- 530 against 534,
which is this source's vertical sync width, the RiscPC's composite sync being a
NOR that carries no horizontal edges through the interval.
[`the-risc-pc-composite-sync-is-not-serrated.md`](the-risc-pc-composite-sync-is-not-serrated.md)

`SourceMeasurement::holdVerticalSync()` restores it from the formatter, but it
needs two consecutive agreeing reconciliations before it holds one, so a pass
taken before that keys on the raw count. **530 matches no published raster** --
no row carries 530 lines -- so that key is framed from the envelope and takes
the raster's default shape: `oh` 239 and `eh` 1763 of a 2041-unit line, which
is `0.117` and `0.864`, against the published `267` and `1018`. The console says
which happened: `source key: 534@50.16/613??, framing recalled` against
`530@50.16/613??, no framing stored, shape 13333`.

So one mode can be framed two ways inside one acquisition, and which it gets
depends on whether the restore had settled when the key was taken.
