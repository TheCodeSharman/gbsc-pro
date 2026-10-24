"""PM5544's finest grating, located in an emitted frame and scored.

The grating is the one feature on the card that asks whether the ADC sampled
the source well. Below two samples per source pixel it sits above the
sampler's Nyquist, so the sampling phase decides how much of it survives --
and nothing else on the card is near enough the limit to show it.

**THE BOX IS FOUND, NOT STATED.** The card scales with the mode but its
layout does not scale with the emitted raster: measured over three
pass-through modes at 1920x1080, the dense block sits at x 0.19..0.31 of the
lit picture on 800x600@60 and 1024x768@60 and at 0.27..0.44 on 1280x720@60.
A fixed fraction therefore covers grating on one mode and flat card on
another, and a reading taken through it is not comparable between them --
which is the whole point of a per-mode sweep.

**AND NO CELL GRID.** A grating this fine is sensitive to where a cell
boundary falls on it: two runs of the same sampling phases ranked them in
opposite orders purely by where the grid landed. Scored per pixel there is no
grid to align.
"""

import numpy as np

# Where on the card to look, as fractions of the LIT picture. Vertical only:
# the wedge's own row band is stable across modes where its horizontal extent
# is not, and bounding the search vertically keeps the colour bars and the
# greyscale staircase out of the crossing profile.
BAND_TOP, BAND_BOTTOM = 0.54, 0.64

# The horizontal span searched for it, which excludes the card's outer
# checkerboard -- that is a square wave too, and a coarse one at the frame's
# own edge would otherwise anchor the box.
SEARCH_LEFT, SEARCH_RIGHT = 0.14, 0.80

# A slice of the search span, as a fraction of the lit width. Wide enough that
# the crossing count is not noise and narrow enough to resolve where the
# grating starts.
SLICE = 0.01

# Crossings per row below which a slice carries no grating. Flat card measures
# 1..3 across every mode surveyed and the wedge 5..10, so this separates them
# outright and stops a frame with no wedge in it from finding one.
FLAT_CROSSINGS = 4.0

# The share of the wedge taken, from its LEFT edge.
#
# THE EDGE IS THE FEATURE AND THE PLATEAU IS NOT. The wedge coarsens
# continuously rather than in blocks, so a box picked by "the densest slices"
# moves with whichever slice happened to read highest -- tried, and on
# 1280x720@60 it collapsed onto four slices at 0.32..0.36 while the wedge ran
# 0.27..0.48. The step from flat card into grating is sharp in every mode
# surveyed, so the box is taken as a share of the run that follows it.
FINEST_SHARE = 0.34

# The narrowest run that counts as a wedge. Below this the profile found an
# edge in the band rather than a grating.
MIN_SLICES = 6


def _crossings(block):
    """Zero crossings about the block's own mean, per row."""
    above = (block > block.mean()).astype(int)
    return float((np.diff(above, axis=1) != 0).sum(axis=1).mean())


def profile(grey, box):
    """Crossings per row for each slice across the search span, as
    (x_fraction, crossings)."""
    left, right, top, bottom = box
    width, height = right - left + 1, bottom - top + 1
    band = grey[top + int(BAND_TOP * height):top + int(BAND_BOTTOM * height), :]
    step = max(8, int(SLICE * width))
    out = []
    fraction = SEARCH_LEFT
    while fraction < SEARCH_RIGHT:
        start = left + int(fraction * width)
        out.append((fraction, _crossings(band[:, start:start + step].astype(float))))
        fraction += SLICE
    return out


def locate(grey, box):
    """The finest grating's box, as (left, right, top, bottom) in pixels.

    Raises where no grating was found, which is the honest answer for a frame
    that is clipped, black, or showing another card: a reading taken from a
    fallback box would be a reading of whatever took the grating's place.
    """
    left, right, top, bottom = box
    width, height = right - left + 1, bottom - top + 1
    readings = profile(grey, box)
    densest = max(crossings for _, crossings in readings)
    if densest < FLAT_CROSSINGS:
        raise AssertionError(
            f"no grating in the wedge band: the densest slice carries "
            f"{densest:.1f} crossings per row against a flat card's 1..3. "
            f"The picture is not PM5544, or it is not framed.")

    run, best = [], []
    for fraction, crossings in readings:
        if crossings >= FLAT_CROSSINGS:
            run.append(fraction)
        else:
            if len(run) > len(best):
                best = run
            run = []
    if len(run) > len(best):
        best = run

    if len(best) < MIN_SLICES:
        raise AssertionError(
            f"the longest run of grating is {len(best)} slices, under "
            f"{MIN_SLICES}: that is an edge in the band rather than a wedge. "
            f"Densest slice {densest:.1f} crossings per row.")

    finest = best[:max(MIN_SLICES, int(round(FINEST_SHARE * len(best))))]
    return (left + int(finest[0] * width),
            left + int(finest[-1] * width) + max(8, int(SLICE * width)),
            top + int(BAND_TOP * height),
            top + int(BAND_BOTTOM * height))


def score(greys, wedge_box):
    """(contrast, movement) over a clip, both in grey levels.

    `contrast` is how much of the grating survived sampling -- the spatial
    spread of the time-averaged block. `movement` is how far each pixel moves
    across the clip, which on a card with its animation off is the system's
    and nothing else's.
    """
    left, right, top, bottom = wedge_box
    block = greys[:, top:bottom, left:right].astype(float)
    return float(block.mean(axis=0).std()), float(block.std(axis=0).mean())


def flattest(grey, box, grating):
    """A patch of the grating's own shape, in the flattest card clear of it.

    **THE MOVEMENT READING NEEDS A SCALE AND A CONSTANT CANNOT CARRY IT.** How
    far a pixel travels between frames is partly the capture's own noise, which
    belongs to the dongle rather than to the board -- measured at 1.05 grey
    levels against the grating's 7.60 on one clip. A reference taken from the
    same frames turns the grating's movement into a multiple of that noise,
    which has a right answer and survives a change of dongle.

    Same width and same rows as the grating, so the two readings are of equal
    populations, and never overlapping it.
    """
    left, right, top, bottom = box
    width = right - left + 1
    span = grating[1] - grating[0]
    step = max(8, int(SLICE * width))
    band = grey[grating[2]:grating[3], :]

    best, found = None, None
    start = left + int(SEARCH_LEFT * width)
    while start + span <= left + int(SEARCH_RIGHT * width):
        if start + span <= grating[0] or start >= grating[1]:
            crossings = _crossings(band[:, start:start + span].astype(float))
            if best is None or crossings < best:
                best, found = crossings, start
        start += step

    assert found is not None, (
        f"no patch of {span} px clear of the grating at {grating[0]}..{grating[1]} "
        f"fits inside the search span of a {width} px picture")
    return (found, found + span, grating[2], grating[3])
