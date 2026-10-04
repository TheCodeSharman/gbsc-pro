#!/usr/bin/env python3
"""Say whether the emitted frame is a picture or a flat field, by number.

    python3 tools/gbsc-pro-hwtest/picstate.py
    python3 tools/gbsc-pro-hwtest/picstate.py --frames 3 --png /tmp/state.png

Two numbers, both taken INSIDE the borders hdmi_capture finds:

  spread  luma standard deviation. A picture is 90-110 on the test card; a flat
          field of any colour is under 6. This is what separates "the scaler is
          emitting the source" from "the scaler is emitting a level".
  cast    how far the three channel means sit from each other, as a fraction of
          full scale. Balanced content cancels to about 0.02; a saturated field
          is an order of magnitude up.

**SCORE INSIDE THE BORDERS.** A flat white field scores spread 102 over the
whole frame, because the pillarbox is in the sample -- which reads as a picture.
The same frame inside its borders is 5.3.

`docs/bench-output-capture.md` is what the capture can and cannot answer.
"""

import argparse
import json

import numpy as np

import hdmi_capture

# Below this the frame carries no picture, whatever its level.
FLAT_SPREAD = 6.0


def score(rgb):
    """One RGB frame as a dict: its borders, and the two numbers inside them."""
    grey = hdmi_capture.luma(rgb)
    edges = hdmi_capture.borders(grey)
    if edges["width"] == 0 or edges["height"] == 0:
        return dict(edges, spread=0.0, cast=0.0, luma=float(grey.mean()), flat=True)

    inside = rgb[edges["top"]:rgb.shape[0] - edges["bottom"],
                 edges["left"]:rgb.shape[1] - edges["right"]]
    means = inside.reshape(-1, 3).mean(axis=0)
    spread = float(hdmi_capture.luma(inside).std())
    return dict(edges,
                spread=spread,
                cast=float(np.abs(means - means.mean()).max() / 255.0),
                luma=float(means.mean()),
                flat=spread < FLAT_SPREAD)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--device", default=None)
    parser.add_argument("--png", default=None, help="save the first frame here")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    rgb = hdmi_capture.frames(args.frames, args.device or hdmi_capture.device())
    if args.png:
        hdmi_capture.write_png(args.png, rgb[0])

    for frame in rgb:
        state = score(frame)
        if args.json:
            print(json.dumps(state))
            continue
        print(f"cast {state['cast']:.2f} spread {state['spread']:.1f} "
              f"luma {state['luma']:.1f}  "
              f"margins {state['left']}/{state['right']}/{state['top']}/{state['bottom']}  "
              f"{'FLAT FIELD' if state['flat'] else 'picture'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
