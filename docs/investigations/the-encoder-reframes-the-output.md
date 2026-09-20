# Where the scaler puts the picture is not where the television shows it

The MS9288A samples the analog output and generates its own HDMI timing. Moving
the picture inside the scaler's raster does not move it on the screen.

**The title overstates it, and the "why the two modes differ" section below is
superseded.** The encoder does not choose a frame of its own: it starts sampling
where our blanking ends and resamples the line into the standard's active pixel
count, both measured. What made 1080p and 1024p differ is that the emitted
active window was a front porch TIME rather than the standard's fraction of the
line, so it overran by a different amount in each mode.
[`the-active-window-is-a-fraction-of-the-line.md`](the-active-window-is-a-fraction-of-the-line.md)
carries the measurements and the fix.

## The measurement

Bench RiscPC at 320x256@50, output 1080p, a black bar down the left of the
television. The scaler's own numbers say the picture fills its window:

| | capture | produced | display window | raster | left margin | right |
|---|---|---|---|---|---|---|
| 1024p | 890 units | 1856 px | 115..1969 | 2022x1066 | 115 | 53 |
| 1080p | 890 units | 1739 px | 112..1849 | 1916x1125 | 112 | 67 |

Identical capture, identical proportions, and the picture sits at almost the
same fraction of the line in both — 5.7% and 5.8%. The television shows a bar
at 1080p and none at 1024p.

**The probe.** At 1080p the whole output window was moved 60 px left by hand —
`VDS_DIS_HB_SP`/`VDS_DIS_HB_ST` `112..1849` → `52..1787`, `VDS_HB_ST` with them.
The registers were read back at `52..1787` and still read that at the moment the
photograph was taken, so the engine had not rewritten them. **The bar did not
move.**

So the bar is not the framing, not the pan and zoom, and not where the solver
places the picture. It is downstream of everything the scaler controls.

## Why the two modes differ

**Superseded.** The reading was that the encoder reinterprets a non-standard
line and happens to do better with one mode than the other. What it actually
does is fixed and simple, and the asymmetry was ours: the active window was
emitted as the raster less a front porch duration, which is a different fraction
of the line in every mode, where the encoder carries the standard's fraction.
[`the-active-window-is-a-fraction-of-the-line.md`](the-active-window-is-a-fraction-of-the-line.md).

The ratio that entry turns on is still the one stated here. The engine is capped
at `OutputMode::EngineCeilingHz`, 108 MHz, so at 1125 lines it emits a 1920 px
line where CEA-861 1080p50 is 2640 px at 148.5 MHz, and at 1066 lines it emits
2026 px against VESA 1280x1024's 1688, which is *wider* than standard.

The board cannot emit standard 1080p50 timing at all: 2640 x 1125 x 50 is
148.5 MHz, above even the 129.6 MHz `WorkingCeilingHz` the part is measured to
run at.

## The untried experiment

129.6 MHz is measured working and sharp (`docs/tv5725-chip.md`, the 2026-08-11
sweep). Raising `EngineCeilingHz` to it would make the 1080p raster about
2301 px rather than 1916 — a fifth closer to standard. Whether that reduces the
bar is unknown; the picture is the only instrument that can say.

The usability argument that originally held `EngineCeilingHz` at 108 no longer
applies: it rested on a scale floor of 500 leaving no horizontal zoom travel at
the wider raster, and the floor the control stops at is `Axis::minimum
Capture()` now, which follows the raster's own room.

## The general rule this is an instance of

Do not read the scaler's raster as what the television sees. `VDS_HSYNC_RST` and
the output windows describe the scaler's own timing; the HDMI mode the display
locks to is the encoder's, and the encoder is on no MCU's I²C bus.
