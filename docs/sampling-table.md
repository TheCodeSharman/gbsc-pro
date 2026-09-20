# What divider each input mode lands on

**Regenerate it, never edit it.** The table below is the firmware's own
arithmetic, printed by the suite that owns it:

```sh
make -C test source-measurement && ./test/output/test_source_measurement --table
```

## The rule

The part sees sync edges, not pixels, so a source's pixel clock is unknowable
and 320x256 and 640x256 are indistinguishable to it. `PLLAD_MD` is therefore a
function of the line rate and the scan mode alone, and the resolution names
below are labels for a line rate rather than inputs to the choice.

> **The kept count must exceed the source's pixels. Nyquist is best effort.**

The **kept** count is what the decimator passes into the pipeline — `PLLAD_MD`
undoubled, and **half of it** where the line is doubled. It is the only thing
that carries the source's pixels, so it is the objective.

The **converted** rate is what the ADC digitises, and it is where aliasing is
decided. The ADC converts `ratio` samples for every one the decimator passes,
and a doubled line's own decimation is a second factor of two:

```
converted = kept x ratio x samplesPerUnit
```

```
output 1080p, showable 1120, oversampling asked for 4
kept >= pixels carries the picture; converted >= 2 x pixels clears Nyquist

source                 pixels line Hz  2x?   4x div bound   2x div bound   1x div bound   PLLAD_MD over    kept  /px  converted  /2px
240p 60 (NTSC rate)       454   15704  yes  *2496    row   4012  field   4012  field       2200   4x    1100 2.42*       8800  9.69*
288p 50 (PAL rate)        512   15624  yes  *2508    row   4012  field   4012  field       2200   4x    1100 2.15*       8800  8.59*
1056x256 50              1536   15624  yes  *2508    row   4012  field   4012  field       2200   4x    1100 0.72        8800  2.86*
480i 60, one field        858   15764  yes  *2486    row   4012  field   4012  field       2200   4x    1100 1.28*       8800  5.13*
576i 50, one field        864   15650  yes  *2502    row   4012  field   4012  field       2200   4x    1100 1.27*       8800  5.09*
480p 60                   858   31468   no   1244    row  *2046 counter   2046 counter       2046   2x    2046 2.38*       4092  2.38*
576p 50                   864   31250   no   1252    row  *2046 counter   2046 counter       2046   2x    2046 2.37*       4092  2.37*
640x480 60                800   31468   no   1244    row  *2046 counter   2046 counter       2046   2x    2046 2.56*       4092  2.56*
640x480 72                832   37861   no   1034    row  *2046 counter   2046 counter       2046   2x    2046 2.46*       4092  2.46*
640x480 75                840   37500   no   1044    row  *2046 counter   2046 counter       2046   2x    2046 2.44*       4092  2.44*
800x600 56                936   35156   no   1114    row  *2046 counter   2046 counter       2046   2x    2046 2.19*       4092  2.19*
800x600 60               1056   37880   no   1032    row  *2046 counter   2046 counter       2046   2x    2046 1.94*       4092  1.94*
800x600 72               1040   48078   no    814    row   1628    row  *2046 counter       2046   1x    2046 1.97*       2046  0.98 
800x600 75               1056   46875   no    834    row   1670    row  *2046 counter       2046   1x    2046 1.94*       2046  0.97 
1024x768 60              1344   48360   no    810    row   1620    row  *2046 counter       2046   1x    2046 1.52*       2046  0.76 
1024x768 70              1328   56476   no    692    row   1386    row  *2046 counter       2046   1x    2046 1.54*       2046  0.77 
1024x768 75              1312   60024   no    652    row   1304    row  *2046 counter       2046   1x    2046 1.56*       2046  0.78 
1280x1024 60             1688   63981   no    612    row   1224    row  *2046 counter       2046   1x    2046 1.21*       2046  0.61 
1280x720 60              1650   45000   no    870    row   1740    row  *2046 counter       2046   1x    2046 1.24*       2046  0.62 
1920x1080 60             2200   67500   no    580    row   1160    row  *2046 counter       2046   1x    2046 0.93        2046  0.47 
```

## What it says

**Every undoubled source lands on the counter's wall**, and two times is kept
free wherever the row still reaches it — through 800x600@60. Above that the
ratio would cost samples and is declined.

**Every mode meets the kept condition except two**, and they fail for different
reasons:

| | kept / pixels | why |
|---|---|---|
| 1920x1080 60 | 0.91 | the counter's 2047 against 2200 pixels — nothing on this part carries it |
| 1056x256 50 | 0.72 | doubled, so the kept count is half the divider, and the cap below holds it there |

Pass-through is not an escape from the first: `HD_HSYNC_RST` is the same eleven
bits, so that route stops at 2039.
[`investigations/the-line-counters-are-eleven-bits-measured.md`](investigations/the-line-counters-are-eleven-bits-measured.md)

Nyquist is met wherever the ratio was free, and missed above 800x600@60 — which
is the best-effort half of the rule doing what it says.

## What the doubling column depends on

`2x?` is the line doubler, and it is a property of the **output** as much as the
source: a doubled frame with no room to be shown is only cropped. The table is
taken at 1080p out, which shows 1120 lines. A shorter output turns the doubler
off for sources near the boundary, and both the divider and the kept count move
with it.

## The cap on a doubled line

`SamplingClock::DoubledLineSampleLimit` holds a doubled line off the divider the
counter would otherwise allow — 4012 on a 15.6 kHz line. The capture path stops
writing video at ADC sample 2236..2256 of a doubled line and the rest of the
line comes back dark green, destroying any picture that reaches it, so the line
is kept shorter than that and the whole of it arrives.

It is measured only on a doubled line because only there can the divider reach
it: undoubled lines of 1217, 1447 and 1561 IF units show no band anywhere, which
is what rules out a bound expressed in IF units.
1056x256's shortfall above is what it costs.
[`investigations/tail-green.md`](investigations/tail-green.md)

## See also

- [`capture-limits.md`](capture-limits.md) — the three ceilings, and why there is
  no capture bound
- [`investigations/adc-pll-lock-range.md`](investigations/adc-pll-lock-range.md)
  — what the ADC PLL will lock to
