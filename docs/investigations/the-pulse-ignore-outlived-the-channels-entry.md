# The channel's entry re-derived the separation threshold

A Wii at 480p on `ypbpr` handed to the pass-through channel emitted nothing:
the USB capture read mean luma 0.00 while every configuration register read
correct, `STATUS_SYNC_PROC_HTOTAL` equalled `PLLAD_MD`, and
`STATUS_SYNC_PROC_VTOTAL` counted the source's 524 lines. The RiscPC on `vga`
was unaffected on the same build, same channel line.

## What it was

`HdBypass::enterFor()` re-applied `SyncProcessor::applyForSyncType()` with the
CHANNEL's divider. Every field that call writes is a sync-type fact
`VideoPath::configureSyncPath()` has already written identically for this
source, with one exception: `SP_H_PULSE_IGNOR` is derived from the divider. So
the call changed exactly one register, and changed it from the value the load
had measured to the interlaced fraction of a divider the load never used.

The fraction is 49 per thousand, so the channel's 2038 gives 99 where the
measured-scan value for a progressive source is 14.

## Why 99 blanks the output

The threshold does not reject the source's hsync -- the pulse is 152 samples
through that line, comfortably above it. What it does is make the block's own
measurement of that pulse bimodal:

| | threshold 99 | threshold 14 |
|---|---|---|
| `STATUS_SYNC_PROC_HLOW_LEN` | alternates 152 / 172 | holds 152 |
| source key | alternates `524@59.94/746` and `524@59.96/844` | one key |
| framing recalled | 16:9, then none -- defaulted 4:3 | the stored one |
| re-solves in 75 s | 13 acquired/absent pairs | none in 60 s |
| emitted frame | mean luma 0.00 | full screen |

The key carries the sync width in ten-thousandths of the line, so the two
readings are two identities for one source. One has a stored framing and one
does not, so the declared shape alternates between 16:9 and the raster's
default, and a shape change is a mode change: each one re-arms a solve and
takes the sync pad away. Nothing is ever presented.

The failure is self-sustaining rather than transient, and it is invisible to a
register dump -- every register reads correct in both halves of the cycle, and
the two halves are five seconds apart.

## The fix

The entry leaves the sync processor alone. It is configured for the SOURCE, and
the load has already done that for this one.

## What this is NOT

Three fields that differ between the two routes were measured innocent, each by
holding the value the working state holds:

- `ADC_SOGCTRL` 24. The scaling path runs 24 with a correct picture; the level
  walks, and was 13 earlier the same session on the same source.
- `SP_SDCS_VSST`/`VSSP` 14/11, start beyond stop. The scaling path holds the
  same pair and shows a correct picture.
- `SP_NO_CLAMP_REG` 1, the clamp held for the life of the route. The channel
  emits a full-screen picture with it held. It is a separate single-ownership
  defect and not this fault.

## The refuted model: let the dynamic pass own the threshold

`SyncProcessor::applyDynamic()` writes the threshold for a source on the
scaling path and skips it for a path source -- the channel, or the RGBHV
connector. Writing it there too looks like the collapse this wants, because it
is one owner deriving one value from the measurement.

**It reproduces the fault.** On the channel the scan is never measured, so
`sourceIsProgressive()` is false and the dynamic pass derives the same
interlaced fraction of the same 2038 for itself: threshold 99, mean luma 0.00.
Measured with that build, having first confirmed over three input selections
each way that it broke nothing else -- `vga` held 255 and acquired in 3.2 /
4.5 / 3.2 s, `ypbpr` converged 99 -> 59 -> 14 and acquired in 3.1 / 2.8 /
2.8 s.

So the threshold cannot be derived on a route that does not measure the scan,
and `applySeparationThresholds()` not writing it is load-bearing.

## What is still open

The threshold in force on the channel is the fraction of whatever divider the
last load ran, not the measured-scan value: 59 here, from the scaling divider
of 1212. It is harmless at 59 and fatal at 99, and nothing chose either. A
source whose scaling divider makes that fraction land near the source's own
pulse would fail the same way, and the engine would again have no register to
show for it.
