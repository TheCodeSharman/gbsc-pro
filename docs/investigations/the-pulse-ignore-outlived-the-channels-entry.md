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

## The first fix, which was half of it

The entry leaves the sync processor alone. It is configured for the SOURCE, and
the load has already done that for this one. That answers the channel entered
FROM the scaling path and nothing else -- see below.

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

## The entry was half of it: a boot into pass-through still landed on 99

Entering the channel from the scaling path leaves the SCALING divider's
fraction, 59, which is harmless. Booting straight into pass-through leaves the
CHANNEL's: `applyForSyncType()` derives the threshold from
`Adc::dividerInForce()`, and on that route the divider in force is already
2038, so 99 is what the arrangement writes and nothing on the channel revises
it.

**The failure is quieter than the one above and worse to read.** Measured on the
Wii at 480p on `ypbpr` after a `/restart` with `scale-rgbhv = 0`: mean luma
0.00, `state: acquired`, `present: true`, the console silent for 75 s, and
`PLLAD_MD` 2038 against `STATUS_SYNC_PROC_HTOTAL` 2038, `VTOTAL` steady at 524
and `HLOW_LEN` steady at 152. There is no re-solve cycle to notice and no
register out of place. 14 written by hand into the same acquisition, nothing
else moved, emitted a 1670x1028 picture at once.

## The scan was never measured on the channel, and that is what made the collapse wrong

`SyncProcessor::applyDynamic()` wrote the threshold for a source on the scaling
path and skipped it for a path source -- the channel, or the RGBHV connector.
Writing it there too is the obvious collapse, and tried on its own **it
reproduces the fault**: `sourceIsProgressive()` is false on the channel, so the
dynamic pass derives the same interlaced fraction of the same 2038 for itself,
threshold 99, mean luma 0.00. Measured with that build, having first confirmed
over three input selections each way that it broke nothing else -- `vga` held
255 and acquired in 3.2 / 4.5 / 3.2 s, `ypbpr` converged 99 -> 59 -> 14 and
acquired in 3.1 / 2.8 / 2.8 s.

**The reason was a gate one layer up.** `VideoSourceAcquisition::maintainSource()`
returned for the channel BEFORE calling `SourceMeasurement::measureScanType()`,
because the deinterlacer is what the scan was measured for. So the channel had
no scan to configure from, and a measurement the engine could take in one line
read as a fact the route could not know.

The scan is measured ahead of that return now and the deinterlacer steering
stays off the channel, which makes the one owner correct. The console prints
`scan: progressive, count 524` on the channel where it printed no scan line at
all, and a boot into pass-through lands on 14 by itself: five reads at 14, mean
luma 163.45 across four identical frames, with the divider, both counts and the
low length unchanged from the failing state.

`vga` on separate sync holds 255 through the channel and acquires in 6.5 s with
a 1920x1079 picture, and the `ypbpr` scaling path converges to 14 at divider
1212 as it did.

## What this does not settle

**A composite source on the RGBHV connector now takes the measured-scan value
where it kept the fraction.** That route is measured indifferent across the
whole range -- 107 and 2 both counting 623 on the RISC PC at 800x600 with
`SYNC 1`, `the-risc-pc-composite-sync-is-not-serrated.md` -- so nothing argues
against it, and it is untested on this build.

**An interlaced source on the channel takes the fraction of the channel's own
line**, which is arithmetically right where the block counts at that divider
and has not been measured there. The host suite pins the branch; the bench
cannot reach it without changing the Wii's output mode.
