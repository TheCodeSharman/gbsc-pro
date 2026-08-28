# What selects the DAC route in bypass

The two bypass switches were expected to differ in the DAC route they pick, and
to pick it from the source's colour space — `DAC_RGBS_BYPS2DAC` with the matrix
on for a component source, `DAC_RGBS_ADC2DAC` with it off for RGB. That model is
wrong in both halves, and what is actually there is a defect.

## Neither switch branches on colour space for the route

There is one writer of each bit:

| | written by | conditional on |
|---|---|---|
| `DAC_RGBS_ADC2DAC` = 1 | `Chip::enterBypassRgbhv()` | nothing |
| `DAC_RGBS_BYPS2DAC` = 1 | `setOutModeHdBypass()` | nothing |

Colour space reaches bypass through the matrix bypasses instead —
`HD_MATRIX_BYPS`, `HD_DYN_BYPS` and `DEC_MATRIX_BYPS`, which the HD switch does
branch on. The route is a property of which switch ran, not of the source.

## Both bits set at once, and it reaches the picture

Neither switch cleared the other's bit, and only `Chip::init()` and
`Chip::routeToScaler()` did. So a crossing left both set. It is one-directional,
and the mechanism is which switch runs the bring-up: `setOutModeHdBypass()` calls
`doPostPresetLoadSteps()`, which clears the pair before selecting;
`bypassModeSwitch_RGBHV()` does not, so it inherits.

Measured with the source at 800x600 — a mode the display accepts as a
passthrough, which is what makes bypass judgeable at all:

| | `ADC2DAC` | `BYPS2DAC` | picture |
|---|---|---|---|
| RGBHV bypass, fresh | 1 | 0 | correct contrast and saturation |
| after entering HD bypass and returning | 1 | 1 | black lifted, greys washed toward white, colour bars desaturated |
| the same state, `BYPS2DAC` cleared by hand | 1 | 0 | correct again |

The two paths sum at the DACs. **Nothing but the picture sees it**: every
register outside s0_4b reads correct, `STATUS_SYNC_PROC_*` is healthy, and the
sync path bits say bypass exactly as they should.

Two traps came with the measurement:

- **The bench's own 320x256 source shows NO SIGNAL in bypass**, because bypass
  passes the source's timing straight to the encoder and the display will not
  take 15 kHz. That is not the DAC bits and reading it as them wastes the
  experiment. Use a mode the display accepts.
- **Absolute brightness is not a measurement.** What settles this is two frames
  photographed seconds apart at one camera position differing in one register
  bit, with the corrected state shot again afterwards as the control.

## What a trace cannot tell you here

A write trace records whole bytes, and both bits live in s0_4b, which every
writer reaches by read-modify-write. So a trace of the RGBHV switch taken after
an HD-bypass trace shows `s0_4b = 6` — both bits — even though the switch writes
only one of them. **The final byte in a trace is not a statement of what that
code chose** for bits it does not write. Read the source for which bits a
function sets, and the trace for the values it sets them to.

## What the switches actually differ in

From the committed fixtures, the same source through each switch:

| | HD bypass | RGBHV bypass |
|---|---|---|
| addresses written | 432 | 85 |
| written by that switch alone | 350 | 3 |
| written by both, same value | 67 | 67 |
| written by both, different value | 15 | 15 |

The 350 are `doPostPresetLoadSteps()`, which the HD switch calls and the RGBHV
switch does not. So the two are not two spellings of one sequence: the overlap
is about 85 addresses and two thirds of those already agree.
