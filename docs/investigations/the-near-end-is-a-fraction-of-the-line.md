# The near end of the active window is a fraction of the line, not a duration

`OutputMode::solve()` places the active window's start at the standard's sync
and back porch converted from the standard's clock to ours — durations. The sink
opens its window at the standard's blanking as a **fraction** of whatever line it
receives. The two agree at one raster and part company either side of it, and the
error changes sign, which is why no value of `TransmittedWindowDelayPx` held the
window and why a per-output, per-field-rate table was proposed instead.

The display clock is pinned near `EngineCeilingHz` on every raster, so a duration
converts to very nearly the same sample count however long the line is: our start
was **161** at a 1916-unit raster and **161** at a 1590-unit one. The sink's start
moved 27 samples between the same two.

## The residual is one constant

Six openings, read off the USB HDMI capture with the aperture held away from the
opening, scored against `T x (syncPx + backPorchPx) / totalPx`:

| output | T | field Hz | opens | fraction | residual |
|---|---|---|---|---|---|
| 1080p | 1590 | 60.35 | 145.00 | 138.76 | **+6.24** |
| 1080p | 1600 | 60.02 | 145.19 | 139.64 | **+5.55** |
| 1080p | 1602 | 59.94 | 146.05 | 139.81 | **+6.24** |
| 1080p | 1916 | 50.08 | 173.78 | 167.21 | **+6.57** |
| 1080p | 1916 | 50.08 | 172.50 | 167.21 | **+5.29** |
| 720p | 2402 | 59.94 | 384.70 | 378.50 | **+6.20** |

Two inputs, two output standards, both field rates, rasters spanning 1590 to
2402. Mean 6.01, spread 1.28. `TransmittedWindowDelayPx` is that residual.

**The field-rate band is what the DURATION base produces.** Scored against it the
same six readings give ~6, ~7, ~6 and ~35, because the base sits at 139 samples
whatever the raster: a 50 Hz line is 316 units longer, so the fraction grows by 27
and the fixed base reports that growth as a jump. There is no band to tabulate.

## What it fixes, measured

On `vga` at 320x256@50 into 1080p, raster 1916, with `VDS_Y_OFST` lifting the
picture so the source's own border cannot be counted as ours:

| edge | duration base | fraction base |
|---|---|---|
| right | **14 columns ours** | **0** |
| left | 0 | 2 |
| top, bottom | 0, 0 | 0, 0 |

On 800x600@60 into 1080p, raster 1592, it is 4 columns before and 4 after, and
the aperture solves to 145 against the 145 the sink's window was measured to
start at.

The translation was validated as a pure register change first, by moving all four
window registers together and dropping and driving `PAD_SYNC_OUT_ENZ` at each
step so the sink placed its window for the state under test:

| delta | aperture | left | right |
|---|---|---|---|
| +6 | 167..1837 | 0 | 8 |
| +10 | 171..1841 | 0 | 3 |
| **+12** | **173..1843** | **1** | **1** |
| +14 | 175..1845 | 3 | 0 |

The engine now solves 174, one unit past the measured optimum, which is the
aperture ceiling onto the unit the write filled.

## Why the sink looked like it followed our window

On a raster whose own position falls **before** our aperture the sink takes the
first non-black content it meets at the aperture's edge instead, so moving our
window moves its window with it and the margin does not respond. That is the 60 Hz
case, and it is why a translation there is a no-op rather than a regression:
measured over −28 to +8 units, the right margin stayed 0 and the left 1 to 2
throughout. Walking far enough left eventually reveals its own position, which is
what located the 1590 raster's window at 145..1530.

A measurement taken with picture hard against the aperture's edge therefore
reports the aperture back, which is the confound that produced the four readings
of ~20 in
[the-transmitted-window-opens-late.md](the-transmitted-window-opens-late.md).

## The divider moves with it

`VideoPath::dividerCeilingForOutput()` takes the delay back off `activeStop` to
recover the published raster's far edge. On the duration base that left 1815 of a
1920-unit raster where the standard states `1920 x 2112 / 2200 = 1843.6`; on the
fraction it is 1842. The ceiling was therefore holding the divider under what the
raster can show, and 480p rises 1804 → 1852 with 576p at 1850 — two values a test
named for their differing had pinned equal.

## What is not settled

The source's own leading blanking is now **visible** at the left where the sink
used to clip it, 18 columns on the bench 320x256@50. Nothing of ours is in it —
the lift raises all but two columns — so it is a framing question about whether a
mode's border belongs in the capture, not about where the window opens.
