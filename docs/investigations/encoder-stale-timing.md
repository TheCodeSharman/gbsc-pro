# The encoder keeps the previous timing across a raster change

The MS9288A samples the scaler's analog output and re-encodes it, and it does
not always notice that the timing under it has moved. The scaler emits a
correct, locked raster; the encoder carries on transmitting the mode it locked
to before; the TV reports that older mode and shows nothing.

**It is recoverable without power.** Dropping HSOUT/VSOUT and restoring them
makes the encoder re-acquire, and the picture returns immediately.

## The signature

Every scaler-side diagnostic is green, which is what makes this expensive to
read as anything else:

- the sampling divider is latched — `STATUS_SYNC_PROC_HTOTAL` equals `PLLAD_MD`
- the capture window, scan mode and output raster are all solved for the
  current source
- FrameSync is locked, with the console reporting input and output field rates
  agreeing to six figures and the display clock being steered

**What identifies it is the rate the TV reports.** A sink showing nothing
because it rejected a mode reports no mode; a sink fed a stale one reports the
*previous* rate. Measured 2026-08-20, twice: the scaler emitting 1279x1125 at
75.001 Hz with the TV reporting 1920x1080@50, and the scaler emitting
1916x1125 at 50 Hz with the TV reporting 1920x1080@60.

**A field rate the sink cannot take is a different fault and reads
differently.** The 75 Hz output above had been displayed successfully before,
so the mode was acceptable; it was never delivered.

## The recovery

`PAD_SYNC_OUT_ENZ`, s0_49 bit 2, set to 1 and back to 0 with a pause between.
Measured with a 1.5 s pause: the picture returns at once, correct, with no
register other than that bit touched and no power cycle.

```sh
python3 - <<'EOF'
import time, gbs_unit
h = "<ip>"
v = gbs_unit.read_reg(h, 0, 0x49)
gbs_unit.write_reg(h, 0, 0x49, v | 0x04)   # HSOUT/VSOUT off
time.sleep(1.5)
gbs_unit.write_reg(h, 0, 0x49, v & ~0x04)  # and back
EOF
```

Entering RGBHV bypass recovers it as well, which is the same mechanism reached
a longer way round: the bypass switch reconfigures the output path outright.

**Try this before pulling the rails.** Whether every case reported as an
encoder wedge is this one is not established — but it costs two register writes
against a bench trip.

## A firmware flash reaches it too, and nothing in the firmware covers that

Measured 2026-09-08 on the Wii, YPbPr, PAL 576i. An OTA flash while that source
was selected came back with the television dark and every scaler-side reading
correct: `STATUS_SYNC_PROC_VTOTAL` 310, `STATUS_SYNC_PROC_HTOTAL` 2250 against a
2250 divider, `PAD_SYNC_OUT_ENZ` 0, `DAC_RGBS_PWDNZ` 1, a solved raster at
`VDS_HSYNC_RST` 2010 / `VDS_VSYNC_RST` 1065, and the engine reporting
`state: acquired`. The toggle brought the picture back at once, and the sink's
own overlay then named the mode it had acquired -- 1280x1024 at 50 Hz.

**A flash is the case the classification can never catch.** `useHdmiSyncFix` is
armed inside `doPostPresetLoadSteps()` off a change of input classification, and
across a flash the input does not change at all -- the source is the same, the
ESP restarts, and the encoder is left holding a timing from before the reset
while the scaler re-solves an equivalent one. That is the same argument as the
section below, from a direction the input classification cannot see even in
principle.

So the post-flash black screen has TWO recoveries with different causes, and
reaching for the wrong one costs a detection sweep:

| what is stale | signature | recovery |
|---|---|---|
| the chip's sync path, left from before the reset | `SP_SOG_MODE` 1 on a separate-sync source, `SP_VTOTAL` 0 or 97, `/geometry` all zeroes | `/sc?~` |
| the encoder's timing | every scaler reading correct, `state: acquired`, sync out enabled | the `PAD_SYNC_OUT_ENZ` toggle |

Both were seen in one session on the same unit, an hour apart. The second one's
registers are the first one's opposite, so the two are told apart by reading
`STATUS_SYNC_PROC_VTOTAL` before choosing.

## The firmware already has the mechanism, aimed too narrowly

`rto->useHdmiSyncFix` is exactly this drop-and-restore. It patches the preset
table so the load leaves `PAD_SYNC_OUT_ENZ` set, then writes it back to 0 once
the load has settled, at three sites in `doPostPresetLoadSteps()`.

It is armed only where the *input* classification swaps inside the SD 50/60
families, or where there was no signal at all:

```
((vsi == 1 || vsi == 3) && (detected == 2 || detected == 4)) ||
  vsi == 0 ||
((vsi == 2 || vsi == 4) && (detected == 1 || detected == 3))
```

So a change between an SD source and a VGA-class one leaves it 0, the pads are
never dropped, and the encoder is given no reason to re-acquire — which is both
of the measured cases.

**The condition is asking the wrong question.** What the encoder cares about is
whether the *output* timing moved, and the input classification is a proxy for
that which does not hold: two sources in the same family can solve to the same
raster, and two in different families to different ones. `Geometry::solveRaster()`
is what knows, because it is what changed the raster. Kicking the pads from
there covers every case the classification misses, including a raster that
moves without the standard changing at all.

Not implemented. `setOutModeHdBypass()` and `bypassModeSwitch_RGBHV()` return
before `doPostPresetLoadSteps()`, so a bypass transition takes neither path
today and recovers for its own reasons.

## Open

- Whether the drop has a minimum length. 1.5 s works; nothing shorter has been
  tried, and the three existing call sites use 10 to 70 ms delays around the
  write.
- Whether the encoder ever fails to re-acquire from the toggle, which is what
  would separate this from a state only removing power clears.

## See also

- [../rgbhv-bypass-trap.md](../rgbhv-bypass-trap.md) — why a bypass transition
  retimes the output
- [../../CLAUDE.md](../../CLAUDE.md), "No HDMI with every register perfect"
