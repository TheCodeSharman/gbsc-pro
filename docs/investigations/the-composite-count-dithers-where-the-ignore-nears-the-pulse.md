# The composite count dithers where the pulse ignore nears the source's pulse

`SP_H_PULSE_IGNOR` is `divider x 49 / 1000`, and on 640x512@50 that lands at
**79% of the source's own sync pulse**. The count then visits five values
instead of one, and everything keyed on it follows: the scan type flaps into
interlace and back, the motion-adaptive deinterlacer engages on a progressive
source nineteen times in ninety seconds, the frame hold that restores the lines
an unserrated vertical interval costs never latches, and the capture is sized
from a count short by the vertical sync.

Lowering the one register to 48 makes the count steady. Nothing else in the
separation group moves it.

Measured on `vga`, RISC PC under `RetroScaler-Acorn.mdf`, output 1920x1080,
16:9 declared.

## A settled acquisition is not settled

Ninety seconds of console on each leg, each reached by a sync-type change, a
`MODE` and a `/sc?~`:

| leg | `cv` | solves armed | scan changes | motion adapt | rate matches | counts seen |
|---|---|---|---|---|---|---|
| 640x512@50 composite | 529 | 5 | **39** | **19** | 34 | 529, 530, 531 |
| 640x512@50 separate | 534 | 0 | 0 | 0 | 0 | -- |
| 320x256@50 composite | 624 | 0 | 0 | 0 | 0 | -- |

The two controls are silent for the whole window. The composite leg of the one
mode re-solves, re-matches its rate and weaves a progressive source for as long
as it is left.

**`SourceMeasurement` holds a second steadiness run for the scan decision and a
count visiting three values satisfies it**, because `CrossingsForInterlace`
counts returns to a value and a wandering count returns as readily as an
alternating one. `an-alternating-count-latches-the-scan-type.md` is what earns
the pair.

## The restoration misses seven acquisitions in eight

The count an unserrated composite source gives is short by the mode's vertical
sync, and `SourceMeasurement::reconciledFrame()` puts the lines back from
`VPERIOD_IF`. Eight acquisitions of 640x512@50 composite, each a `MODE` and a
`/sc?~`:

| landing | acquisitions | `cv` | `PLLAD_MD` |
|---|---|---|---|
| restored | **1** | 534 | 1424 |
| short | **7** | 529 or 530 | 1746 |

The separate-sync leg of the same mode reads 533 in every sample and sizes its
capture at 534 every time.

**The two landings are not two dividers.** `/sampleclock?hold=` solves the
whole engine around a divider, and holding the restored landing at 1746 leaves
`cv` 534 behind only because the frame is already held; holding the short
landing at 1424 leaves it at 530. The divider follows the count, not the other
way round.

## `VT_BAD` 0 does not mean the value is the frame

Held at 1746 from a restored state, `VPERIOD_IF` reads **530 with `VT_BAD` 0**
-- the short count itself. `reconciledFrame()` accepts it, because 530 against
a count of 530 is a non-negative difference of zero inside the eight-line
bound, and restores nothing. The reading is wrong, stable and flagged healthy,
which `vperiod-if-needs-composite-sync-in-the-signal.md` already states as the
shape to expect.

In the short landing reached by its own solve it is debris instead: 789, 818,
822, 1030, 1044, 1513, 1576, 1827 and 1857 across the acquisitions measured,
`VT_BAD` 1 throughout, so nothing is offered at all.

## The ignore threshold is what the count answers to

Frozen on the solve, every write read back, the shipped value repeated between
every treatment. 25 samples of `STATUS_SYNC_PROC_VTOTAL` per row:

