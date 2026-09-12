# The pass-through divider is capped by the channel's horizontal counter, not by the PLL

Scaling costs sampling density where the source has most detail, so pass-through
wants the ADC sampling as densely as the part allows. `Adc::BypassDivider` is
1856 and follows nothing -- the same literal for a 15 kHz line and a 38 kHz one.
The obvious move is to raise it. There is room, but far less than the PLL
suggests, and the bound is in a different block.

## The bound

The HD bypass channel plays out a line of `HD_HSYNC_RST` counts, and
`HdBypass::applyHorizontalFromChannelLine()` sets it to the divider plus a small
guard. **`HD_HSYNC_RST` is ELEVEN bits**, so the played-out line cannot exceed
2047 counts and the divider ceiling at oversample 1 is about 2039 -- roughly 10%
above the 1856 in use.

That is the whole ceiling. The ADC PLL reaches far higher: `PLLAD_MD` is 12 bits
and RD-5725-1.1's crossover rows run to 162 MHz, which at a 37.9 kHz line is a
divider over 4000.

## What made it look like twelve bits

`s1_38` bit 3 is writable and reads back. Written 2120 across the two bytes by
hand, the pair reads 2120.

**Writable is not used.** The counter ignores the bit, and the pair either side
of 2048 says so. `PLLAD_MD` left alone and locked at 1856 throughout, so the only
thing moving is the count:

| `HD_HSYNC_RST` | reads back | picture |
|---|---|---|
| 2040 | 2040 | full screen, correct |
| 2056 | 2056 | **no signal** |

Those two lines differ by 0.8%. Nothing that plays out a line 0.8% longer can
fail while the shorter one works -- but an eleven-bit counter wraps 2056 to 8,
the line collapses and the sink drops the mode. The datasheet's `[10:8]` plus
`[7:0]` is right, and the twelfth bit is a register bit with nothing behind it.

## The trap this leaves

A value past 2047 is stored, reads back correct, and produces a raster nothing
can display. There is no indicator: `STATUS_MISC_PLLAD_LOCK` stays 1 and
`STATUS_SYNC_PROC_HTOTAL` goes on reporting the divider, because both describe
the ADC PLL and neither describes the channel. **The register is not the
counter.**

Seen once as a rolled picture rather than a dead one: 2120 written through an
eleven-bit field lands as 72, which is a line the channel plays out at about
thirty times the source's rate, and the frame arrives displaced rather than
absent.

## Raising it is not a two-register change

`PLLAD_MD` is one of a group. Measured while raising it by hand: asking for 2112
at a 37.9 kHz line is 80.0 MHz, which crosses into `PLLAD_KS`'s top row, and
setting the divider without the row left `STATUS_MISC_PLLAD_LOCK` at 0 with
`STATUS_SYNC_PROC_HTOTAL` reading 1917 against a written 2112 -- the PLL out of
lock and the divider never latched.

`Adc::applySampleRate()` already writes the whole group against a measured rate,
including the crossover row, the charge pump and the decimators that have to
describe the same oversampling as `PLLAD_CKOS`. Pass-through should reach the
divider through it rather than through a literal, with the channel's 2047 as the
clamp.

## What the ceiling is worth

1856 to about 2032 is roughly 9.5% more samples per line. It does not reach an
integer relationship with the source's pixel clock, which is what would remove
beating outright: 800x600@60 is 1056 pixels a line, so two samples per pixel is
2112 and out of reach. The board cannot know that number anyway -- the
horizontal axis has no native resolution here, and 800x600 and 1600x600 present
identical sync to the scaler, differing only in pixel clock.

**1600x600@60 is the test case.** The AKF50 offers it, and the RISC PC drives it
at the same 627 total lines and 37.9 kHz line rate as 800x600@60, so the scaler
cannot tell the two apart and samples both the same. Everything visible between
them is horizontal resolution.
