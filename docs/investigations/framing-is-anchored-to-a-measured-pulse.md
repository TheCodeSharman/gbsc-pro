# The framing is anchored to a measured pulse, and that measurement moves a unit

`VDS_HB_ST` and `VDS_DIS_HB_ST` come out 1899 from a preset load and 1897 from a
re-solve of the same source. Both values are computed by the engine. What differs
between the two states is the **capture window**, by one input unit, and what
moves the capture window is the sync-processor reading the framing's coordinate
system is built on.

## The chain

`InputLine::measured()` derives the hsync pulse width from a live measurement:

    syncUnits = ceil(units * hlowLen / adcLine)

`units` is the IF line (1126 on the bench), `hlowLen` is
`STATUS_SYNC_PROC_HLOW_LEN` and `adcLine` is `PLLAD_MD`. That one number is then
both ends of the framing's coordinate system:

    firstCapture() = syncUnits
    capturable()   = lastCapture() - firstCapture()

`PanAndZoom` holds the framing as a float proportion, and `ActiveImage::place()`
expands it against both:

    start = firstCapture() + lrintf(origin * capturable())
    width =                  lrintf(extent * capturable())

So when the measurement crosses a `ceil` boundary, the origin and the denominator
move **together and in opposite directions**: `firstCapture` 80 -> 79 while
`capturable` 1044 -> 1045. A proportion seeded under one reading expands to a
different absolute window under the other.

`produced` is `capture x 1024 / scale`, so one extra capture unit at
`VDS_HSCALE` 557 is 1.84 output pixels, and the output window is 2 px wider.
That is the whole of the 1899-against-1897 disagreement.

## Measured

Three trials, RiscPC at 320x256@50 on VGA, IF line 1126. `ch` is the capturable
region `/geometry` reports; `start` is the absolute capture start,
`firstCapture + oh`.

| trial | state | ch | start | width | IF_HB_SP2 | IF_HB_ST2 | VDS_HB_ST |
|---|---|---|---|---|---|---|---|
| 1 | before | 1045 | 132 | 973 | 132 | 1105 | 1897 |
| 1 | preset load | 1045 | 132 | 973 | 132 | 1105 | 1897 |
| 1 | re-solve | 1045 | 132 | 973 | 132 | 1105 | 1897 |
| 2 | before | 1044 | 132 | 973 | 132 | 1105 | 1897 |
| 2 | preset load | **1045** | **131** | **974** | **131** | 1105 | **1899** |
| 2 | re-solve | 1045 | 132 | 973 | 132 | 1105 | 1897 |
| 3 | before | 1044 | 132 | 973 | 132 | 1105 | 1897 |
| 3 | preset load | **1045** | **131** | **974** | **131** | 1105 | **1899** |
| 3 | re-solve | 1045 | 132 | 973 | 132 | 1105 | 1897 |

Trial 1 agrees throughout because `ch` never moved. Trials 2 and 3 disagree, and
in both the disagreement appears exactly where `ch` changes.

The arithmetic closes with no residual. At `ch` 1044 the framing holds
`origin = 52/1044` and `extent = 973/1044`; re-expanded at `ch` 1045 those give
`round(52.05) = 52` and `round(973.93) = 974`, which is the observed
`start` 131 and `width` 974. A reset instead recomputes the default from
`line.units()` alone, which is `start` 132 and `width` 973 under either reading.

**The capture END never moves.** `IF_HB_ST2` is 1105 in all nine states. Only the
near end and the width differ, which is why `VDS_HSCALE` agrees while the window
does not.

## It does not reach the screen

Photographed, with automation frozen and `VDS_DIS_HB_ST` driven from its working
1899 down into the middle of a live test card so the blanking boundary has full
contrast either side of it -- the most favourable case there is. Three frames per
state, column means over the frame, compared as peak column difference in grey
levels:

| comparison | peak difference | where |
|---|---|---|
| the same value, photographed twice | 8.66 | scattered |
| a 100-unit move | 76.18 | camera columns 982..1056 |
| **a 2-unit move** | **6.84** | scattered |

The 100-unit move lands as a sharp 75-column strip, which calibrates the method
at 0.75 camera columns per output pixel. The 2-unit move is **0.79x the
same-state noise floor** and is spread across the frame rather than concentrated
at the boundary, so what it measures is camera noise.

So the two units are real, arithmetically explained, and below what the panel and
camera together can show at the boundary where they would show best.

## What it means for the suite

`test_a_preset_load_leaves_the_engines_values_not_the_sketchs` compares the
registers after a preset load against the registers after the engine re-solves.
Its premise is that both halves are at the same framing. A preset load carries the
framing **proportions** forward; the reset control recomputes the **default**. Those
agree only while the measured pulse width has not crossed a `ceil` boundary
between them, so the test's precondition is one it cannot assert by reading the
framing back -- `/geometry` reports `oh` relative to `firstCapture`, and both the
offset and the denominator move.

`ch` is the observable that settles it, and it is in the same `/geometry` payload.