| treatment | count |
|---|---|
| control, `SP_H_PULSE_IGNOR` 85 | 527 x1, 528 x3, **529 x16**, 530 x5 |
| `SP_H_PULSE_IGNOR` 85 -> 16 | **530 x24**, 531 x1 |
| `SP_H_PULSE_IGNOR` 85 -> 48 | **530 x23**, 531 x2 |
| `SP_H_PULSE_IGNOR` 85 -> 64 | 528 x9, 529 x13, 530 x3 |
| control | 528 x6, 529 x10, 530 x9 |
| `SP_PRE_COAST` 7 -> 3, 11, 15 | dithers at every value |
| `SP_POST_COAST` 6 -> 0, 4, 9 | dithers at every value |
| `SP_VSYNC_TGL_THD` 3 -> 1, 5 | dithers at both |
| `SP_DLT_REG` 192 -> 112, 1024 | dithers at both |

The boundary sits between 48 and 64, which is between 45% and 60% of the
source's pulse. Nothing else in the separation group moves the reading at all,
which is the same result `the-risc-pc-composite-sync-is-not-serrated.md`
records for the group on a mode where the count is already steady.

## Where the threshold sits against the pulse

80 samples of `STATUS_SYNC_PROC_HLOW_LEN` beside the count, frozen on the
solve:

| mode | divider | ignore | pulse | ignore is | count |
|---|---|---|---|---|---|
| 640x512@50 | 1746 | 85 | 8 x1, **107 x73**, 108 x3, 110 x1, 118 x2 | **79%** | 527..531 |
| 320x256@50 | 2200 | 107 | 151 x1, **153 x79** | **70%** | 307 x2, 308 x78 |

The fraction of the LINE is one constant and the fraction of the PULSE is not,
because the two modes do not spend their lines alike: 153 of 2200 is 6.95% and
107 of 1746 is 6.13%. The mode whose pulse is the smaller share of its line is
the one whose threshold climbs into it.

`the-pulse-ignore-is-a-fraction-of-the-line.md` states the bound as a sync
pulse wider than 4.9% of the line. **That bound is too loose**: 6.13% is above
it and dithers.

## The safe band, in shares of the line

The register counts ADC samples, so one value is a different share of the line
at every divider and the shares are what compare. 20 samples of
`STATUS_SYNC_PROC_VTOTAL` per value, frozen on the solve:

| source | pulse, share of line | holds the count | dead band | loses lines |
|---|---|---|---|---|
| Wii 480p, `ypbpr`, divider 1212 | 7.2% | **0.99 .. 5.94%** | none | 6.44% |
| RISC PC 320x256@50 csync, 2200 | 6.95% | **2.7 .. 5.2%** | none | 5.5% |
| RISC PC 640x512@50 csync, 1424 | 6.11% | 2.81%, **4.49 .. 5.06%** | **3.09 .. 4.21%** | 5.34% |
| RISC PC 640x512@50 csync, 1746 | 6.13% | 0.92%, **2.75%** | -- | 3.67% |

**No value above 2.81% holds all four.** The shipped `49 / 1000` is 4.9%, which
is inside the band on three of them and outside it on the fourth -- and the
fourth is one of the two dividers one mode lands on, which is why that mode
alone is bistable.

The 640x512@50 surface is **not monotonic**: at 1424 the count is right at
2.81%, garbage from 3.09 to 4.21% -- 890, 1063, 1671 against a true 533 -- and
right again from 4.49 to 5.06%. A boundary found by halving is not the
boundary.

**The unserrated sources bound it from above only.** The Wii at 480p is
indifferent across six-fold range and collapses to 97 at 7.43%, just past its
own 7.2% pulse, so the only rule an unserrated source states is *below the
pulse*.

## What bounds it from below is the serrated source, and the two do not overlap

The floor is the Wii in PAL 576i on `ypbpr`, through the same 2200-sample line
and a 161-sample sync pulse. Frozen on the solve, 24 readings of
`STATUS_SYNC_PROC_VTOTAL` per value against a true count of 310:

| `SP_H_PULSE_IGNOR` | share of line | count |
|---|---|---|
| 22 .. 73 | 1.00 .. 3.32% | **319 x24 at every value** |
| 77 | 3.50% | 312, 313, 314, 317, 318, 319, 320, 321 |
| 81 .. 123 | **3.68 .. 5.59%** | **310 x24 at every value** |
| 132 | 6.00% | 310 x12, 311 x6, 312 x6 |

