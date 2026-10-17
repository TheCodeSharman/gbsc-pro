# The composite roll is lost horizontal pulses, and only the doubled scan shows it

`vga` on composite sync at 320x256@50 into 1080p rolls vertically; the same
source on separate sync does not, and the same sync arrangement at 800x600@60
does not. This records what the roll coincides with, which scan it belongs to,
and every register that was walked against it without moving it, and then
what closed it: the separator and the ADC PLL referencing the trailing edge of
the pulse.

Every reading is `tools/gbsc-pro-hwtest/picture_stability.py`'s share of
displaced frames over an 8 s clip, taken frozen, with a control clip between
every treatment and the register written and read back. The control share
drifts between 4% and 29% inside one acquisition, so a treatment is judged
against its neighbours and only a share near zero is a result.

## The shape of an event

One 8 s clip, frame by frame, `.` still, `+` half a line, `#` one line, `X`
tens of lines:

```
.............................####++.........................####+.++.....####+..
.....##++.........##+................................................##++......###+
..+##+......................##++..###.#..................####+...................###+....+##+
##+.+...............XXXXXX+.....
```

A displaced frame is never alone. The event is a run of about four frames one
source line down, then one or two at half a line, then back -- `####++` -- at
one to three a second, with an occasional run of six frames displaced by 195
to 260 rows. Offsets land on 2, 4 and 8 rows, which at 4.22 output rows a
source line is half a line, one line and two.

## It is the same event as a short count

The sampling log at 25 ms, which samples from inside `loop()`, reads
`STATUS_SYNC_PROC_VTOTAL` 308 in about 93% of samples on this state and 304,
305 or 307 in the rest -- 7.4%, 7.1%, 5.5%, 10.4%, 5.2% over five controls --
while `VPERIOD_IF` reads 623 in 752 of 753. The vertical pulse arrives on time
every field; it is horizontal pulses that go missing, three or four at a time,
in about one field in fifteen.

Taken together with the capture, time-aligned at 50 ms, the two coincide: every
displaced run sits on a short count in the same bin or the one before it, and
the runs of tens of lines sit on repeated short counts. Frame on the left,
count on the right, per 50 ms:

```
..#.#.+...............# ......#v#.+.......#.+..v#.#....................v.v.vX.X.
...........................v#.+....................v.v.vXVXvX.#..............
..#.+..v#.+.#.#...#v#......v#.+....v#.#v#.+....v#.+............ #v#....v#.#.+...
```

**The count is a correlate and not the cause.** `SP_H_PULSE_IGNOR` 30 against
the engine's 107 takes the short-count share to 0.0% of 311 samples, the
count then reading 308 with 309 and 307 beside it, and the roll is unchanged
at 19.8% against 26.7% and 18.3% either side.

## Which scan it belongs to

| state | displaced |
|---|---|
| 320x256@50, separate sync, doubled | 0 of 514, every run, every session |
| 320x256@50, composite sync, doubled | 12% to 29% |
| 800x600@60, composite sync, undoubled | **0 of 514** |

So it is composite sync on the line-doubled scan, and nothing that composite
sync does to the sync processor on its own.

## Refuted: the input formatter's interlace classification

On composite the formatter's vertical measurement completes and
`STATUS_IF_INP_PAL_INT` reads 1 in every sample -- 623 half-lines is an odd
count, which is what an interlaced frame is -- and on separate sync the
measurement never completes, so the obvious reading was a field-parity flip.
`MD_PAL_INT_CNTRL` moved off 38 clears the bit and leaves the roll at 17.9% and
17.1% against controls of 15.2% and 15.6%. The progressive selects do no
better: `IF_PRGRSV_CNTRL` 1 with `IF_LD_SEL_PROV` 1 reads 12.3%, with
`IF_HS_FLIP` 1 as well 19.1%, `IF_VS_SEL` 1 with `IF_LD_WRST_SEL` 0 16.1%, each
against a neighbouring control of 14% to 21%.

## Refuted: the VDS re-locking to the input

The runs of four frames read like a lock that re-places itself every few
frames, and `VDS_FRAME_RST` 4 is "lock every five frames". But `VDS_SYNC_EN`
and `VDS_FLOCK_EN` both read 0, so the VDS free-runs and the frame time lock
steers the clock; the register is inert.

## Refuted: a steady count is a steady picture

`SP_DLT_REG` 4095 reads 308 in 319 of 319 samples, the steadiest count seen on
this source, and the picture rolls in 89.5% of frames with
`STATUS_IF_INP_PAL_INT` 0: the separator has stopped extracting vertical sync
at all. 2048 and 3000 do the same. The count cannot judge this fault.

