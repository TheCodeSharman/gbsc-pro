# What the ADC calibration borrows, and what it owes back

`calibrateAdcOffset()` runs once from `setup()`, behind the `calibrate-adc`
preference, and it borrows the chip to do its work: it bypasses the colour
matrix, switches the ADC's decimator test path on, parks the input on 2, zeroes
the decimators, flattens fifteen sync-processor fields, drives the test bus and
walks each channel's offset register until the reading settles. It ends by
holding the three offsets it found.

It restores one thing, and that is the correct number.

## Only one borrowed field has no owner outside the preset path

| borrowed | left at | who puts it back |
|---|---|---|
| `DEC_TEST_ENABLE` | 1 | **nothing but `applyStoredAdcGain()`**, inside the preset path |
| `DEC_MATRIX_BYPS` | 1 | `ColourSpace`, per input selection |
| `ADC_RYSEL_R/G/B` | 0 | `ColourSpace`, per input selection |
| `SP_CLAMP_MANUAL` | 1 | `SyncProcessor`, through the acquisition layer's clamp |
| `PLL648_CONTROL_01` | 0xA5 | `DisplayClock`, when it hands the clock over |
| `ADC_CLK_ICLK1X` / `2X`, `DEC1_BYPS` | 0 | `Adc::applySampleRate()`, every solve |
| `ADC_TR_RSEL`, `ADC_FLTR`, `ADC_CLK_PA` | 0 / 3 / 2 | `Adc::init()`, every bring-up |
| gain, offset | 0x7F, 0x7F/0x3D/0x7F | `setAdcParametersGainAndOffset()` and `Adc::applyHeldOffset()` |

So `calibrateAdcOffset()` switches the measurement path off when it has
finished, and writes nothing else back. **A hand-written inverse for the rest
would be a second owner of eight fields that already have one**, which is the
defect that costs most here: two writers with the same value today diverge
silently the first time one of them changes.

`DEC_TEST_SEL` is left at 3 and is deliberately not restored. It selects which
channel the decimator test path reports and is inert while `DEC_TEST_ENABLE` is
0, so giving it a resting value would be inventing a constant the datasheet does
not ask for.

## The measurement

Two settled dumps on the bench unit, `vga` at 320x256@50, one boot with
`calibrate-adc` on and one with it off, 608 config registers each. They differ
in five fields:

```
PA_SP_S              1 -> 0
VDS_HSYNC_RST     1915 -> 1917
VDS_HB_ST         1622 -> 1624
VDS_DIS_HB_ST     1622 -> 1624
VDS_HSCALE         562 -> 561
```

The searched sampling phase, and the raster solved from a field rate that
differs by a fraction of a hertz between acquisitions. **Nothing the calibration
touches appears**, which is what says the other eight rows above are honestly
owned rather than merely plausible.

Read directly after a boot on each input, the colour-space fields follow the
selection rather than the calibration: `ADC_RYSEL_R` 0 with `DEC_MATRIX_BYPS` 0
on `vga`, 1 and 1 on `ypbpr`, and `ADC_TR_RSEL` 2 -- the bring-up's value, not
the calibration's 0 -- on both.

## Why it mattered before anything broke

The diff above is taken with `applyPresets()` still in place, so it cannot show
the dependency; it can only show that the two boots agree. The dependency is
structural: `applyStoredAdcGain()` is the only other writer of
`DEC_TEST_ENABLE`, it lives in the preset path, and that path is being
dismantled. A boot would then run the ADC's measurement path for its whole life
with every configuration register reading correct.

`test_adc_calibration_restores.py` states the invariant the preset path
currently supplies -- the measurement path is off unless the auto-gain feature
asked for it -- so removing that path cannot take the invariant with it
silently. It passes against both.

**There is no host test, and no failing one was available.** The trigger is a
sketch function with no runtime route: it runs only from `setup()`, and the
preset load clears the field before anything can observe it.