Nine lines, steady and wrong, everywhere below the floor: the separator is
counting the vertical interval. The floor sits between **3.32% and 3.68%**,
and 6.00% is where real sync pulses start being rejected.

**The ceiling the unserrated sources impose is 2.81%.** The two bands do not
touch, so **no constant serves every source** and the fraction follows a
measurement.

## What the engine does now

`Tv5725::SerrationMeasurement` compares the count through the two thresholds
and holds the verdict for the source, the way `SyncMeasurement` holds the sync
type. The median of five readings at each, four lines of separation before an
interval is one rather than the count's own dither, and a reading no source
produces establishes nothing. The probe runs after the install and before the
duty -- the thresholds being fractions of the line in ADC samples, a probe
through a reference clock is of nothing -- and only on a composite source,
a separate-sync one being indifferent across the whole range.

**Serrated until measured otherwise.** The higher threshold costs an
unserrated source a dithering count until the probe runs; the lower one leaves
a serrated source counting its interval, nine lines out, which no raster
matches and every window is then solved from.

Measured on the unit, one acquisition each unless stated:

| source | threshold | count | capture |
|---|---|---|---|
| Wii 576i, `ypbpr` | **107**, logged `serrated` | 310 | -- |
| 320x256@50 composite | **61** | 308 x8 | 624, unchanged |
| 800x600@60 composite | **37** | 623/624 | 628, unchanged |
| 640x512@50 composite | 48 or 39 | steady | **534** |
| 640x480@60 composite | 40 | 522 | 523 or 525 |

**What it buys is the scan latch.** 640x512@50 composite over ninety seconds
of a solved acquisition, twice:

| | before | after, two runs |
|---|---|---|
| scan changes | 39 | **0, 0** |
| motion adapt engaged on a progressive source | 19 | **0, 0** |
| capture | 529 | **534, 534** |
| solves armed | 5 | 11, 8 |

## What it does not fix

**The frame hold races the capture's sizing.** `holdVerticalSync()` adopts a
frame only once two offers agree, and the capture is sized from whatever the
count is when the solve reaches it -- so a restoration that arrives a pass
late leaves the capture short by the vertical sync. Measured after the change:
640x512@50 composite restores on 3 acquisitions of 8, and 640x480@60 composite
lands on 525 three times and 523 three times in six. Both read `VPERIOD_IF`
valid in the short landings, so the reading is there and the hold has not
taken it.

**And the solves armed went up rather than down**, 5 to 8 and 11. The arm is
`countContradicted()`, which is a separate fault -- `../known-issues.md`.

## What the threshold is rejecting is open

The pulse reads 107 in 73 samples of 80 and never below it except for one
reading of 8, so the normal line's pulse is nowhere near 85 and HTTP sampling
does not catch whatever is. A partial pulse at the vertical interval's boundary
accounts for the shape -- one line, present or absent per frame, on a NOR
composite signal whose horizontal edges stop for the whole of vertical sync --
but the pulse that is being rejected has not been caught. `SamplingLog` reads
`HLOW_LEN` from inside `loop()` and is what could.

**The trade the fix has to make cannot be measured here.** The threshold exists
to reject broadcast equalising pulses, at about 3.6% of the line against a 7.2%
sync pulse, and the only serrated source this board can produce comes through
the ADV7280 on composite or S-Video, which is not on the bench.
`../bench-sources.md`.

## `acquired_and_settled()` cannot see this

The gate polls `/geometry` for `acquired` plus a held `ch`, `cv`, `eh` and
`lineRateHz`, and a re-solve that lands on the same numbers moves none of them.
It returns true on this leg while the engine is cycling, so a freeze taken on
it catches the search configuration -- `PLLAD_MD` 2040, `SP_H_PULSE_IGNOR` 99,
`SP_H_CST_SP` 256 -- and every count read afterwards is of an unlocked block.
Gate on the divider leaving the reference instead.
