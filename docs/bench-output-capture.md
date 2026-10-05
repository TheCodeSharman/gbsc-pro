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

## A black capture is the board first and the connector last

**ASK THE SCALER BEFORE ASKING THE CABLE.** A black capture has been a board
state, and the reason it is worth stating in this order is that a clean register
dump does not clear the board: the blocks the low-power teardown holds down were
left held after the source acquired, and the part emitted nothing while
`/geometry` reported `acquired` and every configuration register read correct.

The order that separates them, cheapest first:

| ask | healthy |
|---|---|
| `s0_46` | `SFTRST_MEM_RSTZ` and its four neighbours all 1 -- **active low**, so 0 is a block held in reset and no video crosses the part |
| `s0_45` | 0x11, the three colour channels enabled and the DAC powered |
| `s0_49` | `PAD_SYNC_OUT_ENZ` 0 and `PAD_TRI_ENZ` 0, so HSOUT/VSOUT are driven |
| the console | `frame time lock` carrying an `out` rate that matches `in` |

That last one is answered without the dongle at all: `out` is the TV5725's own
VSOUT sampled on `DEBUG_IN_PIN`, so it proves the scaler is feeding the encoder.
**It does not prove the encoder is transmitting** -- it is upstream of the
MS9288A -- so it exonerates the scaler and says nothing about the link.

With all four healthy and the panel still dark, the encoder is next
(`PAD_SYNC_OUT_ENZ` toggled 1 then 0), and a full power cycle -- mains *and*
USB -- after that.

## IT DELIVERS BLACK FOR SECONDS AFTER THE LINK RE-ACQUIRES

Anything that re-acquires the HDMI link -- a boot, an input change, a sync-type
round trip -- leaves the dongle delivering black frames after `/geometry`
already reports `acquired`. Measured on a vga boot: black at 1.1, 1.9, 2.7, 3.5
and 4.3 s, the picture at 5.1 s. Other transitions clear inside a second.

**TWO SCORES TAKEN BACK TO BACK ARE BOTH INSIDE THAT WINDOW**, which is how a
healthy unit reads as a regression twice in a row: `picstate.py` run twice is
two grabs a few seconds apart, and both land in the dead period.

## AND IT DELIVERS BLACK INSIDE A SETTLED STREAM, WITH THE BOARD UNCHANGED

A black frame is not evidence at any time, not only after a transition.
Measured on `vga` at composite sync, each grab paired with the board's own state
in one window: a picture, then five consecutive black frames, then a picture
again, with `PAD_SYNC_OUT_ENZ` 0, `DAC_RGBS_PWDNZ` 1, `SFTRST_MEM_RSTZ` 1,
`VDS_VSCALE` 485 and `state: acquired` identical throughout and the sync
processor counting 308/309 across all of it. Video cannot have stopped and
restarted with every one of those unchanged, so the dongle dropped it.

**THE FORTY WARM-UP FRAMES ARE NOT ALWAYS ENOUGH EITHER**, so re-opening the
device per grab does not escape it: of three bursts of twelve consecutive frames
taken in one open, one came back with frames 1, 2 and 4 black and the rest a
picture. Re-opening also costs the forty discarded frames again.

So take a BURST in one open and use the first frame that is not flat, retrying
the burst with a ceiling before calling a field flat:

```python
for frame in hdmi_capture.frames(12, dev):
    scored = picstate.score(frame)
    if not scored["flat"]:
        break
```

`configure_oracle.settled_picture()` is that loop with the retry and the
ceiling, injectable so it is testable without a dongle.

## The unseated HDMI input, which is the last resort

With its input unseated the dongle stays enumerated, reports 1920x1080 at 60 fps,
and delivers a full stream of well-formed frames at limited-range black. Nothing
in `lsusb`, the kernel log or `v4l2-ctl` says otherwise, and neither power cycle
reaches it: the dongle re-enumerates cleanly and carries on delivering black.
Re-seating the cable is the only thing that clears it.

**It belongs at the bottom of the list rather than the top.** A connector that
has been seated stays seated, so this explains a black capture once and then
stops being a live hypothesis -- while a scaler that emits nothing is reachable
from any session and has done it since. Reaching for the cable first is how a
board fault gets a session spent on the bench.

## The loop-out carries a television at the same time

The dongle's second HDMI socket drives a second sink, and the capture is
unaffected by what is on it. Plugging one in costs **one black frame** as the
link re-acquires, and the capture is back at its previous value on the next.

So the television and the capture run together: one is watched while the other
is measured, and neither needs the cable moved.

**WHICH MEANS THE TELEVISION CORROBORATES NOTHING ABOUT THE BOARD.** It hangs
off the loop-out, so it is downstream of the dongle's INPUT: a bad cable into the
dongle, or an unseated connector there, blacks out both views at once. Two dead
views read as the board sending nothing, and are one fault in the one link both
share. Rule that link out before the board -- but after the scaler
configuration, because a connector that has been seated stays seated and
reaching for it first is how a real fault gets a session spent on the cable.

A second sink on a separate output would corroborate; this one is a second
*view* of a single input.

## IT READS NOTHING IN PASS-THROUGH, AND THAT IS THE LINK RATHER THAN THE PICTURE

Measured with the RiscPC at `MODE X800 Y600 C256 F60` and `/uc?x`: every frame
is pure black, `max 0` over the whole raster, while `DAC_RGBS_BYPS2DAC` and
`OUT_SYNC_SEL` read 1, `OUT_SYNC_CNTRL` and `DAC_RGBS_PWDNZ` read 1 and
`PAD_SYNC_OUT_ENZ` reads 0.

**The discriminator is the television OSD, and it needs no camera.** The
STV9426's overlay is keyed into the video at U13, downstream of everything the
scaler does, so a link carrying anything at all carries it. Opened with
`/menu?key=menu` -- the route answers `"open":true` -- the capture is still
`max 0`. Nothing is arriving, so the black is not a picture the board failed to
draw.

So **the capture cannot judge pass-through**, and a black frame taken there is
not evidence about the board. The television is the instrument for that route,
which is what `CLAUDE.md` already says about using bypass as a second view.

**Which side refuses the mode is not established.** The dongle scales whatever
arrives, so the size is not what stops it, which leaves the encoder declining to
transmit a raster this sink's EDID does not offer -- the open question at the
foot of this page. What would settle it is one photograph of the television in
the same state, and the television was not read in the run above.

## `tv-snap` READS THE DONGLE WHEN NO CAMERA IS ATTACHED

It picks the first `/dev/v4l/by-id/*-video-index0` that is not the laptop's own
camera, so with no bench webcam plugged in it selects the capture dongle --
and then applies the saved rectification, a lens and perspective correction
calibrated for a camera pointed at a panel. The result looks like a keystoned
photograph of the television and is the HDMI frame, warped.

**So it is not a second view of anything while that is true.** Check what it
reports as its device before reading one as corroboration: a frame that
disagrees with `hdmi_capture.py` about whether there is a picture is the same
dongle read seconds apart, which is the re-acquisition window above rather
than two instruments agreeing or disagreeing.

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
