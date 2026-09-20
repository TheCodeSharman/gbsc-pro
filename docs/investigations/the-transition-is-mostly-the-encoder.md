# A source mode change is mostly the encoder, not the engine

Measured on `vga`, RiscPC via ModeServ, 640x480@60 -> 320x256@50, with the
console and a camera clip on one clock.

The engine's share of the dark period is under a quarter of it. What the panel
is waiting for is the HDMI encoder re-acquiring after the output sync comes
back, and no register on this board can see that -- only a camera can.

## The split

| event | since the command |
|---|---|
| engine sees the source go | 0.11 s |
| panel goes dark | 0.00 s |
| solve completes, sync pad re-enabled | **1.83 s** |
| panel lights | **8.07 s** |

So 1.83 s of engine and 6.24 s of encoder relock.

The clip's timebase is offset from the host's by about a second -- `tv-snap
--clip` takes time to start recording -- so the two are aligned on the frame the
panel goes dark, which the engine's own blank puts 0.11 s after the command.
**A span timed from the clip's frame 0 is wrong by that offset**, and reading one
that way put the panel "dark before the change" three times.

## The output raster does not change across the leg

Settled reads either side, same source, same framing:

| | `VDS_HSYNC_RST` | `VDS_VSYNC_RST` | `PLLAD_MD` | `IF_HSYNC_RST` | `HPERIOD_IF` |
|---|---|---|---|---|---|
| 320x256@50 | 1915 | 1124 | 2206 | 1103 | 431 |
| 640x480@60 | 1915 | 1124 | 2039 | 1124 | 212 |

The raster and the display clock are identical, so the encoder is never asked
for a different HDMI mode. The 6.24 s is it reacting to sync being taken away
and given back, which is the same behaviour the stale-timing fix depends on.
`encoder-stale-timing.md`.

**The consequence is that the lever on transition time is the blank, not the
output mode.** Choosing one output resolution for every source would not shorten
this leg, because it is already one resolution.

The display clock is NOT identical across it, and the table above does not carry
it. The output frame time follows the source, so 60 Hz and 50 Hz are two pixel
clocks on one raster -- which is a second HDMI mode, and is why this particular
leg still owes the encoder a re-look.

## Against the 2026-08-02 reference build

`be58a24f4` with the bench instruments, against the current engine. Three runs
each, same leg, same threshold rule -- dark is below 45% of the clip's own lit
level, printed per clip so the threshold can be checked rather than trusted.

| | longest dark | frames that are neither picture nor black |
|---|---|---|
| reference | 5.10 / 5.13 / 5.13 s | 7 / 2 / 3, plus a separate 0.10-0.17 s dark run in two of the three |
| current | 8.07 / 7.97 / 8.00 s | 0 / 2 / 1, and those are the single transition frame at each edge |

The reference passes through partial frames at mean 55..78 against a lit 106
before its own dark period. That is the visible junk. The current engine goes
card to black to card with one frame at each boundary and shows none.

So the engine costs about 2.9 s more and buys a clean transition with it. The
reference's own sync-away time is NOT measured, so "the reference does not blank"
is an inference and the 6.24 s relock has no reference figure to sit against.

**THE LONGEST DARK RUN IS THE ANSWER, NOT THE FIRST.** The reference flickers
dark for a tenth of a second before the real span, and a profile that stops at
the first run reports 0.10 s for a 5.13 s transition.

## The divider is not a function of the source alone

`PLLAD_MD` came out **2250, 2206 and 2202** across solves on one unchanged
source, 311 lines at 50 Hz, with `HPERIOD_IF` reading a correct and stable
**431** every time. It is not the railing: a railed register reads 255 or 511,
and `hperiod-if-railing.md` has the distribution.

What moves is which measurement answered. `SourceMeasurement::measureLineRate()`
takes `measureLineRateFromHPeriod()` first and measures the field rate only when
that refuses, and the two do not agree:

```
sampling: 311 lines x 50.08 Hz -> line rate 15625     <- 27e6/(4 x 431) - 1
sampling: 311 lines x 50.19 Hz -> line rate 15661     <- count x field rate
```

0.23% apart, the divider follows the rate, and `IF_HSYNC_RST` follows the
divider -- 1103 against the 1125 a divider of 2250 gives.

