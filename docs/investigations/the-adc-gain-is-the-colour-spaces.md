# The ADC gain is the colour space's, and the preset path's value was wrong

`ADC_RGCTRL`/`GGCTRL`/`BGCTRL` had three writers and the last one won:
`ColourSpace::applyYuv()` wrote 0x33, `setAdcParametersGainAndOffset()` wrote
0x7B over it later in the same preset load, and the auto-gain loop overrode both
when it was on. A boot that reached a picture without a preset load kept 0x33
and nobody knew which was right.

**0x33 is right for component and 0x7B costs 7% of full scale.** Measured on
the emitted frame.

## The sweep

A-B-A-B on the Wii at 480i, three frames per point, scored inside the borders
`hdmi_capture` finds. The source is live content, so the repeat is the control:
0x33 read 97.72 and 97.62, 0x7B read 82.11 and 82.18, and the gap between them
is two orders of magnitude wider than the drift.

Swept:

```
gain    mean    p99.5   max     any channel >= 254
0x18    95.15   239.0   250.5   0.24%
0x20    94.20   239.0   250.0   0.27%
0x28    93.52   239.0   249.7   0.25%
0x2c    93.32   239.0   249.7   0.03%
0x33    97.40   239.0   249.3   0.00%
0x3a    96.69   239.0   248.2   0.00%
0x48    94.72   238.7   248.3   0.00%
0x7b    82.22   222.3   232.2   0.00%
```

**A higher register value is LESS gain**, which `runAutoGain()` corroborates
independently: it raises `ADC_GGCTRL` by 2 whenever the test bus reads
saturated. So the clipping knee is between 0x2C and 0x33, and 0x33 is the first
value that clips nothing. 0x7B is far past it — the peak falls from 239 to 222
and the mean from 97 to 82, which is a dimmer, lower-contrast picture with a
fourteenth of the range thrown away.

## On RGB the register does nothing

The same sweep on the RISC PC at 320x256@50, whose card is static, so this one
needs no control at all — and the three 0x7B readings came back 81.92, 81.92,
81.92.

```
gain    mean    p99.5   max     any channel >= 254
0x33    82.32   240.0   255.0   0.95%
0x50    82.16   240.0   255.0   0.97%
0x60    82.07   240.0   255.0   0.98%
0x6c    82.01   240.0   255.0   0.98%
0x74    81.97   240.0   255.0   0.98%
0x7b    81.92   240.0   255.0   0.98%
0x88    81.83   240.0   255.0   0.98%
0x98    81.71   240.0   255.0   0.96%
```

Across a threefold range of the register the mean moves 0.6 of a grey level and
the peak, the percentile and the clipped fraction do not move at all. Were the
ADC clipping, raising the register would reduce the clipped fraction; it does
not. The stage is not in the RGB path.

## What follows

`ColourSpace` states the gain for both halves — `ComponentGain` 0x33 and
`RgbGain` 0x7B — and `setAdcParametersGainAndOffset()` is gone, its offset write
inlined at both call sites. That leaves two writers where there were three, and
the remaining pair do not disagree: the colour space seeds, and the auto-gain
loop searches from the seed when the user has it on.

**The RGB value is stated even though it is inert.** It is what the board has
always run, so stating it changes nothing measurable — and without it a source
arriving on RGB after a component one inherits 0x33, which is the omission
hazard rather than a decision.

Measured after the change: `vga` boots at spread 109.5 with the gain reading
0x7B, `ypbpr` boots at 92.2 with 0x33, and an input switch moves it both ways.