## Refuted: the H coast stop past the line

`SP_H_CST_SP` at or past the 27 MHz line length of 1728 -- 1727, 1735, 1760,
1900, 4095 -- reads 0 of 514 displaced on every trial, and 1700 reads 15%,
1550 32%, 1400 38%, against the engine's 1672 at 18% to 27%. It is not a fix.
`STATUS_SYNC_PROC_HLOW_LEN` reads 1 there against 154, the count pins at 199
on one trial, and the frame carries a magenta band of unwritten memory a
hundred rows high that grows over thirty seconds, or a sheared picture.
**The instrument is blind to that**: a band is not a displacement, and the
rest of the frame correlates at lag 0.

## Walked without effect

Each at 6 or 8 s, frozen, against a neighbouring control, share in percent:

| register | value | share | controls |
|---|---|---|---|
| `SP_PRE_COAST` | 2, 12 | 20, 15 | 12..18 |
| `SP_POST_COAST` | 2, 12 | 9, 14 | 14, 11 |
| `SP_DIS_SUB_COAST` | 1 | 22 | 14 |
| `SP_H_PROTECT` | 0 | 10 | 14 |
| `SP_HS_INV_REG` | 1 | 10 | 11 |
| `SP_DLT_REG` | 48, 512, 1024 | 17, 23, 5 | 16, 26, 12 |
| `SP_SDCS_VSST_REG_L` | 4, 30 | 28, 10 | 14, 8 |
| `SP_VSYNC_TGL_THD` | 1, 8 | 12, 14 | 16, 10 |
| `SP_V_PRD_EQ_THD` | 1, 8 | 7, 16 | 11, 8 |
| `SP_VT_DLT_REG` | 4, 200 | 11, 11 | 14, 10 |
| `SP_SYNC_WIDTH_DTHD` | 20 | 7 | 7 |
| `SP_JITTER_SYNC` | 1 | 14 | 8 |
| `SP_HS_LOOP_SEL` | 1 | 7 | 12 |
| `ADC_SOGCTRL` | 6, 14 | 12, 9 | 11, 11 |
| `SP_RT_VS_ST`, `SP_RT_VS_SP` | 0, 4 | 9, 9 | 8, 11 |
| `IF_VS_FLIP` | 0 | 35 | 10 |
| `SP_H_PULSE_IGNOR` | 60 | 18 | 4 |
| `MD_SEL_VGA60` | 1 | 14 | 12 |
| `IF_LD_WRST_SEL` | 0 | 8 | 14 |
| `IF_LD_ST` | 0 | 10 | 9 |
| `SP_H_CST_ST` | 200 | 19 | 18 |
| `SP_CS_HS_ST`, `SP_CS_HS_SP` | 150, 200 | 22, 22 | 24, 23 |
| `SP_HS_REG` | 0 | 23 | 25 |
| `SP_HCST_AUTO_EN` | 1 | 31 | 27 |
| `SP_HD_MODE` | 1 | 53 | 17 |
| `SP_H_COAST` | 1 | no picture | 26 |

And with the short-count share as the instrument, frozen, 8 s each, controls
5% to 10%: `SP_H_TIMER_VAL` 10 and 200, `SP_T_DLT_REG` 4095, `SP_L_DLT_REG`
255, `SP_SYNC_PD_THD` 0, `SP_SYNC_TGL_THD` 1, `SP_HT_DIFF_REG` 4095,
`SP_PRD_EQ_THD` 1, `SP_H_TOTAL_EQ_THD` 255, `SP_HS_EP_DLY_SEL` 1,
`SP_HS2PLL_INV_REG` 1 -- each inside the band.

## What it was: the PLL locked to the trailing edge

The chip's default edge reference for the separator and for the hsync into the
ADC PLL is the rising edge -- `SP_CS_P_SWAP` 0 and `SP_HS2PLL_INV_REG` 0. The
RISC PC's composite sync is negative-going, so the rising edge is the TRAILING
edge of every pulse, which is the edge that moves through the vertical
interval. On the leading edge both ways -- `SP_CS_P_SWAP` 1 and
`SP_HS2PLL_INV_REG` 1 -- the roll stops. Frozen, in the engine's own
composite configuration, 8 s clips:

| `SP_CS_P_SWAP` | `SP_HS2PLL_INV_REG` | displaced |
|---|---|---|
| 0 | 0 | 19% to 24%, whole lines and 190..260-row jumps |
| 0 | 1 | 24% |
| 1 | 0 | 12% to 21%, half lines only |
| **1** | **1** | **0.0% to 2.5%** |

