# The clamp window is placed in the wrong units

`SP_CS_CLP_ST` and `SP_CS_CLP_SP` say where in the line the black level is
sampled. They land inside the hsync pulse rather than the back porch, on both
sync paths, and the arithmetic says why.

## The measurement

Wii at 480p on `ypbpr`, passed through, sync on green:

```
line          1124 ADC samples    (STATUS_SYNC_PROC_HTOTAL, = PLLAD_MD)
hsync pulse   0 .. 84             (STATUS_SYNC_PROC_HLOW_LEN, 7.5% of the line)
clamp window  20 .. 39
```

The window is wholly inside the pulse. `acquireClampWindow()`'s csync arm
reproduces both numbers exactly from `HPERIOD_IF`, which reads 214 for this
mode:

    start = 1 + 214 x 0.089 = 20
    stop  = 2 + 214 x 0.174 = 39

## The units do not match the register

`clampLineLength()` measures the line in `HPERIOD_IF` on the csync path and in
`STATUS_SYNC_PROC_HTOTAL` on the separate path, and the fractions are fitted per
path to suit. **The register counts ADC samples on both**, and `HPERIOD_IF`
counts periods of the chip's 27 MHz: 214 against 1124 here, a factor of 5.25. So
the csync arm writes a window about five times too early, which is inside the
pulse for any source whose sync is under a fifth of the line.

That settles a question `docs/scaler-geometry-model.md` carries as open --
whether these two are in IF units or ADC samples, "small enough to be either and
misplaced under both readings". They are ADC samples, and they are misplaced
because the value handed to them was measured in the other unit.

## What it costs

**On sync on green the green channel carries the sync**, so a clamp inside the
pulse references G against the sync tip while Pb and Pr reference their true
blanking level. The channels then sit at different offsets, which is a colour
cast, and the channel that is off is green. Observed as a slight green in the
grey of the Wii menu.

On a separate-sync RGB source nothing carries sync on a colour channel, so the
same misplacement costs a black level taken from inside the pulse on all three
equally -- a lift rather than a cast, which is far harder to see and is why this
has survived.

## The `0x60` in `updateClampPosition()` is a patch over it

    if (inputIsYpBpR && isHdBypassChannel() && sourceLowLineRate())
        offset = 0x60;

Ninety-six ADC samples later, applied to a component source on the bypass
channel at a low line rate. That is the correction the unit conversion should
have made, hand-fitted for the one case someone looked at, and withheld from
480p because the gate asks the line rate.

## What it should be instead

Neither a fraction nor an offset: the pulse is MEASURED.
`STATUS_SYNC_PROC_HLOW_LEN` gives its width in ADC samples, which is the unit
the register wants, so the window can start after the pulse ends and stop before
active video begins without a constant per path or per standard. That is the
same move as every other one on `../video-source-acquisition.md` -- derive it
from what is measured rather than choose it from a classification.

Parked behind that page's steps 10 and 12: placing it correctly is worth little
while the surrounding `apply*` calls still dispatch on a standard byte.
