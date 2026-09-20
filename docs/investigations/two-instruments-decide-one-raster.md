# Two instruments decide one raster, and they disagree

The output raster is solved from the measured line rate. Two things measure it,
they disagree by about a third of a percent on the bench source, and **whichever
one answers on the single pass that solves latches the raster for good**. So the
same source, re-detected twice, produces two different output modes.

Measured on the bench RiscPC at 320x256@50, `vga`, two `/sc?~` recoveries
minutes apart with nothing else touched:

| held `lineRateHz` | `VDS_HSYNC_RST` | picture |
|---|---|---|
| 15625 | 1915 | full screen, correct |
| 15575 | 1922 | full screen, correct |

Both display. The raster is not wrong either time -- it is *arbitrary*, and a
sink shown a line total that moved by seven has re-locked for no reason the
engine chose.

## Why the two disagree

`SourceMeasurement::measureLineRate()` prefers the counter and falls back to the
field rate:

- **The counter.** `HPERIOD_IF` states the input line period in 27 MHz quarters,
  so `lineRateForHPeriod()` is `27e6 / (4 x (n + 1))`. At the bench source's
  n = 431 that is 15625.0 Hz, exactly.
- **The field rate.** `getSourceFieldRate()` spins for vsync edges and the rate
  is `fieldRateHz x (sourceLines + 1)`. Measured 50.08 Hz on one pass and 49.92
  on another, which is 15625 and 15575 against the same 311-line count.

The field rate carries about +-0.3% of spin noise and the counter carries none,
so the fallback is the less precise instrument and it is the one that decides
the raster whenever the counter cannot answer.

**Nothing corrects it afterwards.** `ratesAgree()` is 5%
(`HeldRateTolerancePerMille` 50), so 15625 and 15575 *agree* -- the counter
recovering later does not re-arm a solve, and the raster stays where the fallback
put it for the life of that acquisition.

## The counter is unavailable far more often than "occasionally"

`/samplinglog?ms=25&for=30000`, 1109 samples, on an acquired source with a clean
picture and `STATUS_SYNC_PROC_VTOTAL` reading 311 in 1107 of them:

| `HPERIOD_IF` | samples |
|---|---|
| 511 | 685 |
| 510 | 65 |
| 255 | 25 |
| 2..18 | the remainder |
| **431, which the mode is due** | **0** |

Not one reading in 1109 is the right value. In that state the field rate is not
a fallback, it is the only instrument, and every raster is sized from a spin
measurement. `hperiod-if-railing.md` is the register's own fault; what this page
adds is what the engine does with it.

## The counter's samples are taken and then thrown away

Eight readings are taken and checked to agree, and then seven are discarded:

```cpp
static const uint8_t  HPeriodSamples   = 8;
static const uint16_t HPeriodAgreement = 2;
...
const uint32_t rate = lineRateForHPeriod(samples[0]);
```

**The permitted spread is larger than the difference that moves the raster.** A
step of one in `n` is `27e6 / (4 n^2)` -- 36 Hz at the bench source's 15625, and
146 Hz at the Wii's 31395. A spread of 2 is therefore 72 Hz at 15625, where the
50 Hz between the two instruments already moved the raster by seven units. So on
a dithering counter *which sample arrived first* decides the output mode.

Averaging them is free and finer than any single reading, because a dithering
counter is reporting a period that lies between two integers. Over eight samples
it stays integer arithmetic:

    rate = 27e6 / (4 x ((sum / 8) + 1))
         = 54e6 / (sum + 8)

which is exact, needs no float on a part with no FPU, and resolves to an eighth
of a step. `sum` of eight 431s gives 54e6 / 3456 = 15625, unchanged where the
counter is steady.

## What this does not explain

**The 48 px pan after a bypass round trip is a different fault.** Same source,
photographed through the same camera calibration:

| state | `VDS_HSYNC_RST` | picture, panel columns |
|---|---|---|
| after a recovery | 1915 | 133..1065 |
| after a bypass round trip | 1915 | 85..1018 |
| after the next recovery | 1922 | 133..1065 |

The raster does not determine the framing: 1915 gives both, and 1922 gives the
correct one. A full dump either side of the last row differs in five config
fields out of 608 -- `VDS_HSYNC_RST`, `VDS_HB_ST`, `VDS_DIS_HB_ST`,
`VDS_HSCALE`, `SP_H_CST_SP` -- and **every register that places the picture
relative to output sync is identical**: `VDS_HS_ST` 0, `VDS_HS_SP` 32,
`VDS_DIS_HB_SP` 110, with the picture measuring the same width both times.

So the scaler emits the picture at the same offset from its own sync in both
states and the panel shows it 48 columns apart. What is left is outside the 608
registers: the display clock the Si5351 is actually running, which no dump
carries. That is the next thing to instrument, and per
`the-encoder-reframes-the-output.md` it needs an intervention rather than another
absence of difference.
