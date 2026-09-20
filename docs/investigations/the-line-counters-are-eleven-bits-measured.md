# Both horizontal counters are eleven bits, and the register is not the counter

`InputFormatter::LineCounterMax` and `HdBypass::MaxChannelLine` are both 2047.
That is correct, and it is now measured on the part rather than inferred from
the datasheet's field width.

It needed measuring because the evidence behind the scaling-path figure did not
hold, and because the register that holds the value is **wider than the counter
that uses it** — so a read-back says nothing either way.

## The register holds twelve bits

`IF_HSYNC_RST` is declared `UReg<0x01, 0x0E, 0, 11>`, spanning `s1_0E[7:0]` and
`s1_0F[2:0]`. Writing the pair as raw bytes, past the by-name path that would
truncate to the declaration:

| written | `s1_0E` | `s1_0F` | 12 bits read back |
|---|---|---|---|
| 2047 | 0xff | 0x07 | 2047 |
| 2048 | 0x00 | 0x08 | 2048 |
| 2094 | 0x2e | 0x08 | 2094 |
| 4000 | 0xa0 | 0x0f | 4000 |

So `s1_0F` bit 3 is storage that accepts and returns a 1, the same shape
[`the-bypass-divider-is-capped-by-the-channel-counter.md`](the-bypass-divider-is-capped-by-the-channel-counter.md)
found on `HD_HSYNC_RST`.

**This refutes the reading the constant rested on.** It was recorded as
*"`PLLAD_MD` 2094 was accepted, latched and read back correctly at
`STATUS_SYNC_PROC_HTOTAL` while `IF_HSYNC_RST` held 46"* — and 2094 & 0x7FF is
46, so what truncated the write was the **firmware's own eleven-bit
declaration**, before the value reached the chip. The measurement confirmed the
declaration against itself.

## No register can answer it

Frozen on a settled source — 311 lines doubled, `PLLAD_MD` 2206, correct line
counter 1103 — with the counter written by hand:

| written | `STATUS_IF_HT_OK` | `STATUS_IF_HT_BAD` | `HPERIOD_IF` |
|---|---|---|---|
| 1103, correct | 1 | 0 | 431 |
| 1500, destroys the picture | 1 | 0 | 431 |
| 2151 | 1 | 0 | 431 |
| 3151 | 1 | 0 | 431 |

**The positive control fails**: 1500 tears the picture into two sheared copies
and every status field reads exactly as it does when the counter is right.
`HPERIOD_IF` is a time measurement and does not follow the counter at all. So
the picture is the only instrument here.

## The picture settles it

The discriminator is **1103 + 2048 = 3151**. An eleven-bit counter uses the low
eleven bits of it, which is 1103 — the correct value — so an eleven-bit counter
must make 3151 and 1103 indistinguishable, and a twelve-bit one cannot.

| written | an 11-bit counter uses | picture |
|---|---|---|
| 1103 | 1103 | clean PM5544 |
| **3151** | **1103** | **clean, indistinguishable from 1103** |
| 1500 | 1500 | sheared, the card repeated twice down the frame |
| 2151 | 103 | hash |

Four outcomes, and eleven bits is the only width that predicts all four: a
twelve-bit counter would break at 3151, and a ten-bit one would have broken at
1103. The control was shot either side of the discriminator and both came back
clean.

**`s1_0F` bit 3 is writable storage the counter ignores.**

## What it means

The wall is **2047 IF units per line, on every source and both scan modes** —
undoubled an IF unit is one ADC sample and doubled it is two, so the divider
ceiling differs while the retained count does not. Pass-through hits the same
2047 in `HD_HSYNC_RST`, which is why `HdBypass::dividerFor()` stops at 2039
after its raster guard.

So no source wider than about 2047 pixels per line can be carried with one
retained sample per pixel by either route, and 1920x1080 at htotal 2200 is
outside it. [`../sampling-table.md`](../sampling-table.md) has what each mode
lands on.

## The trap to carry forward

A value past the counter is **stored, reads back correct, and produces a raster
nothing can display**, with no indicator: `STATUS_MISC_PLLAD_LOCK` stays 1 and
`STATUS_SYNC_PROC_HTOTAL` goes on reporting the divider, because both describe
the ADC PLL and neither describes a counter.

And a field declared narrower than the register truncates every write through it
and says nothing — so a probe issued through the by-name path cannot test the
declaration. Write the bytes.