Neither bit alone closes it. The swap alone halves the share and turns the
whole-line events into half-line ones, which is what pointed at the edge. The
vendor firmware writes the same pair, with `SP_HS_PROC_INV_REG`, for a 15 kHz
composite source on its bypass path; the decimator invert adds nothing here.

Engine-solved, unfrozen, 20 s clips on the bench mode: **6 and 13 of 1283
frames displaced** against a fifth of them before, and
`test_picture_holds_still.py`'s composite leg reads 0 of 514.

The Wii at 480p on `ypbpr`, sync on green, undoubled, holds still at 0 of 514
with either edge, so the pair is keyed on the csync path -- the separator in
circuit -- rather than on a measured polarity, which on that path reports the
separator and not the source.

## The edge moves the clamp and the capture origin by one pulse

Both count from the edge the separator references, so moving it moves them.
Measured on the Wii at 480p into 1920x1080, divider 1212, frozen, with the
pair written by hand:

| state | left border | top / bottom | mean luma |
|---|---|---|---|
| default edge, engine's clamp 19..23 | 259 | 35 / 22 | 143 |
| leading edge, clamp 19..23 | 240 | 1 / 1 | 162 |
| leading edge, clamp 81..85 | 383 | 30 / 26 | 135 |

With the clamp left where the trailing edge put it, the window sits on the
sync tip: the frame lifts and the Wii's own black bars read grey, which is
the row of ones. Moved by the pulse -- 88 samples of 1212 on a 858-count line
is 62 of the clamp's 27 MHz counts -- the bars are black again, and what is
left is the picture 124 columns to the right, which at 1.41 columns a sample
is the 88-sample pulse exactly. `SP_CS_P_SWAP` alone does all of this; the
PLL invert alone changes nothing visible on the Wii.

The firmware adds the pulse to the clamp on the leading-edge path through
the same offset an inverted SOG reference already took, since both move the
reference by one pulse.

## The retime stop is inert on the separator path; the separator's own stop is the origin

With the pair in and the clamp right, the bench mode sits 171 columns to the
right of where separate sync frames it, 414 against 243 at the left border.
`SP_RT_HS_SP` does not reach it: written 2128 and 77 frozen, and written 77 by
the engine, the picture is where it was. `SP_CS_HS_SP`, the separator's
regenerated pulse end, moves it one for one:

| `SP_CS_HS_SP` | 0 | 40 | 80 | 120 | 160 | 170 |
|---|---|---|---|---|---|---|
| left border | 414 | 372 | 330 | 288 | 246 | 241 |

**It counts ADC samples.** Against a pan of 20 capture units inside the same
acquisition -- 42 columns on the doubled bench mode, 30 on 800x600@60 -- the
register moves the picture 1.00 and 1.03 samples a count. `SP_CS_HS_ST` moves
nothing between 16 and 120.

So on the leading edge the regenerated pulse would end at the leading edge,
and the capture counter that zeroes on it moves a whole pulse earlier. The
firmware writes the stop as the measured pulse width in ADC samples on that
path, 151 of 2200 on the bench mode, which puts the pulse's end where the
source's is and leaves every register downstream of it -- the separator lead
the capture window carries included -- meaning what it meant on the trailing
edge. The bench mode then frames at 257 against separate sync's 242, where
the trailing edge framed it at 252: the per-sync residual of about ten
columns is older than this change.

The vendor sketch has a hand shift of `SP_CS_HS_ST` and `SP_CS_HS_SP` modulo
the line, which is consistent with the unit.

## The static setup must not write the edge

`SyncProcessor::init()` runs after the sync type on the re-detection route,
as it does for `SP_SOG_MODE`: with the pair written there, a composite source
came back from `/sc?~` on the default edge every time while a boot landed on
the leading one. The pair has one owner, the sync-type application, and the
static setup leaves it alone.

## What is open

**A residual of about one frame in a hundred**, two to 25 rows and an
occasional 100..230-row jump: 13 of 1283 over 20 s, controls between 0.0% and
1.6% inside one acquisition. `SP_PRE_COAST` 0 with `SP_POST_COAST` 0 read 0.0%
on the first two legs, against controls of 1.4% and 0.3% beside them, and is
the lead; it is the coast the csync branch writes for every separator source
and the Wii's sync on green has not been checked with it. `SP_DLT_REG` 300
reads 1.0% and is not.

And 640x480@60 on SEPARATE sync emits a black frame on an acquired source --
524 lines, `PLLAD_MD` 1272 against the 1444 composite solves for the same
mode -- on two attempts, which is what stopped the stop's unit being read on
that mode. 640x240@60 on separate sync does the same. Filed in
`docs/known-issues.md`.
