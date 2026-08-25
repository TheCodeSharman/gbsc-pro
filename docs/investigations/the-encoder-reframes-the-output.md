# Where the scaler puts the picture is not where the television shows it

The MS9288A samples the analog output and generates its own HDMI timing. Moving
the picture inside the scaler's raster does not move it on the screen.

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

The engine is capped at `OutputMode::EngineCeilingHz`, 108 MHz, so at 1125 lines
it can only emit a 1916 px line where CEA-861 1080p50 is 2640 px at 148.5 MHz —
73% of standard width. At 1066 lines it emits 2022 px against VESA 1280x1024's
1688, which is *wider* than standard. The encoder has to reinterpret in both
cases and evidently does better with the second.

The board cannot emit standard 1080p50 timing at all: 2640 x 1125 x 50 is
148.5 MHz, above even the 129.6 MHz `WorkingCeilingHz` the part is measured to
run at.

## The untried experiment

129.6 MHz is measured working and sharp (`docs/tv5725-chip.md`, the 2026-08-11
sweep). Raising `EngineCeilingHz` to it would make the 1080p raster about
2301 px rather than 1916 — a fifth closer to standard. Whether that reduces the
bar is unknown; the picture is the only instrument that can say.

The usability argument that originally held `EngineCeilingHz` at 108 no longer
applies: it rested on a `scaleMin` of 500 leaving no horizontal zoom travel at
the wider raster, and `Scale::Min` has since become `Unity / maxMagnification`.

## The general rule this is an instance of

Do not read the scaler's raster as what the television sees. `VDS_HSYNC_RST` and
the output windows describe the scaler's own timing; the HDMI mode the display
locks to is the encoder's, and the encoder is on no MCU's I²C bus.
