# Capturing the board's HDMI output

`docs/bench-sources.md` is what feeds the board. This is what reads what comes
out of it.

## The instrument

A USB HDMI capture dongle, `345f:2131` UltraSemi USB3 Video, MS2130 family,
SuperSpeed, presenting YUYV 4:2:2 uncompressed. It offers sizes from 640x480 to
2560x1600, and the picture arrives on all of them -- the chip scales, so the
requested size is a choice about resolution rather than a mode that has to
match what the board emits.

`tools/gbsc-pro-hwtest/hdmi_capture.py` drives it. **The node renumbers on every
replug**, so the tool finds it by USB id and nothing should hardcode
`/dev/video4`.

```sh
python3 tools/gbsc-pro-hwtest/hdmi_capture.py --frames 4
python3 tools/gbsc-pro-hwtest/hdmi_capture.py --png /tmp/frame.png
```

It prints the black margin at each edge, which is where the emitted picture
starts and stops, and frames of one state come back identical.

**THE WARM-UP IS NOT OPTIONAL.** The first frames after the device is opened are
black, and a one-frame grab returns one of them -- which reads exactly like no
signal. The tool discards forty.

## A stream of black frames is the HDMI connector, not the signal

**This is the trap, and every software-visible signal says the unit is healthy
while it happens.** With its HDMI input unseated the dongle stays enumerated,
reports 1920x1080 at 60 fps, and delivers a full stream of well-formed frames at
limited-range black. Nothing in `lsusb`, the kernel log or `v4l2-ctl` says
otherwise.

Neither power cycle reaches it. The dongle re-enumerates cleanly over USB and
carries on delivering black; the board reboots, re-acquires and carries on
emitting. **Re-seat the HDMI cable at the dongle**, which is the only thing that
clears it, and check it before reading black as a fault anywhere else.

What the board is doing is answered without the dongle at all: the console's
`frame time lock` line carries `out`, which is the TV5725's own VSOUT sampled on
`DEBUG_IN_PIN`. That proves the scaler is feeding the encoder. **It does not
prove the encoder is transmitting** -- it is upstream of the MS9288A -- so it
exonerates the scaler and says nothing about the link.

## The loop-out carries a television at the same time

The dongle's second HDMI socket drives a second sink, and the capture is
unaffected by what is on it. Plugging one in costs **one black frame** as the
link re-acquires, and the capture is back at its previous value on the next.

So the television and the capture run together: one is watched while the other
is measured, and neither needs the cable moved.

## What it retires

- **The photo-column mapping**, which does not survive an output mode change and
  had to be recalibrated against a differenced frame after any excursion.
- **The room**, which moves absolute brightness further than anything the scaler
  does, and whose auto-exposure lifts mid-tones while leaving blacks alone --
  which reads as a gamma change.
- **The argument about where the panel edges are.** The capture's frame is the
  HDMI active area, so an edge is a number rather than a judgement.

A photograph of the television remains the second view, and is still what
settles anything about what a display does with the signal.

## Open

**Whether the sink's EDID changes what the board emits is not tested.** The
MS9288A reads the sink's EDID over its own DDC master, so a dongle and a
television are not guaranteed to be handed the same mode. The A/B is one mode
measured into each, and with the loop-out carrying the television both are
present at once.
