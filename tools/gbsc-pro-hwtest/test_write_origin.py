"""write_origin must not corrupt the picture on the way through a change.

No hardware. setfield's own docstring states the constraint: a sequence that
leaves the scaler producing more pixels than the memory window holds, *even for
one intermediate step*, will visibly corrupt the picture.

Two register pairs can break it, and they break it in opposite directions:

  IF_HB_SP2 / IF_HB_ST2    the capture window. Widening it raises the produced
                           pixel count, so a transient widening overruns.
  VDS_HB_SP / VDS_HB_ST    the memory window. Narrowing it lowers the space
                           available, so a transient narrowing overruns.

Both are two independent registers, so moving either takes two writes with a
state in between, and one of the two orders is wrong. The single quantity that
covers both is the headroom -- memory window minus produced -- which must never
dip on the way through. These tests pin that.

The baselines here are real states off the bench unit on 2026-08-05, which ran
at 14 px of headroom. At that margin a transient overrun of a hundred-odd pixels
is not a theoretical concern; it is the difference between a measurement and a
shredded picture.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import write_origin

# A real reading, with VDS_HSCALE 512 so produced is exactly twice the capture
# width and the arithmetic in the assertions stays legible.
BASE = {
    "IF_HB_SP2": 104, "IF_HB_ST2": 1124, "IF_HSYNC_RST": 1276,
    "VDS_HB_SP": 170, "VDS_HB_ST": 2224,
    "VDS_DIS_HB_SP": 256, "VDS_DIS_HB_ST": 2196,
    "VDS_HSYNC_RST": 2704, "VDS_HSCALE": 512,
}
SHIFT = 100

# The same reading's vertical half. IF_VB counts half-lines, so 513 of them at
# VSCALE 660 is 795.93 output lines, not 1591.9.
BASE_BOTH_AXES = dict(
    BASE,
    IF_VB_SP=56, IF_VB_ST=569,
    VDS_VB_SP=37, VDS_VB_ST=831,
    VDS_VSCALE=660, VDS_VSYNC_RST=1125,
)


def states_through(current, changes):
    """Every intermediate state, in the order the tool sends the writes."""
    state = dict(current)
    seen = []
    for name, value in write_origin.ordered_writes(current, changes):
        state[name] = value
        seen.append(dict(state))
    return seen


def produced(state):
    return (state["IF_HB_ST2"] - state["IF_HB_SP2"]) * 1024 / state["VDS_HSCALE"]


def memory_window(state):
    return state["VDS_HB_ST"] - state["VDS_HB_SP"]


def worst_headroom(current, changes):
    return min(memory_window(s) - produced(s) for s in states_through(current, changes))


def settled_headroom(current, changes):
    """Headroom at the end, which both orders reach -- the floor no ordering can
    improve on, and so the bar an intermediate state has to clear."""
    final = dict(current, **changes)
    return memory_window(final) - produced(final)


def pan(current, shift):
    return {"IF_HB_SP2": current["IF_HB_SP2"] + shift,
            "IF_HB_ST2": current["IF_HB_ST2"] + shift}


def slide_memory(current, shift):
    return {"VDS_HB_SP": current["VDS_HB_SP"] + shift,
            "VDS_HB_ST": current["VDS_HB_ST"] + shift}


def test_panning_earlier_never_dips_the_headroom():
    """Writing IF_HB_SP2 first moves the window's start earlier while its end is
    still where it was, widening the capture and overrunning the memory."""
    assert worst_headroom(BASE, pan(BASE, -SHIFT)) >= settled_headroom(BASE, pan(BASE, -SHIFT))


def test_panning_later_never_dips_the_headroom():
    assert worst_headroom(BASE, pan(BASE, SHIFT)) >= settled_headroom(BASE, pan(BASE, SHIFT))


def test_sliding_the_memory_window_later_never_dips_the_headroom():
    """The other pair, and the opposite hazard: writing VDS_HB_SP first shrinks
    the window while its far edge has not moved out yet."""
    changes = slide_memory(BASE, SHIFT)
    assert worst_headroom(BASE, changes) >= settled_headroom(BASE, changes)


def test_sliding_the_memory_window_earlier_never_dips_the_headroom():
    changes = slide_memory(BASE, -SHIFT)
    assert worst_headroom(BASE, changes) >= settled_headroom(BASE, changes)


def test_restoring_after_a_pan_never_dips_the_headroom():
    """Undoing a trial is the same move reversed and needs the same care."""
    panned = dict(BASE, **pan(BASE, SHIFT))
    restore = {name: BASE[name] for name in ("IF_HB_SP2", "IF_HB_ST2")}
    assert worst_headroom(panned, restore) >= settled_headroom(panned, restore)


def test_moving_both_windows_at_once_never_dips_the_headroom():
    """The real experiment moves capture and memory together. Ordering within
    each pair is not enough -- the two pairs interact through the headroom."""
    changes = dict(pan(BASE, SHIFT), **slide_memory(BASE, -SHIFT))
    assert worst_headroom(BASE, changes) >= settled_headroom(BASE, changes)


def test_a_pan_still_lands_on_the_requested_window():
    """Ordering must not change where it ends up, only how it gets there."""
    changes = pan(BASE, -SHIFT)
    final = states_through(BASE, changes)[-1]
    assert (final["IF_HB_SP2"], final["IF_HB_ST2"]) == (4, 1024)


def test_every_requested_write_is_sent_exactly_once():
    changes = dict(pan(BASE, SHIFT), **slide_memory(BASE, SHIFT))
    sent = [name for name, _ in write_origin.ordered_writes(BASE, changes)]
    assert sorted(sent) == sorted(changes)


def test_a_single_edge_change_is_passed_through():
    """The origin trial moves one register. Nothing to reorder or drop."""
    assert write_origin.ordered_writes(BASE, {"VDS_HB_SP": 300}) == [("VDS_HB_SP", 300)]


def test_fields_outside_the_two_windows_keep_their_given_order():
    """Ordering is only meaningful for the registers that bound the headroom;
    everything else should pass through as the caller wrote it."""
    changes = {"VDS_DIS_HB_SP": 300, "VDS_DIS_HB_ST": 2100}
    assert write_origin.ordered_writes(BASE, changes) == list(changes.items())


# --- the vertical pair has the same hazard and nothing was enforcing it -------


def test_headroom_reports_the_worse_of_the_two_axes():
    """A move that is safe horizontally and ruinous vertically must not be
    ordered as though it were safe. The bench sits at -1.9 lines vertically
    against +1034 px horizontally, so the vertical half is what decides."""
    assert write_origin.headroom_of(BASE_BOTH_AXES) < 0


def test_a_horizontal_only_state_still_reports_its_headroom():
    """Most callers read the horizontal context only; they must keep working."""
    assert write_origin.headroom_of(BASE) > 0


def test_a_vertical_scale_down_is_ordered_after_its_window_opens():
    """Lowering VSCALE grows the picture. Sent before the window widens, it
    overflows for as long as the two writes are apart -- the same failure the
    horizontal pair had, which corrupted the picture mid-pan."""
    changes = {"VDS_VSCALE": 500, "VDS_VB_ST": 1100}

    order = [name for name, _ in
             write_origin.ordered_writes(BASE_BOTH_AXES, changes)]

    assert order.index("VDS_VB_ST") < order.index("VDS_VSCALE")
