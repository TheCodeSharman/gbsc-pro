# The deinterlacer had two owners, and only one of them wrote the chip

A Wii moved from 576i to 480p shows a green picture with a horizontally shifted
second copy of itself overlaid. It reads as broken acquisition. It is not:
every measurement is correct throughout.

```
state acquired          lineRateHz 31395
PLLAD_MD 1096 == STATUS_SYNC_PROC_HTOTAL 1096      latched and locked
STATUS_SYNC_PROC_VTOTAL 524    VPERIOD_IF 524      correct for 480p
HPERIOD_IF 214                                     exact for the mode
IF_HSYNC_RST 1096  SP_RT_HS_SP 1019                sized for an undoubled line
STATUS_IF_VT_OK 1  STATUS_IF_INP_NTSC_PRG 1        progressive, and named so
```

What is wrong is one block: `MAPDT_VT_SEL_PRGV` 0 with `WFF_ENABLE` and
`RFF_ENABLE` 1. The motion-adaptive deinterlacer is running on a progressive
source.

**The horizontal shift is the tell, and it is why this does not look like bob
judder.** `enableMotionAdapt()` sets `RFF_WFF_OFFSET` to 0x100 and
`RFF_FETCH_NUM` to 0x80, so the read pointer trails the write pointer and a
displaced second image is fetched over the first. Ordinary deinterlacing
artefacts are vertical; this one is sideways.

## Why nothing could turn it off

`rto->motionAdaptiveDeinterlaceActive` was a cache of hardware state, and four
places assigned it directly without writing the chip -- three reset paths in the
sketch and the OLED menu's `LoadDefault()`. They cleared the flag and left the
registers engaged.

The branch that disengages the path is guarded by that flag:

    } else if (scan == ScanProgressive) {
        if (uopt->deintMode == 0 && rto->motionAdaptiveDeinterlaceActive)
            disableMotionAdaptDeinterlace();

So once the two disagreed in that direction the state was terminal. The engine
measured progressive correctly, the branch decided to disable correctly, and the
guard vetoed it on every pass for the life of the boot --
`disableMotionAdapt()` has no other caller.

**Bring-up does not rescue it.** `Deinterlacer::init()` writes
`MAPDT_VT_SEL_PRGV` 1, but it never touches `WFF_ENABLE`, `RFF_ENABLE`,
`RFF_WFF_OFFSET` or `RFF_FETCH_NUM`, which belong to `FrameBuffer`. A full
bring-up would leave the frame buffer configured for deinterlacing.

## What replaced it

`Deinterlacer` owns the state, maintained by the two functions that write the
registers, and the sketch asks rather than remembers. The reset paths call
`disableMotionAdapt()`, which writes the chip and updates the answer together.

**The reset paths must still disable it rather than leaving the next source to
decide**, because the deinterlace branch is gated on `STATUS_IF_VT_OK`. A
separate-sync RGBHV source reads 0 there, so the branch never runs at all --
a path engaged by a component source would otherwise survive the switch to RGBHV
with nothing able to clear it.

A host test locks the pairing: engagement cannot go true to false without the
disable sequence reaching the chip. Verified on the bench by forcing the state
the old code could not leave -- chip engaged, firmware never told -- and watching
a reset path clear it.
