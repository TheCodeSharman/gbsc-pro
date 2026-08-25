# The scaler's output width limit

**Root cause found and a fix proven on the bench.** The scaler produces at most
about **2240 output pixels per line**. Ask for more and the line wraps: the
excess reappears at the start of the line as compressed live picture, which
reads as edge corruption.

The engine asks for more whenever the output mode has few frame lines, because
the raster width is derived from the frame height and nothing caps it.

## Why a short frame produces a wide line

`OutputMode` carries no horizontal dimension. The raster is

```
horizontalTotal = clock / (fieldRate x frameLines)
```

taken at the highest clock at or below `EngineCeilingHz` (108 MHz), so **the
shorter the frame, the wider the line**. Measured on the bench, RiscPC
320x256@50, capture 890 units:

| preset | frame lines | raster | display window | magnification | result |
|---|---|---|---|---|---|
| 480p | 625 | 3450 | **3241** | x3.64 | striping across the whole picture |
| 720p | 750 | 2875 | **2565** | x2.88 | bands both edges |
| 960p | 1000 | 2156 | 1937 | x2.18 | clean |
| 1024p | 1066 | 2022 | 1854 | x2.08 | clean |
| 1080p | 1125 | 1916 | 1737 | x1.95 | clean |

The break falls between a 1937-pixel window and a 2565-pixel one.

## Magnification is not the variable

The obvious reading — that the corruption tracks `VDS_HSCALE` — is **refuted**.
Zooming in on 1080p raises the magnification with the raster fixed:

| | window | magnification | result |
|---|---|---|---|
| 1080p zoomed in | 1712 | **x2.92** | clean |
| 1080p zoomed in | 1707 | **x3.13** | clean |
| 720p as solved | 2565 | x2.88 | **broken** |

1080p at x2.92 is clean while 720p at x2.88 is not, so what separates them is the
width of the line, not how hard the picture is magnified.

## The threshold

Measured with matched pairs — the window set to exactly the width the scale
produces, so nothing is unfilled by construction, centred in the 720p raster:

| window = produced | `VDS_HSCALE` | result |
|---|---|---|
| 2000 | 456 | clean |
| 2200 | 414 | clean |
| **2225** | 410 | **clean** |
| **2250** | 405 | **a band appears at the left** |
| 2300 | 396 | band |
| 2400 | 380 | bands, wider |

**Clean at 2225, first band at 2250.**

## What the bands are

Compressed live picture, not stale memory: the source's text cursor blinks inside
them. The far-left band carries the card's *right-hand* colours, which is the
signature of a line wrapping rather than of a window open past the end of the
data. Where the window is much wider than the produced picture the unfilled part
shows green instead — `Y=U=V=0`, memory never written, as `capture-limits.md`
describes — so both appear depending on how far past the limit the request goes.

## The fix, proven

Lower the display clock for short frames so the line stays under the limit. 720p
rebuilt by hand at **81 MHz** — raster 2160, window 1927, `VDS_HSCALE` 473,
field rate unchanged at 50 Hz because 81e6 / 2160 / 750 = 50 — **is clean**.

The clocks available are 40.5, 54, 64.8, 81, 108, 129.6 and 162 MHz. What each
gives at 50 Hz:

| preset | at 108 MHz | at 81 MHz | at 64.8 MHz |
|---|---|---|---|
| 480p (625) | 3456 ✗ | 2592 ✗ | **2073 ✓** |
| 720p (750) | 2880 ✗ | **2160 ✓** | 1728 |
| 960p (1000) | **2160 ✓** | 1620 | 1296 |
| 1024p (1066) | **2026 ✓** | 1520 | 1216 |
| 1080p (1125) | **1920 ✓** | 1440 | 1152 |

So `clockDividerFor()` needs a second constraint: reject a seed whose resulting
line exceeds the working width, and take the next one down. Today it maximises
the clock against `EngineCeilingHz` alone, which is why the two shortest frames
are broken by construction and the three tallest are fine by luck.

**Verified as shipped.** With the constraint in `clockDividerFor()`, the engine
picks 81 MHz for 720p and 64.8 MHz for 480p on its own:

| preset | raster before | raster after | window after | result |
|---|---|---|---|---|
| 480p | 3450 | **2070** | 1920 | clean |
| 720p | 2875 | **2156** | 1916 | clean |
| 1024p | 2022 | 2022 | 1854 | unchanged, clean |

**A lower clock costs horizontal resolution**, which is the trade
`EngineCeilingHz` already documents. Being under the limit is not optional.

## Eliminated along the way

- **The television's own scaling** — identical with the set's aspect on `full`.
- **The playback fetch and stride** — `PB_FETCH_NUM` 223 and `PB_CAP_OFFSET` 282
  are identical in the broken and clean modes.
- **Unwritten memory as the whole story** — the bands carry live video.
- **The output hsync position** — moving the pulse from 0..58 to 62..77 rotates
  the line, picture and bands together, and cures nothing.
- **Magnification** — see above.

## Related

- `src/tv5725/OutputMode.h` — the raster arithmetic and the two ceilings
- `src/tv5725/DisplayClock.cpp` — the seven clock seeds
- [../capture-limits.md](../capture-limits.md) — the input-side write limit, a
  different bound with a similar shape
