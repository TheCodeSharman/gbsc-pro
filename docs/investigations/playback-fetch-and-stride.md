# The frame buffer stage: fetch, stride, and what is read back

`PB_FETCH_NUM` and `PB_CAP_OFFSET` govern how each line is written into the
frame buffer and read out of it. Both were treated as tuning knobs and both are
determined: one by the capture width, one by the line.

## The buffer comes before the scaler

Garbage in the region past the picture **scales with `VDS_HSCALE`**. So it passes
through the scaler, so it is already in memory when the scaler sees it. That is a
direct observation of the data path, and it settles an ordering that no
architectural argument had:

```
ADC -> IF          captures a window; its scaling-down path is inactive, rate registers 0
    -> SDRAM       capture samples, plus whatever was never written
    -> playback    PB_FETCH_NUM per line, advancing by PB_CAP_OFFSET
    -> VDS scaler  VDS_HSCALE, magnifying
    -> output raster
```

**So `PB_FETCH_NUM` is a function of the capture width alone and must not track
`VDS_HSCALE`.** The scaler is downstream; how much the picture is magnified
cannot change how much of it was stored. Setting fetch and stride both to 370 at
a capture of 1008 is far too large — the whole IF line allows 319 at that
divider.

## One fetch unit is four IF units

Both registers are documented "Mapping to 64bits width data bus field". The 4 is
confirmed by the picture rather than by the wording: at a capture of 1260 a
stride of `offset x 2` would overlap successive lines by 560 units and obliterate
the test card.

**The ratio itself has never been measured directly.** `Memory::fetchFor()`
computes `ceil(capture / 4)`, so any check that reads the fetch back and finds it
is a quarter of the capture is confirming its own arithmetic. To measure it,
creep the fetch *down* at a fixed capture until the line fails to finish — the
signature is the start of the picture reappearing at the right — and take
`capture / that value`. Creep past the first failure; everything here comes in
bands.

## The stride is shared, and is sized for the line

`PB_CAP_OFFSET` is a line stride used by **both** the capture and playback sides
— *PlayBack and CAPture*, as in `PB_CAP_BUF_STA_ADDR_A`. Changing it by one with
the capture frozen produces a diagonal, which is a stride mismatch accumulating
down the frame.

It must cover a capture of the whole line, `ceil(lineUnits / RequestsPerLine)`,
not the current framing. The fetch follows the capture, and the capture grows as
the picture zooms **out**; a stride sized for the framing is passed by the fetch
partway through the zoom range, after which lines overlap and each overwrites its
predecessor's tail. On the bench that arrives at a capture of about 920.

Two consequences that read as contradictions until the direction is clear:

- **A stride above the fetch is the wrong sign.** Any excess leaves
  `(stride - fetch) x units` per line that capture never writes and playback
  reads anyway. Upstream's `+4` generates the artefact; `+32` makes it worse.
- **A stride below the fetch does not cure it either.** It hides the gap by
  corrupting it — lines overlap, and the tail is overwritten rather than left
  unwritten.

## Writing the stride flickers, so it is written once

Any write to `PB_CAP_OFFSET` reprograms the playback FIFO mid-readout and
flickers **even when the value written is identical** — the same behaviour the
codebase already records for `PB_FETCH_NUM`. A constant stride is written once at
the first solve; a stride derived per framing is rewritten on every zoom press,
which is what introduces flicker where none was seen before. Sizing it from the
line rather than the framing keeps it still through a zoom.

Freezing the capture around the write does not help: that pauses the write side,
not playback.

## The probe that separates the two sides

`CAP_REQ_FREEZ` (`s4_22` bit 3) freezes the capture side and holds the buffer
static. With a frozen buffer, anything that still moves the picture is playback,
and anything baked into the frozen image was never written. That is the only
clean read-side/write-side split available.

## The colour of an idle read is not settled

Unwritten 4:2:2 decodes to saturated green, which is the arithmetic
[`tail-green.md`](tail-green.md) rests on. But **`PB_ENABLE` = 0 gives a white
screen**, and an all-ones idle through the same 4:2:2 decode would be pink. So
the colour space of that stage is not established, and the zeros are inferred
from the green rather than observed.

## Open

**Combing at the zoom-out extreme.** No value of `PB_CAP_OFFSET` clears it, so it
is not the stride. It is separate from the green band, and it is not established
to be the same artefact as the 493-framing dataset in
[`hscale-tearing-characterisation.md`](hscale-tearing-characterisation.md) —
that identification was made once without evidence and withdrawn.

**Is `VDS_HSCALE` an offset-by-one field?** If 1023 means 1024 then unity is
reachable and `Scale::Unity` is off by one. The chip does use minus-one
encodings: `VDS_VSYNC_RST` holds total minus 1, and six shipped modes ran a line
long because upstream wrote the plain total. The two readings differ by 1 px at
`VDS_HSCALE` 1023 and by 11 px at 300, so **measure `produced` at deep zoom** —
the eleven bench readings behind `produced = capture x 1024 / scale` were all
taken where the difference is 1 to 3 px.

## Measurement discipline this cost

- **The register panel caches `PB_FETCH_NUM`.** A day of stride measurements was
  paired with a fetch that was not in the chip. This is the one-pass rule from
  the other end: not a stale solve, a stale read.
- **The buffer accumulates the session.** Stale content from earlier framings is
  what the garbage past the picture usually is. Moving `PB_CAP_OFFSET` shifts the
  layout so old content does not land in the same place, which is how to see a
  true edge rather than a remembered one.
- **`/getreg` returns empty when hammered.** Space reads about 2 s apart, or use
  `dump_registers.py`, which reads everything in one pass.
- **`regpanel.py` builds its sections at import**, so an edit needs the process
  restarted, not the page refreshed.
- **The magenta is static, not flashing.** The source's border cycles colour and
  the leftover region does not follow it, so whatever occupies it is not being
  written each frame. One observation, and it separates captured content from
  leftovers better than any register.
- **The framing drifts underneath a hand measurement.** `solveRaster()` re-runs
  on a field-rate wobble and moves the raster — 1901 to 1916 between two
  readings — which makes two values look comparable when they are not. Freeze
  automation first.
- **`VDS_HSCALE` at the register panel's cap is not a reachable engine state.**
  At 1023 the picture shrinks to about 1009 px while the display window stays
  sized for the last solve, so most of the screen shows unwritten memory. It is
  an amplifier, not evidence about a value.
- **A hand-set divider is not a measurable state.** Moving `PLLAD_MD` and its
  dependent fields while `VDS_HSCALE`, the fetch and the stride stay solved for
  the previous framing leaves the picture filling 1579 px of a 1744 px window,
  and those numbers cannot be compared with engine-solved ones. To measure at
  another divider, make the firmware choose it.
- **A warm reset can leave the unit wedged** — no signal, dead OSD — where a
  cold boot clears it. The flash's RTS reset restarts only the ESP; the HC32F460
  and MS9288A keep their state through it.

## See also

- [`tail-green.md`](tail-green.md) — the two green bands, and which one is the
  stride
- [`../scaler-geometry-model.md`](../scaler-geometry-model.md) — the arithmetic
  the capture window is solved from
