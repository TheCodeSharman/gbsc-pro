# A wide status field tears across its two bytes

`STATUS_SYNC_PROC_HLOW_LEN` is 12 bits at `s0_19`, so a read fetches two bytes.
`tw.h`'s `rawRead()` takes them in ONE I2C transaction, which looks atomic from
the host and is not: the chip clocks the second byte out tens of microseconds
after the first, and the sync processor updates the register every line. A read
straddling an update returns the low byte from one instant and the high nibble
from the other.

**The signature is a 2x2 crossing of two genuine values**, and it is decisive
because the four products appear and nothing else does. Measured on `vga` at
320x256@50 on composite sync, 27207 samples at 1 ms:

| | low byte `0x99` | low byte `0x65` |
|---|---|---|
| high nibble `0x0` | **153** x26770 | **101** x25 |
| high nibble `0xA` | **2713** x32 | **2661** x71 |

153 is the source's hsync and 2661 the vertical interval's low period, which
arrives once a frame. 101 and 2713 are the crossings, and `0x064`/`0xA64` give
the same pair at 100 x4 and 2660 x2. 61 torn reads against 73 clean ones in the
interval puts the gap between the two bytes at about half a 64 us line.

## What follows

**A VALUE THAT IS NOT A CROSSING OF TWO OTHERS IS THE ONLY KIND WORTH READING.**
`STATUS_SYNC_PROC_VTOTAL` visits 337 (`0x151`) where its other values are 308,
309 and 310 (`0x134`..`0x136`): every one shares the high nibble, so a tear
could only substitute a genuine low byte, and `0x51` is not one. That reading is
real. Decompose before believing an outlier.

**AND A FIELD THAT NEVER MOVES CANNOT SHOW THE ARTEFACT**, which makes a
comparison across two states unsound in one direction. The same source on
separate sync reads `hlow_len` 155 or 156 in 28343 of 28343 samples -- one high
nibble, two adjacent low bytes -- so tearing there is invisible by
construction. Reading composite's 23 distinct values against separate sync's 2
as a measurement of pulse stability is therefore wrong, and the genuine
composite pulse dithers by the same one unit the separate-sync one does.

**A SINGLE-BIT FIELD IS IMMUNE**, so `STATUS_MISC_PLLAD_LOCK` and the `ifbits`
byte carry across a comparison where a wide field does not.

The twelve singleton values below 120 -- `0x000`, `0x001`, `0x011`, `0x01A`,
`0x01F`, `0x02A`, `0x033`, `0x041`, `0x05A`, `0x05E`, `0x07D`, `0x0A5`, one
occurrence each in 27207 -- are not crossings of the observed pair. A counter
read part way through its count produces exactly that shape, and nothing here
separates it from a genuinely short pulse.
