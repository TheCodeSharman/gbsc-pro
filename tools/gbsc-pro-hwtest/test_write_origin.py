"""write_origin's pan trials must not corrupt the picture on the way through.

No hardware. setfield's own docstring states the constraint these tests pin: a
sequence that leaves the scaler producing more pixels than the memory window
holds, *even for one intermediate step*, will visibly corrupt the picture. The
capture window is two independent registers, so moving it takes two writes and
one of the two orders widens it in between.

That matters because the baseline this tool runs against has ~16 px of headroom
-- the value CLAUDE.md records as artefacting -- so a transient widening is not
a theoretical concern, it is the difference between an answer and "it BROKE".
A trial that breaks the picture for a reason unrelated to the question answers
nothing, and the first run of this tool produced exactly that.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import write_origin

# A capture window with room to move in both directions, in the same shape the
# tool reads off the unit: the window is [SP2..ST2] and its width is ST2 - SP2.
BASE = {"IF_HB_SP2": 116, "IF_HB_ST2": 1124, "IF_HSYNC_RST": 1172}
SHIFT = 60


def widths_through(current, changes):
    """Capture width after each successive write, in the order the tool sends
    them. The intermediate states are the whole point -- the final state is
    correct under either order."""
    state = dict(current)
    widths = []
    for name, value in write_origin.ordered_writes(current, changes):
        state[name] = value
        widths.append(state["IF_HB_ST2"] - state["IF_HB_SP2"])
    return widths


def pan(current, shift):
    return {"IF_HB_SP2": current["IF_HB_SP2"] + shift,
            "IF_HB_ST2": current["IF_HB_ST2"] + shift}


def test_panning_earlier_never_widens_the_capture_window():
    """The negative pan is the one that was wrong: writing SP2 first moves the
    window's start earlier while its end is still where it was, which widens the
    window and overruns the memory window."""
    width = BASE["IF_HB_ST2"] - BASE["IF_HB_SP2"]
    assert max(widths_through(BASE, pan(BASE, -SHIFT))) <= width


def test_panning_later_never_widens_the_capture_window():
    width = BASE["IF_HB_ST2"] - BASE["IF_HB_SP2"]
    assert max(widths_through(BASE, pan(BASE, SHIFT))) <= width


def test_restoring_after_a_pan_never_widens_the_capture_window():
    """Putting the baseline back is another two-write move of the same window,
    so it needs the same care. Restoring a positive pan is a negative move."""
    panned = dict(BASE, **pan(BASE, SHIFT))
    width = BASE["IF_HB_ST2"] - BASE["IF_HB_SP2"]
    restore = {name: BASE[name] for name in ("IF_HB_SP2", "IF_HB_ST2")}
    assert max(widths_through(panned, restore)) <= width


def test_a_pan_still_lands_on_the_requested_window():
    """Ordering must not change where it ends up, only how it gets there."""
    changes = pan(BASE, -SHIFT)
    state = dict(BASE)
    for name, value in write_origin.ordered_writes(BASE, changes):
        state[name] = value
    assert (state["IF_HB_SP2"], state["IF_HB_ST2"]) == (56, 1064)


def test_every_requested_write_is_sent_exactly_once():
    changes = pan(BASE, SHIFT)
    sent = [name for name, _ in write_origin.ordered_writes(BASE, changes)]
    assert sorted(sent) == ["IF_HB_ST2", "IF_HB_SP2"][::-1] and len(sent) == 2


def test_a_single_edge_change_is_passed_through():
    """The origin trial moves one register. There is no intermediate state to
    get wrong, and nothing should be reordered or dropped."""
    changes = {"VDS_HB_SP": 300}
    assert write_origin.ordered_writes(BASE, changes) == [("VDS_HB_SP", 300)]
