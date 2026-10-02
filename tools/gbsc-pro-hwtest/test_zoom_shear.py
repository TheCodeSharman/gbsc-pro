"""The zoom's sample selection, scored off the emitted frame.

Zooming horizontally used to shear the picture on about half its steps -- wrong
sample SELECTION, which reads as the frequency wedge's bars splitting into
hairlines and the card's curves going ragged. `Axis::solve()` biases the memory
window to an ODD width against it, and gives a column away at the right-hand
edge to do it. The bias is a bias and not a cure: why an even width shears is
not known, so nothing but the picture can say whether it still works.
docs/investigations/horizontal-scale-corruption.md

**EVERY MARK ON RECORD CAME FROM AN EYE.** `sweep_zoom.py` and the creep tools
each ask a person watching the screen, which is what the artefact needed while
it was being characterised and is why the bias has never been regression tested.
`picture_shear.py` scores the frame instead, so a build can be asked.

**CONSECUTIVE STEPS, SKIPPING NOTHING.** The fault alternates on roughly one
granule, so readings several steps apart are aliased by construction -- four
rules were refuted that way. Each step here is one capture granule.

Two tests, and the second is why the first is worth anything: one walks the zoom
and asks that every engine-solved step is clean, the other takes a clean step
and makes the width EVEN by hand, and asks that the score moves. A guard that
cannot see the fault it guards against is green for no reason.

Disturbs the framing, so it needs --source, and it resets the framing when it
ends. The hand-set half freezes automation and puts the register back.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gbs_unit
import hdmi_capture
import picture_shear
import setfield
from gbs_unit import get, read_fields, reset_framing

# Enough consecutive granules to cross several parity flips. The artefact
# alternates on about one, so this is tens of chances to see it.
STEPS = 12

# Frames per step. The artefact is not on every frame, and picture_shear takes
# the worst rather than the mean for that reason.
CLIP_FRAMES = 6

# What a clean engine-solved step scores, with headroom over the clean states
# these came from. A step over either is the picture carrying samples it should
# not. PLACEHOLDERS until the calibration below has been run.
CLEAN_DISPLACED = 0.02
CLEAN_ROUGH = 0.12

# How far the hand-set even width has to move a score before the instrument is
# credited with being able to see the fault. Relative to the clean step it was
# taken from, so it is not a second absolute threshold to keep in step.
FAULT_FACTOR = 2.0

WINDOW = ["VDS_HB_SP", "VDS_HB_ST", "VDS_HSCALE"]


def set_field(host, name, value):
    """Read-modify-write by name, so the neighbour sharing the byte survives --
    VDS_HB_ST's top nibble sits in the same byte as VDS_HB_SP's bottom one."""
    seg, reg, off, width = gbs_unit.field_spec(name)
    spec = {"seg": seg, "reg": reg, "off": off, "width": width}
    assert setfield.apply(host, name, spec, value, False), f"{name} would not write"


@pytest.fixture
def capture_device():
    try:
        return hdmi_capture.device()
    except SystemExit as why:
        pytest.skip(str(why))


@pytest.fixture
def framed(host, source):
    """A reset framing to walk away from, and a reset one left behind."""
    assert reset_framing(host) is not None, "the framing never reset"
    yield
    get(host, "/freeze?on=0")
    reset_framing(host)


def window_width(host):
    state = read_fields(host, WINDOW)
    assert state is not None, "the memory window did not read back"
    return state["VDS_HB_ST"] - state["VDS_HB_SP"], state


def score(dev):
    return picture_shear.measure_clip(hdmi_capture.frames(CLIP_FRAMES, dev))


def zoom_one_granule(host):
    """One step in, of the smallest move the axis has.

    press_until_moved grows the request until the framing moves: a press is
    answered in OUTPUT pixels and one granule is granularity x magnification of
    them, so asking for one pixel is no move at all above x2.
    """
    moved, _ = gbs_unit.press_until_moved(host, "z", "eh", 1)
    return moved


def test_every_zoom_step_leaves_the_memory_window_an_odd_width(
        host, framed, capture_device):
    """The rule the engine ships, read off the registers -- cheap, and it says
    which steps the picture test below was actually judging."""
    even = []
    for step in range(STEPS):
        assert zoom_one_granule(host) is not None, \
            f"step {step}: the zoom would not move"
        width, state = window_width(host)
        if width % 2 == 0:
            even.append(f"step {step}: width {width} from {state}")
    assert not even, "the width bias left an even window:\n  " + "\n  ".join(even)


@pytest.mark.zoom
def test_no_zoom_step_puts_the_wrong_samples_on_the_line(
        host, framed, capture_device):
    """The goal the bias exists for, judged on the frame rather than by eye."""
    dirty = []
    for step in range(STEPS):
        assert zoom_one_granule(host) is not None, \
            f"step {step}: the zoom would not move"
        got = score(capture_device)
        if not got["pairs"]:
            continue
        if got["displaced"] > CLEAN_DISPLACED or got["rough"] > CLEAN_ROUGH:
            width, state = window_width(host)
            dirty.append(f"step {step}: displaced {got['displaced']:.4f} "
                         f"rough {got['rough']:.4f} width {width} {state}")
    assert not dirty, ("a zoom step put the wrong samples on the line:\n  "
                       + "\n  ".join(dirty))


@pytest.mark.zoom
def test_an_even_memory_window_is_what_the_score_can_see(
        host, framed, capture_device, register_guard):
    """THE GUARD'S OWN PROOF. Without it a passing sweep says only that nothing
    scored badly, which a blind instrument also says.

    The far edge alone, which is the motion that separated the width rule from
    the register: moving both edges about the centre holds the parity and never
    changes the verdict. docs/investigations/horizontal-scale-corruption.md
    """
    assert zoom_one_granule(host) is not None, "the zoom would not move"
    assert get(host, "/freeze?on=1")[0] == 200, "the unit would not freeze"
    register_guard(3, 0x04)
    register_guard(3, 0x05)

    clean = score(capture_device)
    if not clean["pairs"]:
        pytest.skip("the frame carries no pair the score can judge")
    width, state = window_width(host)
    assert width % 2 == 1, f"the engine left an even width {width} to start from"

    set_field(host, "VDS_HB_ST", state["VDS_HB_ST"] + 1)
    even, _ = window_width(host)
    assert even % 2 == 0, f"the hand-set width is {even}, not even"
    faulted = score(capture_device)

    assert (faulted["displaced"] >= clean["displaced"] * FAULT_FACTOR
            or faulted["rough"] >= clean["rough"] * FAULT_FACTOR), (
        f"an even memory window scored displaced {faulted['displaced']:.4f} "
        f"rough {faulted['rough']:.4f} against a clean "
        f"{clean['displaced']:.4f}/{clean['rough']:.4f} -- the score cannot see "
        "the fault it is guarding against")
