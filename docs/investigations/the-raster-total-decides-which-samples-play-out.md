# An even VDS_HSYNC_RST plays out the wrong samples

The output raster total reaches the picture. At one framing, with the capture,
the scale, both windows and the sampling divider all held, stepping
`VDS_HSYNC_RST` by one alternates the picture between clean and corrupt.

`OutputMode::horizontalTotalFor()` therefore rounds its floored total up to the
next even value, because `VideoPath` writes `horizontalTotal - 1` into the
register.

## What was measured

Bench RiscPC at 320x256@50 on `vga`, output 1024p, engine solve untouched, one
register written by hand between shots:

| `VDS_HSYNC_RST` | | picture |
|---|---|---|
| 2025 | odd | clean |
| 2024 | even | **corrupt** |
| 2023 | odd | clean |
| 2022 | even | **corrupt** |
| 2021 | odd | clean |
| 2020 | even | **corrupt** |

Six consecutive values, no exceptions. The corrupt verdict is the set
`docs/known-issues.md` lists as one artefact -- the wedge's bars splitting into
hairlines and wandering in width, the yellow curve and the grey curve before the
wedge going ragged, black bars through the label text.

## How it was found, and why nothing else can account for it

A bisect over the eight commits of one session. `cade823ea` and `5a521465d`
clean, `44ad4e433` corrupt, and the registers either side of that boundary are
identical in everything the geometry engine solves:

| build | `VDS_HSCALE` | capture | `VDS_HB_SP` | `VDS_HB_ST` | `PLLAD_MD` | `VDS_HSYNC_RST` | picture |
|---|---|---|---|---|---|---|---|
| `5a521465d` | 460 | 689 | 249 | 1890 | 2200 | **2025** | clean |
| `44ad4e433` | 460 | 689 | 249 | 1890 | 2200 | **2022** | corrupt |

Same capture, same scale, same produced width, same memory window, same
divider. `44ad4e433` quantises the source key's field rate to hundredths of a
hertz rather than whole hertz, which is correct and stays -- it moved the solved
total by three pixels, and three pixels was enough.

**So `produced` is refuted as the quantity here, and so is the memory window's
width.** Both are identical across that pair, and the memory window is odd in
both. Whatever the mechanism, it has a term on the output raster that neither
covers.

Writing 2025 back into the corrupt build by hand, with nothing else touched and
no reflash, cleans the picture completely. That is the same experiment as the
creep above and it is what makes the bisect a cause rather than a correlation.

## Which direction the bias goes, and what it costs

**Up.** The total is a clock budget floored -- `clock / (fieldRate x frameLines)`
-- so raising it by one asks for a line one pixel longer than the budget affords.
The frame time lock steers the display clock continuously, so a pixel of budget
is not a quantity that has to be exact; a pixel of raster is picture, and the
alternative loses it. Every mode gains at most one pixel of raster.

## What is not established

**It has been measured at one framing, on one output mode, on one source.** The
parity held across six consecutive values there, which is strong for that state
and says nothing about whether the parity is the same sense at another
magnification. The cross-mode comparison cannot settle it either, because the
scale, the capture and the divider all move between modes: at the framing the
session opened on, 480p ran an even total and looked clean while 576p ran an odd
one and looked notched.

**Nor is the mechanism known.** It is the same shape as the memory window's
width parity -- a one-unit change to an integer flipping which samples play out,
with no account of why -- and the same caution applies: anything that later
explains it should be expected to replace this rather than build on it.

The measurement that would settle the sense is a zoom sweep at a fixed total
repeated at the total plus one, on more than one output mode. Two columns, the
same shape the width rule was separated from the register with.