**THE RATE PATH IS NOT WHAT MOVED IT, AND THAT IS MEASURED.** With the path now
named in the sampling line, thirteen solves on one unchanged source alternating
the two ways of reaching one gave `PLLAD_MD` **2206 in all thirteen** -- six by
the line period and seven by the field rate, at 15625 both ways. Both paths
agree on this source, so what varies the divider is the measured FIELD RATE
landing on 50.19 rather than 50.08, and what does that is unmeasured.
`hperiod-if-railing.md` has the table. The capture's framing
constants do not follow it, so a capture positioned at a fixed count of IF units
begins at a different fraction of the source line depending on which path
answered. That is the mechanism `../known-issues.md` records for a picture
sitting left with a coloured band down one side.

## The relock is the blank's cost, and it quantises

Measured by taking the sync pad away for a fixed hold with **nothing else on the
board moving** -- the same source, the same raster, the same windows -- so the
whole dark period is the encoder and the sink.

| blank mechanism | hold | panel dark | dark after the blank lifted |
|---|---|---|---|
| `PAD_SYNC_OUT_ENZ` | 0.3 s | 3.67 s | 3.32 s |
| `PAD_SYNC_OUT_ENZ` | 2.0 s | 7.60 s | 5.58 s |
| `PAD_SYNC_OUT_ENZ` | 4.0 s | 7.57 s | 3.53 s |
| display aperture collapsed | 2.0 s | **2.10 s** | **0.10 s** |
| `DAC_RGBS_PWDNZ` 0 | 2.0 s | **2.07 s** | **0.07 s** |

**IT IS NEITHER FLAT NOR LINEAR.** The total dark period lands at about 3.7 s or
about 7.6 s and nothing in between, so the sink retries on a cadence of its own
and a blank is paid in whole quanta -- a 4.0 s hold costs the same as a 2.0 s
one, and a 0.3 s hold costs a whole quantum less than either. Reading the
middle row alone gives a relock that looks like it scales; three points are what
refuse that.

**And a blank the encoder never sees costs nothing.** Both video-domain blanks
returned the picture within a frame or two of the register going back, because
the link never dropped. The panel shows black rather than losing signal.

So the lever on transition time is **which domain the blank is in**, and the
6.24 s above is not a cost of the transition -- it is a cost the blank creates.

## What the engine does now

`Tv5725::VideoPath::showOutput()` closes the display aperture, which only the
VDS can see. The sync pad is spent on one thing: making the encoder look again
when a solve has moved the timing it is locked to -- both raster totals and the
field rate, because the output frame time follows the source and one raster at
two field rates is two HDMI modes. `EncoderRelookMs` holds it away for 300 ms,
inside the sink's first retry.

The aperture is collapsed to one active line rather than to none: a start at the
stop is not a case RD-5725-1.1 states behaviour for, and one line is what the
bench measured black.

Measured after the change, `vga`, same field rate, both ends on the scaling
path: **0.20 s to 0.30 s of dark panel**, against the 8.07 s above.

**A FIXED BRIGHTNESS THRESHOLD CANNOT MEASURE THIS ANY MORE.** Two source modes
light the panel to different levels -- 48 and 144 on one leg here -- so a
threshold taken from the brighter one reads the *other picture* as dark and
reports a 4.20 s gap where 0.30 s is the truth. Find the excursion by stepping
between settled levels instead.

## The sync-type probe is the rest of a composite source's transition

On composite sync the probe is on the critical path and spends its whole window:

```
3.00  MODE sent
3.17  source moved: interrupt (308 lines, solved 308)
4.17  own V sync: no after 1000ms
5.04  sampling: 308 lines x 50.56 Hz -> line rate 15625
5.13  sampling phase: chosen, oversample 4
```

About 2.0 s of engine with 1.00 s of it waiting out `OwnVsyncWindowMs`. A source
that has its own V sync answers in 2-3 ms, so the whole cost falls on composite
sources. `sync-type-selection.md`.

## What is left

- The reference build's sync-away time, which is what turns the 2.9 s
  difference into an attribution.
- The shortest sync drop the encoder still notices. 0.3 s works; below that is
  unmeasured, and it is what decides whether a genuine output-mode change can
  land inside one quantum.
- `VDS_VSCALE` read 533 against capture heights of 622 and 523 on two different
  sources, where the horizontal scale did track. Two sources are two
  `SourceKey`s and two framings, so this is not evidence about the proportional
  framing invariant -- but a vertical scale that does not move with a 19% change
  in capture height is worth one reading.
