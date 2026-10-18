# The key carried the origin's calibration

`SourceKey`'s sync share was built from the pulse the sync processor's counter
reports, and that reading has `SyncProcessor::InvertedPulseWidthSamples` taken
off it wherever the path is inverted or separated. The term is a count of ADC
samples, so it is a different share of the line at every divider -- and the
divider is bounded by the OUTPUT raster. So the key moved when the output
moved, and the framing stored against a source stopped answering for it.

## The reading is sound and the constant is what varies

Measured on the Wii at 480p on `ypbpr`, automation frozen and the source held,
walking `PLLAD_MD` through `/sampleclock` and reading
`STATUS_SYNC_PROC_HLOW_LEN` by name at each value:

| `PLLAD_MD` | pulse | share, ten-thousandths |
|---|---|---|
| 1212 | 87 | 718 |
| 1300 | 94 | 723 |
| 1446 | 105 | 726 |
| 1700 | 123 | 724 |
| 2046 | 148 | 723 |

CEA-861 states 480p's hsync as 62 of 858 pixels, which is **723**. The raw
reading is that figure across a 69% sweep of the divider, to four
ten-thousandths -- so the source's share is already divider-invariant and
already the published one.

**It is not a settle.** Polled across an install from 1212 to 1446, the count
reaches its settled 105 in the first sample after `PLLAD_MD` updates, 177 ms
after the request, with no ramp and no intermediate value. Occasional torn
reads appear beside it -- 1, 58, 59, 104, 1385 -- and the modal value never
moves.

## What it cost

The same source re-acquired at three output modes, each of which installs a
different divider for the duty pass:

| output | divider the duty is counted at | key share before | after |
|---|---|---|---|
| 1280x720 | 2046 | 699 | 723 |
| 1920x1080 | 1446 | 685 | 719 |
| 720x480 | 1856 | 673 | 727 |

`SourceKey::SyncWidthIdentity` allows **20**. The spread before is 26, so two
outputs give two sources and neither matches an entry stored under the third;
after, it is 8.

The `vga` RISC PC at 320x256@50 moves the same way, 673 to 695 on composite and
686 to 709 on separate sync against AKF50's 36 of 512 = 703.

## Why the term is right where it is and wrong in the key

`InvertedPulseWidthSamples` is a calibration of the capture origin, not a
measurement of the pulse. It was fitted as a RELATIVE alignment: over seven
modes the derived pulse read -4.4, -4.8, -2.7 and -3.3 against the mode file on
H-negative sources and +1.8, +1.7, +1.7 on H-positive ones, and the term brings
the positive side down onto the negative one because `RetimeOriginSamples` was
calibrated there. Both sides are left about four samples low, which the origin
absorbs.

A key wants the opposite: the source's share in absolute terms, so that it
matches the published raster and does not move. So the pulse is read once and
the calibration is applied only where the counter's units are being written.

**The evidence given for the term being a constant COUNT rather than a constant
time does not reach constant count versus constant share of the line.** It was
taken at `PLLAD_MD` 1438 and 1444 with the sample rates differing by 27%: the
divider barely moves there, which separates a count from a time and cannot
separate a count from a fraction. The sweep above moves the divider instead.

## Open

**Three consumers of `HsyncPulse::syncDuty()` inherited the term and nothing
says they want it** -- the capture window's sync interval in
`VideoSourceLine`, `SyncProcessor::clampOffsetForPulse()`, which spends it in
27 MHz counts rather than ADC samples, and
`SyncProcessor::placeSeparatorPulseEnd()`. Only `retimeStopFor()` is where it
was calibrated. Removing it from the other three is a framing change on every
source and is not made here.

**Every stored framing entry is re-keyed by this**, the shares all moving up by
the term's share of whatever divider they were taken at. There is no migration,
because the divider an entry was stored at is not recorded.
