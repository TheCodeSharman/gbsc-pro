# The picture falls short of the transmitted window, and only at 50 Hz

The emitted frame's last 14 of 1920 columns are black at `X320 Y256 C256 F50`,
at the engine's own framing, with nothing hand-set. This is what that is and
what it is not.

## The instrument

Walk the aperture and watch the emitted frame's last column change state.
`VDS_DIS_HB_ST` and `VDS_HB_ST` move together, automation frozen, and the
question asked is only where the encoder stops carrying our line -- so the
answer is free of the capture, the scale and the placement.

The count of black columns tracks the aperture linearly, 1.15 columns per raster
unit, from 14 at `VDS_DIS_HB_ST` 1829 to none at 1841. That linearity is what
says the black is OUR blanking rather than anything downstream refusing it.

## What it is not

**Not the encoder latching onto image content.** With the aperture held open the
capture was panned 4 units -- 11.1 emitted columns -- and every region of the
frame was asked which shift best explains it:

| region | best shift | runner-up |
|---|---|---|
| picture, columns 1500..1900 | **11** (err 7.8) | 12 (err 112.6) |
| picture, columns 1800..1900 | **11** (err 14.7) | 12 (err 150.6) |
| the strip past the aperture, 1906..1919 | **0** (err 1.7) | 1 (err 870.0) |

The picture moved exactly as far as the pan and the strip did not move at all,
columns 1908..1919 reading byte-identical at both pans. **So the strip carries no
source information**: it is a fixed ramp, 235 falling to about 75, following the
last written column. Opening the aperture recovers nothing.

**Not the aperture.** It closes on the write end, which is where the picture
stops. The picture is what falls short.

**Not the near edge.** Solved from two independent anchors in one frame -- the
card's leftmost drawn pixel at raster 165.4 reading column 6, and the picture's
end at 1829.5 reading column 1905 -- the window is 160.2 .. 1842.7 at 1.1411
columns per unit, against an aperture opening at 160. A reading of 156.6 was
taken and withdrawn: it counted the black at the LEFT as our own blanking where
it is captured SOURCE blanking, which is the confound
`the-transmitted-window-is-latched-from-our-blanking.md` names.

**Not the scan mode.** A reading that it followed line doubling was taken and
withdrawn. It rested on `X640 Y200 C256 F60`, walked DOWNWARD from an aperture
where the fetch returns black -- which reads as a window edge and is not one.

**Not acquisition to acquisition drift.** Six re-locks per mode through a
`PAD_SYNC_OUT_ENZ` toggle, framing untouched and `VDS_DIS_HB_ST` unchanged at
1831 throughout:

```
X320 Y256 C256 F50   black at the right: 14, 14, 14, 15, 14, 15
X640 Y480 C256 F60   black at the right:  0,  0,  0,  0,  0,  0
```

Repeatable to a column. **And the left edge does not move either**, which is the
argument that settles it: a window placed differently at each lock would move
both ends, and the card's left border lands on the same column every time.

## What it is

The field rate. At the engine's own framing, no register touched:

| mode | lines | scan | raster | black at the right |
|---|---|---|---|---|
| X640 Y512 C256 F50 | 534 | flat | 1914 | 6 |
| X320 Y256 C256 F50 | 312 | doubled | 1916 | 15 |
| X640 Y480 C256 F60 | 525 | flat | 1600 | 0 |
| X800 Y600 C256 F60 | 628 | flat | 1592 | 0 |
| X640 Y200 C256 F60 | 262 | doubled | 1604 | 0 |

Both 50 Hz sources short, no 60 Hz one short, on either scan mode. **The
magnitudes do not fit one model**: 6 against 15 on two 50 Hz rasters two units
apart is more than the arithmetic accounts for.

## Where to look

`OutputMode::solve()` computes the window as `activeStart + horizontalTotal x
carriedPx / totalPx`, and `Mode1080p` states `totalPx` 2200 -- **CEA's 60 Hz
total**. It is the one quantity in the solve that a standard states differently
at 50 Hz, and the only modes that fall short are the 50 Hz ones.

The naive substitution goes the wrong way. CEA 1080p50 is 1920 of 2640, 72.7%,
where the measurement wants a window WIDER than 87.27% -- 87.5% on `X640 Y512`
and 87.8% on `X320 Y256`. So the encoder is not switching to the 50 Hz
standard's blanking, and 2640 is not the number to reach for.

**The raster is ours and not CEA's**, which is what makes the fraction an
approximation in the first place: `solve()` renders the standard's sync and back
porch as DURATIONS at whatever clock the line runs, and the active width as a
FRACTION, because our total overruns the standard's by a different factor in
every mode. What no measurement here establishes is what the encoder actually
does to decide the fraction -- it cannot see our blanking, the HBOUT pads being
off and its I2C slave pins unconnected, so it has only the sync it is given.

**A second output resolution is the cheapest next measurement.** `/uc?g` puts the
encoder on 720p, whose CEA totals differ at both field rates, and the same
aperture walk says whether the shortfall tracks the output standard's own
numbers or stays with the source's rate.
