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
